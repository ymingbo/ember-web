/*
 * ------------------------------------------------------------------------
 * 名称：bbg-board.cpp
 * 说明：步步高主板的实现：CPU 与 PPU 两侧的存储译码、整机寄存器、行时钟、扫描线 IRQ 与分屏。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/bbg/bbg-board.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;

namespace {

/** 8K 程序页的大小与页内偏移掩码。 */
constexpr std::size_t PROGRAM_PAGE_SIZE = 0x2000;
constexpr std::uint16_t PROGRAM_PAGE_MASK = 0x1FFF;
/** 字符页的大小。 */
constexpr std::size_t CHARACTER_PAGE_SIZE = 1024;
/** 每帧的行数（Dendy）与可见行数。 */
constexpr int LINES_PER_FRAME = 312;
constexpr int VISIBLE_LINES = 240;
/** 系统工作区在 DRAM 顶部 32K 里占前 16K；顶部 32K 的后 16K 是 MMC3 模式的固定页。 */
constexpr std::size_t TOP_RESERVED_SIZE = 32 * 1024;
/** $FF01 视频控制的位。 */
constexpr std::uint8_t VIDEO_MIRROR_MASK = 0x03;
constexpr std::uint8_t VIDEO_IRQ_ENABLE = 0x04;
constexpr std::uint8_t VIDEO_MAP_RAM = 0x08;
constexpr std::uint8_t VIDEO_MMC3_MODE = 0x20;
constexpr std::uint8_t VIDEO_SPLIT = 0x40;
/** 页寄存器的位 7：选 DRAM。 */
constexpr std::uint8_t PAGE_RAM_BIT = 0x80;
/** $FFB0 读出的值。 */
constexpr std::uint8_t IRQ_ACKNOWLEDGE_VALUE = 0x06;
/** $FF3C 里的游戏数据大小：位 7-4 选程序页数（取反），位 2 选字符页范围。 */
constexpr std::uint8_t GAME_SIZE_PROGRAM_MASK = 0xF0;
constexpr std::uint8_t GAME_SIZE_CHARACTER_BIT = 0x04;
/** 并口：状态位。 */
constexpr std::uint8_t PARALLEL_ERROR_N = 0x08;
constexpr std::uint8_t PARALLEL_SELECT = 0x10;
constexpr std::uint8_t PARALLEL_PAPER_OUT = 0x20;
constexpr std::uint8_t PARALLEL_ACK_N = 0x40;
constexpr std::uint8_t PARALLEL_BUSY_N = 0x80;
/** 并口：控制位。 */
constexpr std::uint8_t PARALLEL_CONTROL_STROBE = 0x01;
constexpr std::uint8_t PARALLEL_CONTROL_LINE_FEED = 0x02;
constexpr std::uint8_t PARALLEL_CONTROL_NOT_RESET = 0x04;
constexpr std::uint8_t PARALLEL_CONTROL_SELECT = 0x08;

bool isPowerOfTwo(std::size_t value) { return value != 0 && (value & (value - 1)) == 0; }

void requireSize(std::size_t size, const char* name) {
  if (size < PROGRAM_PAGE_SIZE * 4 || !isPowerOfTwo(size / PROGRAM_PAGE_SIZE) || size % PROGRAM_PAGE_SIZE != 0) {
    throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, std::string(name) + " 的容量必须是 8K 的 2 的幂倍");
  }
}

}  // namespace

const BbgVariant& BbgVariant::floppyOne() {
  static const BbgVariant variant{"bbg1", 128 * 1024, 512 * 1024, 0x1F};
  return variant;
}

const BbgVariant& BbgVariant::model98() {
  static const BbgVariant variant{"bbg98", 2048 * 1024, 2048 * 1024, 0xFF};
  return variant;
}

