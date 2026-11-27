/*
 * ------------------------------------------------------------------------
 * 名称：pyramid-board.cpp
 * 说明：金字塔学习机主板的实现：CPU 与 PPU 两侧的存储译码、寄存器、程序窗口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/pyramid/pyramid-board.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;

namespace {

constexpr std::uint16_t REGISTER_FIRST = 0x4100;
constexpr std::uint16_t REGISTER_LAST = 0x5FFF;
constexpr std::uint16_t REGISTER_TAPE_OUTPUT = 0x5100;
constexpr std::uint16_t REGISTER_STATUS = 0x5500;
constexpr std::uint16_t REGISTER_TAPE_INPUT_586 = 0x5300;
/** 软驱控制器占 $5600-$56FF，低 4 位是寄存器号。 */
constexpr std::uint16_t DISK_FIRST = 0x5600;
constexpr std::uint16_t DISK_LAST = 0x56FF;
constexpr std::uint16_t DISK_REGISTER_MASK = 0x000F;
constexpr std::uint16_t WORK_RAM_FIRST = 0x6000;
constexpr std::uint16_t WINDOW_FIRST = 0x8000;
constexpr std::uint16_t PAGE_MASK = 0x1FFF;
/** $5500 读出的固定值，位 2 另外叠加磁带的输入线。 */
constexpr std::uint8_t STATUS_BASE = 0x10;
constexpr std::uint8_t STATUS_TAPE_BIT = 0x04;
/** $5100 写入的位 1 是打印机的选通线；打印机的数据在 $5200（reg2）；586 的打印机就绪读 $5300 的位 0。 */
constexpr std::uint8_t PRINTER_STROBE_BIT = 0x02;
constexpr std::size_t PRINTER_DATA_REGISTER = 2;
constexpr std::uint8_t PRINTER_READY_BIT_586 = 0x01;
/** $5100 写入里磁带输出的位：PEC-9588 是位 0，PEC-586 与 PEC-686F 是位 2。 */
constexpr std::uint8_t TAPE_OUTPUT_BIT_9588 = 0x01;
constexpr std::uint8_t TAPE_OUTPUT_BIT_586 = 0x04;
/** reg0 的位 7：汉字模式；位 4：586 的整块 32K 选择；位 4-6：窗口选择。 */
constexpr std::uint8_t TEXT_MODE_BIT = 0x80;
constexpr std::uint8_t WHOLE_BANK_BIT = 0x10;
constexpr std::uint8_t SELECT_MASK = 0x70;
/** 586 启动映像：从每个 16K 块末尾取的两段 1K 的块内偏移，以及第一个块（块 16，即 ROM 的后半）。 */
constexpr std::size_t BOOT_CHUNK_SIZE = 1024;
constexpr std::size_t BOOT_FIRST_BLOCK = 16;
constexpr std::size_t BOOT_BLOCK_COUNT = 16;
constexpr std::size_t BLOCK_SIZE = 16 * 1024;
constexpr std::size_t BOOT_CHUNK_A = 0x1C00;
constexpr std::size_t BOOT_CHUNK_B = 0x3C00;

}  // namespace

PyramidBoard::PyramidBoard(Model model, std::vector<std::uint8_t> rom, hw::TapeLine& tape, hw::ParallelPort* printer, logic::hardware::Disk2* disk2)
    : model_(model), tape_(tape), printer_(printer), disk2_(disk2), rom_(std::move(rom)), workRam_(WORK_RAM_SIZE, 0), characterRam_(CHARACTER_RAM_SIZE, 0) {
  if (rom_.size() != ROM_SIZE) {
    throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "金字塔学习机的 ROM 必须是 " + std::to_string(ROM_SIZE) + " 字节，收到 " + std::to_string(rom_.size()) + " 字节");
  }
  buildBootImage();
  reset(true);
}

void PyramidBoard::buildBootImage() {
  for (std::size_t index = 0; index < BOOT_BLOCK_COUNT; ++index) {
    const std::size_t blockStart = (BOOT_FIRST_BLOCK + index) * BLOCK_SIZE;
    std::copy_n(rom_.begin() + static_cast<std::ptrdiff_t>(blockStart + BOOT_CHUNK_A), BOOT_CHUNK_SIZE, bootImage_.begin() + static_cast<std::ptrdiff_t>(index * 2 * BOOT_CHUNK_SIZE));
    std::copy_n(rom_.begin() + static_cast<std::ptrdiff_t>(blockStart + BOOT_CHUNK_B), BOOT_CHUNK_SIZE, bootImage_.begin() + static_cast<std::ptrdiff_t>((index * 2 + 1) * BOOT_CHUNK_SIZE));
  }
}

