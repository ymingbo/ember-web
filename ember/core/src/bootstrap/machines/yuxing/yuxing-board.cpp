/*
 * ------------------------------------------------------------------------
 * 名称：yuxing-board.cpp
 * 说明：裕兴学习机主板的实现：CPU 与 PPU 两侧的存储译码、整机寄存器、MMC3 模式、汉字单色模式与 4 分屏。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/yuxing/yuxing-board.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;

namespace {

/** 8K 窗口的大小与窗口内偏移的掩码。 */
constexpr std::size_t WINDOW_SIZE = 0x2000;
constexpr std::uint16_t WINDOW_MASK = 0x1FFF;
/** 1K 字符页的大小。 */
constexpr std::size_t CHARACTER_PAGE_SIZE = 1024;
/** 窗口 0 是 $6000-$7FFF，所以窗口序号是 (地址 >> 13) 减去 $6000 的页号 3。 */
constexpr std::size_t FIRST_WINDOW_PAGE = 3;
/** 程序 RAM 与 ROM 的 8K 页数。 */
constexpr std::size_t PROGRAM_RAM_PAGES = YuxingBoard::PROGRAM_RAM_SIZE / WINDOW_SIZE;
constexpr std::size_t ROM_PAGES = YuxingBoard::ROM_SIZE / WINDOW_SIZE;
/** MMC3 模式的程序页号取低 6 位，落在程序 RAM 的前 512K。 */
constexpr std::uint8_t MMC3_PROGRAM_BANK_MASK = 0x3F;
/** $6000 窗口在 reg5500 位 0-1 为 0 时接程序 RAM 的这一页（8K 页号，在 480K 处）；页号还要与程序 RAM 掩码相与。 */
constexpr std::size_t DEFAULT_WORK_PAGE = 0x3C;
/** 128K 程序 RAM、32K 字符 RAM 的型号（V7.0、V8.0-C、V8.2-D、V8.3-D）的 ROM 校验值。 */
constexpr std::array<std::uint32_t, 4> SMALL_MEMORY_ROM_CRCS{0x3DF65263U, 0x6BA6BD80U, 0x2B1B969EU, 0xA70FD0F3U};
/** 小容量型号的程序 RAM 掩码与字符页掩码；其余型号是 1MB 与 128K 对应的 $3F 与 $0F。 */
constexpr YuxingBoard::MemoryMasks SMALL_MEMORY_MASKS{0x07, 0x03};
constexpr YuxingBoard::MemoryMasks LARGE_MEMORY_MASKS{0x3F, 0x0F};
/** 软驱寄存器的别名地址：(地址 & $FE4F) == $4244，寄存器号由地址位 7、4、5 拼出。 */
constexpr std::uint16_t FLOPPY_ALIAS_MASK = 0xFE4F;
constexpr std::uint16_t FLOPPY_ALIAS_PATTERN = 0x4244;
/** reg5500 的地址：$55xx 的偶数地址都是它。 */
constexpr std::uint16_t REG5500_ADDRESS_MASK = 0xFF01;
constexpr std::uint16_t REG5500_ADDRESS = 0x5500;
/** $5002 的读出值。 */
constexpr std::uint8_t READ_5002 = 0x02;
/** 语音数据口协议的命令。 */
constexpr std::uint8_t SPEECH_RESET = 0x00;
constexpr std::uint8_t SPEECH_START = 0xFF;
constexpr std::uint8_t SPEECH_NIBBLE_MASK = 0xF0;
constexpr std::uint8_t SPEECH_NIBBLE_TAG = 0xC0;
constexpr std::uint8_t SPEECH_SCRAMBLE = 0x41;

/** CRC-32（IEEE 802.3）的查表；机型目录只能引用硬件模块，所以这里自备一份。 */
constexpr std::array<std::uint32_t, 256> makeCrcTable() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t index = 0; index < 256; ++index) {
    std::uint32_t value = index;
    for (int bit = 0; bit < 8; ++bit) value = (value & 1U) != 0 ? (value >> 1) ^ 0xEDB88320U : value >> 1;
    table[index] = value;
  }
  return table;
}

constexpr std::array<std::uint32_t, 256> CRC_TABLE = makeCrcTable();