BbgBoard::BbgBoard(const BbgVariant& variant, std::vector<std::uint8_t> rom, hw::Scheduler& scheduler, hw::IrqLine& irq, const logic::hardware::Ppu2c02& ppu, hw::BusDevice& ppuRegisters,
                   logic::hardware::FdcUpd765& fdc, hw::ParallelPort& parallel, logic::hardware::SpeechLpc& speech, hw::MapperCircuit& mmc3, hw::MasterTime lineTicks, hw::MasterTime dotTicks)
    : variant_(variant),
      scheduler_(scheduler),
      irq_(irq),
      irqSource_(irq.addSource()),
      ppu_(ppu),
      ppuRegisters_(ppuRegisters),
      floppy_(fdc),
      parallel_(parallel),
      speech_(speech),
      mmc3_(mmc3),
      lineTicks_(lineTicks),
      dotTicks_(dotTicks),
      rom_(std::move(rom)),
      dram_(variant.dramSize, 0),
      characterRam_(CHARACTER_RAM_SIZE, 0),
      romPageMask_(static_cast<unsigned>(variant.romSize / PROGRAM_PAGE_SIZE - 1)),
      dramPageMask_(static_cast<unsigned>(variant.dramSize / PROGRAM_PAGE_SIZE - 1)),
      systemBase_(variant.dramSize - TOP_RESERVED_SIZE) {
  requireSize(variant.romSize, "BIOS ROM");
  requireSize(variant.dramSize, "DRAM");
  if (rom_.size() != variant.romSize) {
    throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, std::string(variant.id) + " 的 BIOS 必须是 " + std::to_string(variant.romSize) + " 字节，收到 " + std::to_string(rom_.size()) + " 字节");
  }
  if (variant.dramSize < TOP_RESERVED_SIZE * 2 || variant.characterPageMask >= CHARACTER_RAM_SIZE / CHARACTER_PAGE_SIZE) {
    throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "步步高机型的 DRAM 或字符页容量不合格");
  }
  reset(true);
}

BbgBoard::Registers BbgBoard::registers() const {
  return Registers{videoControl_, prgBank_[0], prgBank_[1], ramEnable_, cdBank_, efBank_, forceRam_, scanlineCounter_, mmc3Mode_, splitMode_};
}

void BbgBoard::reset(bool power) {
  if (power) {
    std::fill(dram_.begin(), dram_.end(), std::uint8_t{0});
    std::fill(characterRam_.begin(), characterRam_.end(), std::uint8_t{0});
    nametable_.fill(0);
  }
  videoControl_ = 0;
  irqEnable_ = false;
  mapRam_ = false;
  mmc3Mode_ = false;
  splitMode_ = false;
  prgBank_.fill(0);
  ramEnable_ = false;
  cdBank_ = 0;
  efBank_ = 0;
  forceRam_ = false;
  characterReg_.fill(0);
  characterEffective_.fill(0);
  scanlineCounter_ = 0;
  gameDataSize_ = 0;
  speechResetBit_ = false;
  splitLines_.clear();
  splitBanks_.clear();
  pendingLines_.clear();
  pendingBanks_.clear();
  splitIndex_ = 0;
  mmc3ProgramMask_ = 0x3F;
  mmc3CharacterMask_ = 0x7F;
  clearIrq();
  mmc3_.reset();
}

// ---------------------------------------------------------------- 存储译码

std::size_t BbgBoard::dramOffset(unsigned page, std::uint16_t address) const { return static_cast<std::size_t>(page & dramPageMask_) * PROGRAM_PAGE_SIZE + (address & PROGRAM_PAGE_MASK); }

std::size_t BbgBoard::mmc3DramOffset(std::uint16_t address) const {
  // 游戏装在 DRAM 里：游戏的页号落到 DRAM 的高端，最后两页固定在 DRAM 的最后；游戏的倒数第 4、第 3 页与系统工作区重合，改放到 DRAM 的最前面。
  const unsigned masked = mmc3_.bank(hw::BankSpace::Program, address) & mmc3ProgramMask_;
  unsigned page = masked + (dramPageMask_ - mmc3ProgramMask_);
  if (masked == mmc3ProgramMask_ - 3) page = 0;
  else if (masked == mmc3ProgramMask_ - 2) page = 1;
  return dramOffset(page, address);
}