void PyramidBoard::reset(bool power) {
  if (power) {
    std::fill(workRam_.begin(), workRam_.end(), std::uint8_t{0});
    std::fill(characterRam_.begin(), characterRam_.end(), std::uint8_t{0});
    nametable_.fill(0);
  }
  registers_.fill(0);
  text_.reset();
  if (model_ == Model::Pec9588) {
    setRomBlock(0, 0);
    setRomBlock(2, 31);
  } else {
    for (std::size_t window = 0; window < WINDOW_COUNT; ++window) setBootPage(window);
  }
}

// ---------------------------------------------------------------- 程序窗口

void PyramidBoard::setRomPage(std::size_t window, std::size_t page) { windows_[window] = WindowPage{false, (page % PAGE_COUNT) * ROM_PAGE_SIZE}; }

void PyramidBoard::setRomBlock(std::size_t first, std::size_t block) {
  setRomPage(first, block * 2);
  setRomPage(first + 1, block * 2 + 1);
}

void PyramidBoard::setBootPage(std::size_t window) { windows_[window] = WindowPage{true, window * ROM_PAGE_SIZE}; }

void PyramidBoard::applyRegisters() {
  const std::uint8_t control = registers_[0];
  text_.setTextMode((control & TEXT_MODE_BIT) != 0);
  if (model_ == Model::Pec9588) applyWindows9588();
  else applyWindows586();
}

void PyramidBoard::applyWindows9588() {
  const std::uint8_t control = registers_[0];
  const std::size_t low = control & 0x0FU;
  switch (control & SELECT_MASK) {
    case 0x00:
      setRomBlock(0, low);
      setRomBlock(2, 31);
      break;
    case 0x20:
      setRomBlock(0, low | 0x10U);
      setRomBlock(2, 31);
      break;
    case 0x40:
      setRomBlock(0, low);
      setRomBlock(2, 30);
      break;
    case 0x60:
      setRomBlock(0, low | 0x10U);
      setRomBlock(2, control >= 0x6C ? 28 : 30);
      break;
    default:
      break;
  }
}

void PyramidBoard::applyWindows586() {
  const std::uint8_t control = registers_[0];
  if ((control & WHOLE_BANK_BIT) != 0) {
    const std::size_t bank = control & 0x07U;
    for (std::size_t window = 0; window < WINDOW_COUNT; ++window) setRomPage(window, bank * WINDOW_COUNT + window);
    return;
  }
  for (std::size_t window = 0; window < WINDOW_COUNT; ++window) setBootPage(window);
  const std::size_t low = control & 0x0FU;
  if ((control & SELECT_MASK) == 0x40) setRomPage(0, low | 0x20U);
  else if ((control & SELECT_MASK) == 0x60) setRomPage(0, low | 0x30U);
}

// ---------------------------------------------------------------- 存储译码

PyramidBoard::CpuLocation PyramidBoard::locate(std::uint16_t address) const {
  using Kind = CpuLocation::Kind;
  if (address < REGISTER_FIRST) return {};
  if (address < WORK_RAM_FIRST) return address <= REGISTER_LAST ? CpuLocation{Kind::Register, address} : CpuLocation{};
  if (address < WINDOW_FIRST) return {Kind::WorkRam, static_cast<std::size_t>(address - WORK_RAM_FIRST)};
  const WindowPage& page = windows_[(address - WINDOW_FIRST) >> 13];
  const std::size_t offset = page.offset + (address & PAGE_MASK);
  return {page.bootImage ? Kind::BootImage : Kind::Rom, offset};
}

