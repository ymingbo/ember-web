/*
 * ------------------------------------------------------------------------
 * 名称：sb97-board.cpp
 * 说明：小霸王 97 主板的实现：CPU 与 PPU 两侧的存储译码、整机寄存器。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/sb97/sb97-board.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;

namespace {

constexpr std::uint16_t REGISTER_PROGRAM_PAGE = 0x5000;
constexpr std::uint16_t REGISTER_CONTROL = 0x5200;
constexpr std::uint16_t REGISTER_SPEECH = 0x5300;
constexpr std::uint16_t FDC_WRITE_FIRST = 0x5500;
constexpr std::uint16_t FDC_WRITE_LAST = 0x5507;
constexpr std::uint16_t FDC_READ_FIRST = 0x5600;
constexpr std::uint16_t FDC_READ_LAST = 0x5607;
constexpr std::uint16_t WORK_RAM_FIRST = 0x6000;
constexpr std::uint16_t PROGRAM_WINDOW_FIRST = 0x8000;
constexpr std::uint16_t HIGH_HALF_FIRST = 0xC000;
constexpr std::size_t HALF_WINDOW_SIZE = 16 * 1024;
constexpr std::size_t FULL_WINDOW_SIZE = 32 * 1024;
/** $5000 的位 7：程序窗口用程序 RAM。 */
constexpr std::uint8_t PROGRAM_RAM_BIT = 0x80;
/** 软驱控制器的写端口：$5501 写到寄存器 2（DOR），$5500 写到寄存器 7（数据率），其余取低 3 位。 */
constexpr unsigned FDC_PORT_FROM_5501 = 2;
constexpr unsigned FDC_PORT_FROM_5500 = 7;

}  // namespace

Sb97Board::Sb97Board(std::vector<std::uint8_t> rom, hw::IrqLine& irq, logic::hardware::FdcUpd765& fdc, logic::hardware::SpeechLpc& speech)
    : irq_(irq),
      irqSource_(irq.addSource()),
      floppy_(fdc),
      speech_(speech),
      rom_(std::move(rom)),
      programRam_(PROGRAM_RAM_SIZE, 0),
      workRam_(WORK_RAM_SIZE, 0),
      characterRam_(CHARACTER_RAM_SIZE, 0) {
  if (rom_.size() != ROM_SIZE) {
    throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "sb97 的 BIOS 必须是 " + std::to_string(ROM_SIZE) + " 字节，收到 " + std::to_string(rom_.size()) + " 字节");
  }
  reset(true);
}

Sb97Board::Registers Sb97Board::registers() const {
  return Registers{programPage_, control_, programRamMode(), thirtyTwoKilobyteMode(), horizontalMirroring(), tableSwitch()};
}

void Sb97Board::reset(bool power) {
  if (power) {
    std::fill(programRam_.begin(), programRam_.end(), std::uint8_t{0});
    std::fill(workRam_.begin(), workRam_.end(), std::uint8_t{0});
    std::fill(characterRam_.begin(), characterRam_.end(), std::uint8_t{0});
    nametable_.fill(0);
  }
  programPage_ = 0;
  control_ = 0;
  irq_.set(irqSource_, false);
}

void Sb97Board::refreshFdcInterrupt() { irq_.set(irqSource_, floppy_.irqRaised()); }

bool Sb97Board::backgroundUsesTableOne(unsigned nametable) const {
  if (nametable == 2) return true;
  return nametable != 0 && tableSwitch();
}

// ---------------------------------------------------------------- 存储译码

Sb97Board::CpuLocation Sb97Board::locate(std::uint16_t address) const {
  using Kind = CpuLocation::Kind;
  if (address < 0x4020U) return {};
  if (address < WORK_RAM_FIRST) {
    if (address < 0x4100U) return {};
    if (address == REGISTER_PROGRAM_PAGE || address == REGISTER_CONTROL || address == REGISTER_SPEECH || (address >= FDC_WRITE_FIRST && address <= FDC_WRITE_LAST) ||
        (address >= FDC_READ_FIRST && address <= FDC_READ_LAST)) {
      return {Kind::Register, address};
    }
    return {};
  }
  if (address < PROGRAM_WINDOW_FIRST) return {Kind::WorkRam, static_cast<std::size_t>(address - WORK_RAM_FIRST)};

  const std::size_t window = static_cast<std::size_t>(address - PROGRAM_WINDOW_FIRST);
  if (thirtyTwoKilobyteMode()) {
    if (programRamMode()) return {Kind::ProgramRam, static_cast<std::size_t>(programPage_ & 0x03U) * FULL_WINDOW_SIZE + window};
    return {Kind::Rom, static_cast<std::size_t>(programPage_ & 0x1FU) * FULL_WINDOW_SIZE + window};
  }
  if (address >= HIGH_HALF_FIRST) return {Kind::Rom, static_cast<std::size_t>(address - HIGH_HALF_FIRST)};
  if (programRamMode()) return {Kind::ProgramRam, static_cast<std::size_t>(programPage_ & 0x07U) * HALF_WINDOW_SIZE + window};
  return {Kind::Rom, static_cast<std::size_t>(programPage_ & 0x3FU) * HALF_WINDOW_SIZE + window};
}