BbgBoard::CpuLocation BbgBoard::locate(std::uint16_t address) const {
  using Kind = CpuLocation::Kind;
  if (address < 0x4020U) return {};
  if (address < 0x8000U) {
    if (address >= 0x4400U || mmc3Mode_) return {Kind::Dram, systemBase_ + (address - 0x4000U)};
    return {};
  }
  if (mmc3Mode_) return {Kind::Dram, mmc3DramOffset(address)};
  const unsigned page = address >> 13;
  const std::size_t romBase = page == 6 ? (romPageMask_ - 1U) : romPageMask_;
  switch (page) {
    case 4:
    case 5:
      if (ramEnable_) return {Kind::Dram, dramOffset(prgBank_[page - 4], address)};
      return {Kind::Rom, static_cast<std::size_t>(prgBank_[page - 4] & romPageMask_) * PROGRAM_PAGE_SIZE + (address & PROGRAM_PAGE_MASK)};
    case 6:
      if (upperIsRam()) return {Kind::Dram, dramOffset(cdBank_, address)};
      return {Kind::Rom, romBase * PROGRAM_PAGE_SIZE + (address & PROGRAM_PAGE_MASK)};
    default:
      if (upperIsRam()) return {Kind::Dram, dramOffset(efBank_, address)};
      if (address >= 0xFF00U && (address & 7U) == 0) return {Kind::Register, address & 0xFFU};
      return {Kind::Rom, romBase * PROGRAM_PAGE_SIZE + (address & PROGRAM_PAGE_MASK)};
  }
}

std::uint8_t BbgBoard::readCpu(std::uint16_t address, bool sideEffects, std::uint8_t& drivenMask) {
  drivenMask = 0xFF;
  if (address < 0x8000U) {
    if (address >= 0x4400U || mmc3Mode_) return dram_[systemBase_ + (address - 0x4000U)];
    drivenMask = 0;
    return 0;
  }
  const CpuLocation where = locate(address);
  switch (where.kind) {
    case CpuLocation::Kind::Dram:
      return dram_[where.offset];
    case CpuLocation::Kind::Register: {
      const std::optional<std::uint8_t> value = readRegister(address, sideEffects);
      if (value) return *value;
      // 调试读：读不到内容的寄存器不驱动数据线。
      if (!sideEffects) {
        drivenMask = 0;
        return 0;
      }
      // 没有这个寄存器：读到 ROM 最后一页里同一个地址的内容。
      return rom_[static_cast<std::size_t>(romPageMask_) * PROGRAM_PAGE_SIZE + (address & PROGRAM_PAGE_MASK)];
    }
    case CpuLocation::Kind::Rom:
      return rom_[where.offset];
    case CpuLocation::Kind::None:
      break;
  }
  drivenMask = 0;
  return 0;
}

std::optional<std::uint8_t> BbgBoard::readRegister(std::uint16_t address, bool sideEffects) {
  switch (address) {
    case 0xFF18:
      return speech_.read(address).value;
    case 0xFF40:
    case 0xFF50:
      return std::uint8_t{0};
    case 0xFF48: {
      std::uint8_t status = 0;
      if (!parallel_.error()) status = static_cast<std::uint8_t>(status | PARALLEL_ERROR_N);
      if (parallel_.select()) status = static_cast<std::uint8_t>(status | PARALLEL_SELECT);
      if (parallel_.paperOut()) status = static_cast<std::uint8_t>(status | PARALLEL_PAPER_OUT);
      if (!parallel_.ack()) status = static_cast<std::uint8_t>(status | PARALLEL_ACK_N);
      if (parallel_.busyN()) status = static_cast<std::uint8_t>(status | PARALLEL_BUSY_N);
      return status;
    }
    case 0xFFB0:
      if (sideEffects) clearIrq();
      return IRQ_ACKNOWLEDGE_VALUE;
    case 0xFF80:
    case 0xFF88:
    case 0xFF90:
    case 0xFF98:
    case 0xFFA0:
    case 0xFFA8:
    case 0xFFB8:
      return floppy_.readPcPort((address >> 3) & 7U, sideEffects);
    default:
      return std::nullopt;
  }
}