std::uint32_t crc32Of(const std::vector<std::uint8_t>& data) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const std::uint8_t byte : data) crc = CRC_TABLE[(crc ^ byte) & 0xFFU] ^ (crc >> 8);
  return ~crc;
}

/** 软驱寄存器的别名地址。 */
bool isFloppyAlias(std::uint16_t address) { return (address & FLOPPY_ALIAS_MASK) == FLOPPY_ALIAS_PATTERN; }

/** 别名地址对应的软驱寄存器号：位 0 取地址位 7，位 1 取地址位 4，位 2 取地址位 5。 */
unsigned floppyAliasRegister(std::uint16_t address) { return ((address >> 7) & 1U) | ((address >> 3) & 2U) | ((address >> 3) & 4U); }

/** $4100-$5FFF 里有寄存器的地址（读或写）。 */
bool isRegisterAddress(std::uint16_t address) {
  if (isFloppyAlias(address)) return true;
  switch (address) {
    case 0x4200:
    case 0x4201:
    case 0x4202:
    case 0x4203:
    case 0x4205:
    case 0x4207:
    case 0x4302:
    case 0x4303:
    case 0x4304:
    case 0x4305:
    case 0x4700:
    case 0x4701:
    case 0x4800:
    case 0x5002:
    case 0x5004:
    case 0x5005:
    case 0x5500:
    case 0x5501:
      return true;
    default:
      return false;
  }
}

}  // namespace

YuxingBoard::MemoryMasks YuxingBoard::memoryMasksFor(std::uint32_t romCrc32) {
  return std::find(SMALL_MEMORY_ROM_CRCS.begin(), SMALL_MEMORY_ROM_CRCS.end(), romCrc32) != SMALL_MEMORY_ROM_CRCS.end() ? SMALL_MEMORY_MASKS : LARGE_MEMORY_MASKS;
}

YuxingBoard::YuxingBoard(std::vector<std::uint8_t> rom, logic::hardware::FdcUpd765& fdc, logic::hardware::SpeechLpc& speech, logic::hardware::KeyboardYuxing& keyboard, hw::MapperCircuit& mmc3)
    : floppy_(fdc),
      speech_(speech),
      keyboard_(keyboard),
      mmc3_(mmc3),
      rom_(std::move(rom)),
      programRam_(PROGRAM_RAM_SIZE, 0),
      mmc3WorkRam_(MMC3_WORK_RAM_SIZE, 0),
      staticRam_(STATIC_RAM_SIZE, 0),
      characterRam_(CHARACTER_RAM_SIZE, 0) {
  if (rom_.size() != ROM_SIZE) {
    throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "裕兴的 ROM 必须是 " + std::to_string(ROM_SIZE) + " 字节，收到 " + std::to_string(rom_.size()) + " 字节");
  }
  const MemoryMasks masks = memoryMasksFor(crc32Of(rom_));
  programRamMask_ = masks.programRam;
  characterMask_ = masks.character;
  reset(true);
}

YuxingBoard::Registers YuxingBoard::registers() const {
  return Registers{reg4800_, reg5500_, reg5501_, pageRegister_, keyboard_.rowSelect(), mmc3Mode_, horizontalMirroring_, textMode(), fourWaySplit()};
}

void YuxingBoard::reset(bool power) {
  if (power) {
    std::fill(programRam_.begin(), programRam_.end(), std::uint8_t{0});
    std::fill(mmc3WorkRam_.begin(), mmc3WorkRam_.end(), std::uint8_t{0});
    std::fill(staticRam_.begin(), staticRam_.end(), std::uint8_t{0});
    std::fill(characterRam_.begin(), characterRam_.end(), std::uint8_t{0});
    nametable_.fill(0);
  }
  reg4800_ = 0;
  reg5500_ = 0;
  reg5501_ = 0;
  pageRegister_ = 0;
  mmc3Mode_ = false;
  horizontalMirroring_ = false;
  speechReceiving_ = false;
  speechNibbles_ = 0;
  speechByte_ = 0;
  mmc3_.reset();
  text_.reset();
  applyBanks();
  updateTextMode();
}

// ---------------------------------------------------------------- 窗口