hw::BusRead Sb97Board::readCpu(std::uint16_t address, bool sideEffects) {
  if (address < 0x4100U) return hw::driveBus(0);
  const CpuLocation where = locate(address);
  switch (where.kind) {
    case CpuLocation::Kind::Rom:
      return hw::driveBus(rom_[where.offset]);
    case CpuLocation::Kind::ProgramRam:
      return hw::driveBus(programRam_[where.offset]);
    case CpuLocation::Kind::WorkRam:
      return hw::driveBus(workRam_[where.offset]);
    case CpuLocation::Kind::Register:
      if (address == REGISTER_SPEECH) return sideEffects ? speech_.read(address) : speech_.peek(address);
      if (address >= FDC_READ_FIRST && address <= FDC_READ_LAST) {
        const std::uint8_t value = floppy_.readRegister(address & 7U, sideEffects);
        if (sideEffects) refreshFdcInterrupt();
        return hw::driveBus(value);
      }
      // 只写的寄存器没有读通路
      return hw::releaseBus();
    case CpuLocation::Kind::None:
      break;
  }
  return hw::releaseBus();
}

void Sb97Board::writeCpu(std::uint16_t address, std::uint8_t value) {
  if (address < 0x4100U) return;
  const CpuLocation where = locate(address);
  switch (where.kind) {
    case CpuLocation::Kind::ProgramRam:
      programRam_[where.offset] = value;
      break;
    case CpuLocation::Kind::WorkRam:
      workRam_[where.offset] = value;
      break;
    case CpuLocation::Kind::Register:
      if (address == REGISTER_PROGRAM_PAGE) {
        programPage_ = value;
      } else if (address == REGISTER_CONTROL) {
        control_ = value;
      } else if (address == REGISTER_SPEECH) {
        speech_.write(address, value);
      } else if (address >= FDC_WRITE_FIRST && address <= FDC_WRITE_LAST) {
        const unsigned port = address == 0x5501U ? FDC_PORT_FROM_5501 : address == 0x5500U ? FDC_PORT_FROM_5500 : (address & 7U);
        floppy_.writeRegister(port, value);
        refreshFdcInterrupt();
      }
      break;
    case CpuLocation::Kind::Rom:
    case CpuLocation::Kind::None:
      break;
  }
}

// ---------------------------------------------------------------- PPU 侧

std::size_t Sb97Board::nametableOffset(std::uint16_t address) const {
  const unsigned slot = (address >> 10) & 3U;
  const unsigned page = horizontalMirroring() ? (slot >> 1) : (slot & 1U);
  return static_cast<std::size_t>(page) * 1024U + (address & 0x3FFU);
}

std::size_t Sb97Board::locateVideo(std::uint16_t address, bool& isPattern) const {
  const std::uint16_t masked = address & 0x3FFFU;
  isPattern = masked < 0x2000U;
  return isPattern ? static_cast<std::size_t>(masked) : nametableOffset(masked);
}

hw::BusRead Sb97Board::VideoSide::read(std::uint16_t address) {
  bool isPattern = false;
  const std::size_t offset = board_.locateVideo(address, isPattern);
  return hw::driveBus(isPattern ? board_.characterRam_[offset] : board_.nametable_[offset]);
}

hw::BusRead Sb97Board::VideoSide::peek(std::uint16_t address) { return read(address); }

void Sb97Board::VideoSide::write(std::uint16_t address, std::uint8_t value) {
  bool isPattern = false;
  const std::size_t offset = board_.locateVideo(address, isPattern);
  if (isPattern) board_.characterRam_[offset] = value;
  else board_.nametable_[offset] = value;
}

// ---------------------------------------------------------------- CPU 侧总线设备

hw::BusRead Sb97Board::CpuSide::read(std::uint16_t address) { return board_.readCpu(address, true); }

hw::BusRead Sb97Board::CpuSide::peek(std::uint16_t address) { return board_.readCpu(address, false); }

void Sb97Board::CpuSide::write(std::uint16_t address, std::uint8_t value) { board_.writeCpu(address, value); }

}  // namespace ember::bootstrap