void BbgBoard::writeProgramRam(std::uint16_t address, std::uint8_t value) {
  const unsigned page = address >> 13;
  switch (page) {
    case 4:
    case 5:
      if (ramEnable_) dram_[dramOffset(prgBank_[page - 4], address)] = value;
      break;
    case 6:
      if (upperIsRam()) dram_[dramOffset(cdBank_, address)] = value;
      break;
    default:
      if (upperIsRam()) dram_[dramOffset(efBank_, address)] = value;
      break;
  }
}

void BbgBoard::writeCpu(std::uint16_t address, std::uint8_t value) {
  if (address < 0x8000U) {
    if (address >= 0x4400U || (mmc3Mode_ && address >= 0x4020U)) dram_[systemBase_ + (address - 0x4000U)] = value;
    return;
  }
  if (mmc3Mode_) {
    mmc3_.writeRegister(address, value);
    if (forceRam_) return;
    if (address < 0xFF00U) return;
  } else if (address < 0xFF00U) {
    writeProgramRam(address, value);
    return;
  }
  writeRegister(address, value);
}

void BbgBoard::shadowPpuWrite(std::uint16_t address, std::uint8_t value) {
  const unsigned reg = address & 7U;
  if ((reg == 0 || reg == 1 || reg == 5) && upperIsRam()) dram_[systemBase_ + 0x2000U + (address & PROGRAM_PAGE_MASK)] = value;
}

void BbgBoard::applyCharacterBanks() {
  if (splitMode_) return;
  for (std::size_t index = 0; index < characterReg_.size(); ++index) characterEffective_[index] = characterReg_[index] & variant_.characterPageMask;
}

void BbgBoard::writeRegister(std::uint16_t address, std::uint8_t value) {
  if (address < 0xFF90U) {
    const unsigned index = address & 0xFFU;
    switch (index) {
      case 0x01:
        videoControl_ = value;
        irqEnable_ = (value & VIDEO_IRQ_ENABLE) != 0;
        mapRam_ = (value & VIDEO_MAP_RAM) != 0;
        mmc3Mode_ = (value & VIDEO_MMC3_MODE) != 0;
        splitMode_ = (value & VIDEO_SPLIT) != 0;
        clearIrq();
        if (mmc3Mode_) {
          mmc3ProgramMask_ = static_cast<unsigned>(~(gameDataSize_ & GAME_SIZE_PROGRAM_MASK)) & 0x3FU;
          mmc3CharacterMask_ = (gameDataSize_ & GAME_SIZE_CHARACTER_BIT) != 0 ? 0xFFU : 0x7FU;
          mmc3_.reset();
        }
        break;
      case 0x02:
        scanlineCounter_ = static_cast<std::uint8_t>((scanlineCounter_ & 0xF0U) | (value & 0x0FU));
        break;
      case 0x06:
        scanlineCounter_ = static_cast<std::uint8_t>((scanlineCounter_ & 0x0FU) | (value << 4));
        break;
      case 0x03:
      case 0x0B:
      case 0x13:
      case 0x1B: {
        // 2K 字符页：占相邻的两个 1K 页。
        const std::size_t first = (index >> 3) * 2U;
        characterReg_[first] = static_cast<std::uint8_t>(value << 1);
        characterReg_[first + 1] = static_cast<std::uint8_t>((value << 1) | 1U);
        break;
      }
      case 0x04:
        ramEnable_ = (value & PAGE_RAM_BIT) != 0;
        prgBank_[0] = static_cast<std::uint8_t>(value << 1);
        prgBank_[1] = static_cast<std::uint8_t>((value << 1) | 1U);
        break;
      case 0x14:
        prgBank_[0] = value;
        ramEnable_ = true;
        break;
      case 0x1C:
        prgBank_[1] = value;
        ramEnable_ = true;
        break;
      case 0x10: {
        const bool bit = (value & 1U) != 0;
        if (!speechResetBit_ && bit) speech_.reset(false);
        speechResetBit_ = bit;
        break;
      }
      case 0x18:
        speech_.write(address, value);
        break;
      case 0x11:
        forceRam_ = (value & PAGE_RAM_BIT) != 0;
        break;
      case 0x19:
        forceRam_ = false;
        break;
      case 0x0A:
        pendingLines_.push_back(value);
        break;
      case 0x1A:
        pendingBanks_.push_back(value);
        break;
      case 0x12:
        splitLines_.clear();
        splitBanks_.clear();
        splitIndex_ = 0;
        break;
      case 0x22:
        splitLines_ = pendingLines_;
        splitBanks_ = pendingBanks_;
        pendingLines_.clear();
        pendingBanks_.clear();
        break;
      case 0x2A:
        splitIndex_ = 0;
        break;
      case 0x24:
        cdBank_ = value;
        break;
      case 0x2C:
        efBank_ = value;
        break;
      case 0x23:
      case 0x2B:
      case 0x33:
      case 0x3B:
      case 0x43:
      case 0x4B:
      case 0x53:
      case 0x5B:
        characterReg_[(index - 0x23U) >> 3] = value;
        break;
      case 0x3C:
        gameDataSize_ = value;
        break;
      case 0x40:
        parallel_.writeData(value);
        break;
      case 0x50:
        parallel_.setStrobeN((value & PARALLEL_CONTROL_STROBE) == 0);
        parallel_.setLineFeedN((value & PARALLEL_CONTROL_LINE_FEED) == 0);
        parallel_.setReset((value & PARALLEL_CONTROL_NOT_RESET) == 0);
        parallel_.setSelectPrinterN((value & PARALLEL_CONTROL_SELECT) == 0);
        break;
      case 0x80:
        floppy_.writePcPort(0, value);
        break;
      case 0x88:
        floppy_.writePcPort(1, value);
        break;
      default:
        break;
    }
    applyCharacterBanks();
  } else if (address == 0xFF98U) {
    clearIrq();
    applyCharacterBanks();
  } else if (address == 0xFFB0U) {
    clearIrq();
  } else if (address == 0xFFA0U) {
    floppy_.writePcPort(4, value);
  } else if (address <= 0xFFB8U) {
    floppy_.writePcPort((address >> 3) & 7U, value);
  }
}