void YuxingBoard::setProgramRamWindows(std::size_t firstWindow, std::size_t first8kPage, std::size_t count) {
  for (std::size_t index = 0; index < count; ++index) windows_[firstWindow + index] = Window{CpuLocation::Kind::ProgramRam, ((first8kPage + index) % PROGRAM_RAM_PAGES) * WINDOW_SIZE};
}

void YuxingBoard::setRomWindows(std::size_t first8kPage) {
  for (std::size_t index = 0; index < 4; ++index) windows_[1 + index] = Window{CpuLocation::Kind::Rom, ((first8kPage + index) % ROM_PAGES) * WINDOW_SIZE};
}

void YuxingBoard::applyBanks() {
  // MMC3 模式下程序窗口与名称表镜像由 MMC3 电路管；reg5500 位 2 为 1 时 $6000 窗口是电路自己的 8K 工作 RAM。
  if (programRamMode() && mmc3Mode_) {
    windows_[0] = Window{CpuLocation::Kind::Mmc3WorkRam, 0};
    return;
  }
  if (programRamMode()) {
    if ((reg5500_ & 0x40U) != 0) {
      setProgramRamWindows(1, static_cast<std::size_t>(pageRegister_ >> 1) * 4U, 4);
    } else {
      setProgramRamWindows(1, static_cast<std::size_t>(pageRegister_) * 2U, 2);
      setProgramRamWindows(3, static_cast<std::size_t>(programRamMask_) * 2U, 2);
    }
  } else {
    setRomWindows(static_cast<std::size_t>(reg4800_ & 0x1FU) * 4U);
  }
  horizontalMirroring_ = (reg5500_ & 0x08U) != 0;
  // $6000 窗口：小容量型号（V8.x，程序 RAM 只有 128K）选择 1-3 接独立的静态 RAM，不占程序 RAM；
  // 大容量型号是程序 RAM 的 8K 页：位 0-1 与 $3C 按位或。
  const unsigned workPage = reg5500_ & 0x03U;
  if (workPage != 0 && programRamMask_ <= SMALL_MEMORY_MASKS.programRam) {
    windows_[0] = Window{CpuLocation::Kind::StaticRam, workPage * WINDOW_SIZE};
    return;
  }
  windows_[0] = Window{CpuLocation::Kind::ProgramRam, (((DEFAULT_WORK_PAGE & programRamMask_) | workPage) % PROGRAM_RAM_PAGES) * WINDOW_SIZE};
}

void YuxingBoard::syncMmc3Windows() {
  for (std::size_t index = 0; index < 4; ++index) {
    const unsigned page = mmc3_.bank(hw::BankSpace::Program, static_cast<std::uint16_t>(0x8000U + index * WINDOW_SIZE)) & MMC3_PROGRAM_BANK_MASK;
    windows_[1 + index] = Window{CpuLocation::Kind::ProgramRam, (page % PROGRAM_RAM_PAGES) * WINDOW_SIZE};
  }
}

void YuxingBoard::updateTextMode() { text_.setTextMode(textMode()); }

// ---------------------------------------------------------------- 存储译码

YuxingBoard::CpuLocation YuxingBoard::locate(std::uint16_t address) const {
  using Kind = CpuLocation::Kind;
  if (address < 0x4100U) return {};
  if (address < 0x6000U) return isRegisterAddress(address) ? CpuLocation{Kind::Register, address} : CpuLocation{};
  const Window& window = windows_[(address >> 13) - FIRST_WINDOW_PAGE];
  return {window.kind, window.base + (address & WINDOW_MASK)};
}

hw::BusRead YuxingBoard::readCpu(std::uint16_t address, bool sideEffects) {
  if (address < 0x4100U) return hw::driveBus(0);
  const CpuLocation where = locate(address);
  switch (where.kind) {
    case CpuLocation::Kind::Rom:
      return hw::driveBus(rom_[where.offset]);
    case CpuLocation::Kind::ProgramRam:
      return hw::driveBus(programRam_[where.offset]);
    case CpuLocation::Kind::Mmc3WorkRam:
      return hw::driveBus(mmc3WorkRam_[where.offset]);
    case CpuLocation::Kind::StaticRam:
      return hw::driveBus(staticRam_[where.offset]);
    case CpuLocation::Kind::Register:
      if (isFloppyAlias(address)) return hw::driveBus(floppy_.readRegister(floppyAliasRegister(address), sideEffects));
      switch (address) {
        case 0x4207:
          return hw::driveBus(keyboard_.read());
        case 0x5002:
          return hw::driveBus(READ_5002);
        case 0x4304:
          return hw::driveBus(floppy_.readRegister(4, sideEffects));
        case 0x4305:
          return hw::driveBus(floppy_.readRegister(5, sideEffects));
        case 0x4701:
          return hw::driveBus(speech_.read(address).value);
        default:
          // 只写的寄存器没有读通路。
          return hw::releaseBus();
      }
    case CpuLocation::Kind::None:
      break;
  }
  return hw::releaseBus();
}