hw::BusRead PyramidBoard::readCpu(std::uint16_t address, bool sideEffects) {
  const CpuLocation where = locate(address);
  switch (where.kind) {
    case CpuLocation::Kind::Rom:
      return hw::driveBus(rom_[where.offset]);
    case CpuLocation::Kind::BootImage:
      return hw::driveBus(bootImage_[where.offset]);
    case CpuLocation::Kind::WorkRam:
      return hw::driveBus(workRam_[where.offset]);
    case CpuLocation::Kind::Register:
      if (disk2_ != nullptr && address >= DISK_FIRST && address <= DISK_LAST) {
        // 调试读不碰软驱控制器：读它会改变读写状态、取走半字节。
        if (!sideEffects) return hw::releaseBus();
        const std::optional<std::uint8_t> value = disk2_->read(address & DISK_REGISTER_MASK);
        return value.has_value() ? hw::driveBus(*value) : hw::releaseBus();
      }
      if (address == REGISTER_STATUS) {
        const bool tapeLevel = model_ == Model::Pec9588 && sideEffects && tape_.level();
        return hw::driveBus(static_cast<std::uint8_t>(STATUS_BASE | (tapeLevel ? STATUS_TAPE_BIT : 0)));
      }
      if (address == REGISTER_TAPE_INPUT_586 && model_ == Model::Pec586) {
        // 只驱动位 2（磁带输入）与位 0（打印机就绪），其余位是开放总线。
        const bool tapeLevel = sideEffects && tape_.level();
        const std::uint8_t level = static_cast<std::uint8_t>((tapeLevel ? STATUS_TAPE_BIT : 0) | (printerReady() ? PRINTER_READY_BIT_586 : 0));
        return hw::driveBus(level, static_cast<std::uint8_t>(STATUS_TAPE_BIT | PRINTER_READY_BIT_586));
      }
      return hw::releaseBus();
    case CpuLocation::Kind::None:
      break;
  }
  return hw::releaseBus();
}

void PyramidBoard::writeCpu(std::uint16_t address, std::uint8_t value) {
  const CpuLocation where = locate(address);
  switch (where.kind) {
    case CpuLocation::Kind::WorkRam:
      workRam_[where.offset] = value;
      break;
    case CpuLocation::Kind::Register: {
      const bool strobeWasHigh = (registers_[(REGISTER_TAPE_OUTPUT >> 8) & 7U] & PRINTER_STROBE_BIT) != 0;
      registers_[(address >> 8) & 7U] = value;
      if (disk2_ != nullptr && address >= DISK_FIRST && address <= DISK_LAST) disk2_->write(address & DISK_REGISTER_MASK, value);
      if (address == REGISTER_TAPE_OUTPUT) {
        tape_.drive((value & (model_ == Model::Pec9588 ? TAPE_OUTPUT_BIT_9588 : TAPE_OUTPUT_BIT_586)) != 0);
        // 选通线从低变高时，打印机取走 $5200 里的数据。
        if (printer_ != nullptr && !strobeWasHigh && (value & PRINTER_STROBE_BIT) != 0) printer_->writeData(registers_[PRINTER_DATA_REGISTER]);
      }
      applyRegisters();
      break;
    }
    case CpuLocation::Kind::Rom:
    case CpuLocation::Kind::BootImage:
    case CpuLocation::Kind::None:
      break;
  }
}

// ---------------------------------------------------------------- PPU 侧

std::size_t PyramidBoard::nametableOffset(std::uint16_t address) const {
  const unsigned slot = (address >> 10) & 3U;
  const unsigned page = horizontalMirroring() ? (slot >> 1) : (slot & 1U);
  return static_cast<std::size_t>(page) * 1024U + (address & 0x3FFU);
}

std::size_t PyramidBoard::locateVideo(std::uint16_t address, bool& isPattern) const {
  const std::uint16_t masked = address & 0x3FFFU;
  isPattern = masked < 0x2000U;
  return isPattern ? static_cast<std::size_t>(masked) : nametableOffset(masked);
}

hw::BusRead PyramidBoard::VideoSide::read(std::uint16_t address) {
  const std::uint16_t masked = address & 0x3FFFU;
  board_.text_.observe(masked);
  bool isPattern = false;
  const std::size_t offset = board_.locateVideo(board_.text_.remap(masked), isPattern);
  return hw::driveBus(isPattern ? board_.characterRam_[offset] : board_.nametable_[offset]);
}

hw::BusRead PyramidBoard::VideoSide::peek(std::uint16_t address) {
  bool isPattern = false;
  const std::size_t offset = board_.locateVideo(address, isPattern);
  return hw::driveBus(isPattern ? board_.characterRam_[offset] : board_.nametable_[offset]);
}

void PyramidBoard::VideoSide::write(std::uint16_t address, std::uint8_t value) {
  bool isPattern = false;
  const std::size_t offset = board_.locateVideo(address, isPattern);
  if (isPattern) board_.characterRam_[offset] = value;
  else board_.nametable_[offset] = value;
}

// ---------------------------------------------------------------- CPU 侧总线设备

hw::BusRead PyramidBoard::CpuSide::read(std::uint16_t address) { return board_.readCpu(address, true); }

hw::BusRead PyramidBoard::CpuSide::peek(std::uint16_t address) { return board_.readCpu(address, false); }

void PyramidBoard::CpuSide::write(std::uint16_t address, std::uint8_t value) { board_.writeCpu(address, value); }

}  // namespace ember::bootstrap