// ---------------------------------------------------------------- 行时钟

void BbgBoard::startLineClock() {
  stopLineClock();
  line_ = 0;
  nextLineTime_ = scheduler_.now() + static_cast<hw::MasterTime>(LINE_EVENT_DOT) * dotTicks_;
  lineEvent_ = scheduler_.scheduleAt(nextLineTime_, *this, 0);
}

void BbgBoard::stopLineClock() {
  if (lineEvent_ != hw::NO_EVENT) scheduler_.cancel(lineEvent_);
  lineEvent_ = hw::NO_EVENT;
}

void BbgBoard::onEvent(hw::MasterTime /*when*/, std::uint32_t /*tag*/) {
  lineEvent_ = hw::NO_EVENT;
  const int current = line_;
  line_ = (line_ + 1) % LINES_PER_FRAME;
  nextLineTime_ += lineTicks_;
  lineEvent_ = scheduler_.scheduleAt(nextLineTime_, *this, 0);
  onLine(current);
}

void BbgBoard::onLine(int line) {
  if (mmc3Mode_) return;
  if (splitMode_) {
    if (line >= VISIBLE_LINES) {
      splitIndex_ = 0;
      scanlineCounter_ = 0;
    } else {
      ++scanlineCounter_;
    }
    if (scanlineCounter_ == 0 && splitIndex_ < splitBanks_.size() && splitIndex_ < splitLines_.size()) {
      const unsigned banks = splitBanks_[splitIndex_];
      const unsigned low = banks & 0x0FU;
      const unsigned high = (banks >> 4) & 0x0FU;
      characterEffective_[0] = low * 2U;
      characterEffective_[1] = low * 2U + 1U;
      characterEffective_[2] = high * 2U;
      characterEffective_[3] = high * 2U + 1U;
      scanlineCounter_ = splitLines_[splitIndex_];
      ++splitIndex_;
    }
    if (line == VISIBLE_LINES - 1 && irqEnable_) raiseIrq();
  } else {
    ++scanlineCounter_;
    if (scanlineCounter_ == 0 && irqEnable_ && line < VISIBLE_LINES && ppu_.renderingEnabled()) raiseIrq();
  }
}