void YuxingBoard::writeSpeech(std::uint8_t value) {
  if (value == SPEECH_RESET) {
    speechReceiving_ = false;
    speechNibbles_ = 0;
    speech_.reset(false);
  } else if (value == SPEECH_START) {
    speechReceiving_ = true;
  } else if ((value & SPEECH_NIBBLE_MASK) == SPEECH_NIBBLE_TAG && speechReceiving_) {
    if (++speechNibbles_ == 1) {
      speechByte_ = static_cast<std::uint8_t>(value & 0x0FU);
    } else {
      speechByte_ = static_cast<std::uint8_t>(speechByte_ | ((value & 0x0FU) << 4));
      speech_.write(0x4700, static_cast<std::uint8_t>(speechByte_ ^ SPEECH_SCRAMBLE));
      speechNibbles_ = 0;
    }
  }
}

void YuxingBoard::writeLow(std::uint16_t address, std::uint8_t value) {
  if (isFloppyAlias(address)) {
    floppy_.writeRegister(floppyAliasRegister(address), value);
    return;
  }
  if ((address & REG5500_ADDRESS_MASK) == REG5500_ADDRESS) address = REG5500_ADDRESS;
  switch (address) {
    case 0x4200:
      floppy_.writeRegister(7, value);
      break;
    case 0x4201:
      floppy_.writeRegister(2, value);
      break;
    case 0x4205:
      floppy_.writeRegister(5, value);
      break;
    case 0x4202:
    case 0x4302:
    case 0x5004:
      keyboard_.selectLow(value);
      break;
    case 0x4203:
    case 0x4303:
    case 0x5005:
      keyboard_.selectHigh(value);
      break;
    case 0x4700:
      writeSpeech(value);
      break;
    case 0x4800:
      reg4800_ = value;
      applyBanks();
      updateTextMode();
      break;
    case 0x5500:
      reg5500_ = value;
      applyBanks();
      updateTextMode();
      break;
    case 0x5501:
      reg5501_ = value;
      mmc3Mode_ = (value & 0x80U) != 0;
      // $6000 窗口与 ROM 窗口要随模式重排，之后 MMC3 电路才接管 $8000-$FFFF；汉字模式与 4 分屏也随模式变化。
      applyBanks();
      if (mmc3Mode_) syncMmc3Windows();
      updateTextMode();
      break;
    default:
      break;
  }
}

void YuxingBoard::writeProgram(std::uint16_t address, std::uint8_t value) {
  if (mmc3Mode_) {
    mmc3_.writeRegister(address, value);
    // 名称表镜像由 MMC3 的 $A000 写入决定：位 0 为 1 水平。
    if ((address & 0xE001U) == 0xA000U) horizontalMirroring_ = (value & 1U) != 0;
    syncMmc3Windows();
    return;
  }
  if (programRamMode()) {
    if ((reg4800_ & 0x20U) != 0) {
      pageRegister_ = static_cast<std::uint8_t>(value & programRamMask_);
    } else {
      const Window& window = windows_[(address >> 13) - FIRST_WINDOW_PAGE];
      if (window.kind == CpuLocation::Kind::ProgramRam) programRam_[window.base + (address & WINDOW_MASK)] = value;
    }
  }
  applyBanks();
}