// ---------------------------------------------------------------- PPU 侧

std::size_t BbgBoard::characterOffset(std::uint16_t address) const {
  const unsigned slot = (address >> 10) & 7U;
  const unsigned page = mmc3Mode_ ? (mmc3_.bank(hw::BankSpace::Character, address) & mmc3CharacterMask_) : characterEffective_[slot];
  return static_cast<std::size_t>(page & 0xFFU) * CHARACTER_PAGE_SIZE + (address & 0x3FFU);
}

std::size_t BbgBoard::nametableOffset(std::uint16_t address) const {
  const std::uint16_t masked = address & 0x2FFFU;
  const unsigned slot = (masked >> 10) & 3U;
  unsigned page = 0;
  if (mmc3Mode_) {
    page = mmc3_.mirroring().value_or(hw::CircuitMirroring::Vertical) == hw::CircuitMirroring::Horizontal ? (slot >> 1) : (slot & 1U);
  } else {
    switch (videoControl_ & VIDEO_MIRROR_MASK) {
      case 0:
        page = slot & 1U;
        break;
      case 1:
        page = slot >> 1;
        break;
      case 2:
        page = 0;
        break;
      default:
        page = 1;
        break;
    }
  }
  return static_cast<std::size_t>(page) * CHARACTER_PAGE_SIZE + (masked & 0x3FFU);
}

std::size_t BbgBoard::locateVideo(std::uint16_t address, bool& isPattern) const {
  const std::uint16_t masked = address & 0x3FFFU;
  isPattern = masked < 0x2000U;
  return isPattern ? characterOffset(masked) : nametableOffset(masked);
}

std::uint8_t BbgBoard::readVideo(std::uint16_t address, bool notifyCircuit) {
  const std::uint16_t masked = address & 0x3FFFU;
  if (notifyCircuit && mmc3Mode_) mmc3_.onVideoAddress(masked);
  return masked < 0x2000U ? characterRam_[characterOffset(masked)] : nametable_[nametableOffset(masked)];
}

void BbgBoard::writeVideo(std::uint16_t address, std::uint8_t value) {
  const std::uint16_t masked = address & 0x3FFFU;
  if (mmc3Mode_) mmc3_.onVideoAddress(masked);
  if (masked < 0x2000U) characterRam_[characterOffset(masked)] = value;
  else nametable_[nametableOffset(masked)] = value;
}

// ---------------------------------------------------------------- 总线设备

hw::BusRead BbgBoard::CpuSide::read(std::uint16_t address) {
  std::uint8_t mask = 0xFF;
  const std::uint8_t value = board_.readCpu(address, true, mask);
  return hw::BusRead{value, mask};
}

hw::BusRead BbgBoard::CpuSide::peek(std::uint16_t address) {
  std::uint8_t mask = 0xFF;
  const std::uint8_t value = board_.readCpu(address, false, mask);
  return hw::BusRead{value, mask};
}

void BbgBoard::CpuSide::write(std::uint16_t address, std::uint8_t value) { board_.writeCpu(address, value); }

hw::BusRead BbgBoard::VideoSide::read(std::uint16_t address) { return hw::driveBus(board_.readVideo(address, true)); }

hw::BusRead BbgBoard::VideoSide::peek(std::uint16_t address) { return hw::driveBus(board_.readVideo(address, false)); }

void BbgBoard::VideoSide::write(std::uint16_t address, std::uint8_t value) { board_.writeVideo(address, value); }

hw::BusRead BbgBoard::PpuPort::read(std::uint16_t address) { return board_.ppuRegisters_.read(address); }

hw::BusRead BbgBoard::PpuPort::peek(std::uint16_t address) { return board_.ppuRegisters_.peek(address); }

void BbgBoard::PpuPort::write(std::uint16_t address, std::uint8_t value) {
  board_.ppuRegisters_.write(address, value);
  board_.shadowPpuWrite(address, value);
}

}  // namespace ember::bootstrap