void YuxingBoard::writeCpu(std::uint16_t address, std::uint8_t value) {
  if (address < 0x4100U) return;
  if (address < 0x6000U) {
    writeLow(address, value);
    return;
  }
  if (address >= 0x8000U) {
    writeProgram(address, value);
    return;
  }
  // $6000-$7FFF 窗口：程序 RAM 与 MMC3 工作 RAM 可写，ROM 不可写。
  const CpuLocation where = locate(address);
  if (where.kind == CpuLocation::Kind::ProgramRam) programRam_[where.offset] = value;
  else if (where.kind == CpuLocation::Kind::Mmc3WorkRam) mmc3WorkRam_[where.offset] = value;
  else if (where.kind == CpuLocation::Kind::StaticRam) staticRam_[where.offset] = value;
}

// ---------------------------------------------------------------- PPU 侧

std::size_t YuxingBoard::characterOffset(std::uint16_t address, bool latched) const {
  if (mmc3Mode_) {
    // 电路给出的 1K 页号换算到字符 RAM：页号的位 7-3 里，位 4 不动，位 1-0 与位 3-2 互换。
    const unsigned chip = mmc3_.bank(hw::BankSpace::Character, address);
    const unsigned fixed = (chip >> 3) & characterMask_;
    const unsigned page = ((((fixed & 0x10U) | ((fixed << 2) & 0x0CU) | ((fixed >> 2) & 3U)) << 3) | (chip & 7U)) & 0x1FFU;
    return (static_cast<std::size_t>(page) * CHARACTER_PAGE_SIZE + (address & 0x3FFU)) % CHARACTER_RAM_SIZE;
  }
  if (latched && fourWaySplit() && (address & 0x1000U) != 0) {
    const unsigned region = (text_.tileAddress() >> 8) & 3U;
    const std::size_t fourKilobytePage = (static_cast<std::size_t>(region) << 3) | (static_cast<std::size_t>(reg5501_ & 3U) << 1) | 1U;
    return (fourKilobytePage * 0x1000U + (address & 0x0FFFU)) % CHARACTER_RAM_SIZE;
  }
  return (static_cast<std::size_t>(reg5501_ & characterMask_) * WINDOW_SIZE + (address & WINDOW_MASK)) % CHARACTER_RAM_SIZE;
}

std::size_t YuxingBoard::nametableOffset(std::uint16_t address) const {
  const unsigned slot = (address >> 10) & 3U;
  const unsigned page = horizontalMirroring_ ? (slot >> 1) : (slot & 1U);
  return static_cast<std::size_t>(page) * CHARACTER_PAGE_SIZE + (address & 0x3FFU);
}

std::size_t YuxingBoard::locateVideo(std::uint16_t address, bool& isPattern) const {
  const std::uint16_t masked = address & 0x3FFFU;
  isPattern = masked < 0x2000U;
  return isPattern ? characterOffset(masked, false) : nametableOffset(masked);
}

hw::BusRead YuxingBoard::VideoSide::read(std::uint16_t address) {
  const std::uint16_t masked = address & 0x3FFFU;
  if (board_.mmc3Mode_) board_.mmc3_.onVideoAddress(masked);
  board_.text_.observe(masked);
  if (masked < 0x2000U) return hw::driveBus(board_.characterRam_[board_.characterOffset(board_.text_.remap(masked), true)]);
  return hw::driveBus(board_.nametable_[board_.nametableOffset(masked)]);
}

hw::BusRead YuxingBoard::VideoSide::peek(std::uint16_t address) {
  bool isPattern = false;
  const std::size_t offset = board_.locateVideo(address, isPattern);
  return hw::driveBus(isPattern ? board_.characterRam_[offset] : board_.nametable_[offset]);
}

void YuxingBoard::VideoSide::write(std::uint16_t address, std::uint8_t value) {
  const std::uint16_t masked = address & 0x3FFFU;
  if (board_.mmc3Mode_) board_.mmc3_.onVideoAddress(masked);
  bool isPattern = false;
  const std::size_t offset = board_.locateVideo(masked, isPattern);
  if (isPattern) board_.characterRam_[offset] = value;
  else board_.nametable_[offset] = value;
}

// ---------------------------------------------------------------- CPU 侧总线设备

hw::BusRead YuxingBoard::CpuSide::read(std::uint16_t address) { return board_.readCpu(address, true); }

hw::BusRead YuxingBoard::CpuSide::peek(std::uint16_t address) { return board_.readCpu(address, false); }

void YuxingBoard::CpuSide::write(std::uint16_t address, std::uint8_t value) { board_.writeCpu(address, value); }

}  // namespace ember::bootstrap
