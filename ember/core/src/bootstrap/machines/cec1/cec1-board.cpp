/*
 * ------------------------------------------------------------------------
 * 名称：cec1-board.cpp
 * 说明：中华学习机 CEC-I 主板的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/cec1/cec1-board.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "bootstrap/machines/cec1/cec1-font.hpp"
#include "contracts/hardware/button-input.hpp"
#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;

namespace {

constexpr std::uint16_t IO_FIRST = 0xC000;
constexpr std::uint16_t SLOT_ROM_FIRST = 0xC100;
constexpr std::uint16_t HIGH_FIRST = 0xD000;
constexpr std::uint16_t SLOT_THREE_FIRST = 0xC300;
constexpr std::uint16_t SLOT_THREE_LAST = 0xC3FF;
constexpr std::uint16_t WINDOW_FIRST = 0x8000;
constexpr std::uint16_t EXTENSION_FIRST = 0x4000;
/** $C0B0 的位 5：辅存读开关为 1 时 $8000-$BFFF 读 LOGO（1）还是汉字表（0）。 */
constexpr std::uint8_t AUX_SELECT_LOGO = 0x20;
/** $C0B0 的位 4：允许彩色（1）还是单色（0）。 */
constexpr std::uint8_t AUX_COLOR_ENABLE = 0x10;
/** $C0B0 的低 4 位：汉字字库的页（bank）号。 */
constexpr std::uint8_t LIBRARY_BANK_MASK = 0x0F;
/** 打印机接口卡（槽 1）：数据寄存器在 $C090，状态在 $C1C0-$C1C7。 */
constexpr std::uint8_t PRINTER_DATA_REGISTER = 0x90;
/** Ember 的扩展：写到这里的是文本，由文本适配排成位图行（虚拟打印机只会画位图）。 */
constexpr std::uint8_t PRINTER_TEXT_REGISTER = 0x91;
constexpr std::uint16_t PRINTER_STATUS_FIRST = 0xC1C0;
constexpr std::uint16_t PRINTER_STATUS_LAST = 0xC1C7;
constexpr std::uint8_t PRINTER_STATUS_ACK = 0;
constexpr std::uint8_t PRINTER_STATUS_BUSY = 4;
/**
 * 打印机接口卡的驱动程序（槽 1 的 $C100 起，PR#1 后输出的字符交给它）：去掉高位、
 * 等打印机不忙后写 $C091（文本口，见 PRINTER_TEXT_REGISTER），
 * 遇到回车再追加一个换行，返回时 A 不变。原卡的驱动 ROM 没有收集到，这是按接口卡的行为重写的。
 */
constexpr std::array<std::uint8_t, 26> PRINTER_DRIVER = {
    0x48,              // C100 PHA
    0x29, 0x7F,        // C101 AND #$7F
    0x20, 0x11, 0xC1,  // C103 JSR $C111
    0xC9, 0x0D,        // C106 CMP #$0D
    0xD0, 0x05,        // C108 BNE $C10F
    0xA9, 0x0A,        // C10A LDA #$0A
    0x20, 0x11, 0xC1,  // C10C JSR $C111
    0x68,              // C10F PLA
    0x60,              // C110 RTS
    0x2C, 0xC4, 0xC1,  // C111 BIT $C1C4
    0x30, 0xFB,        // C114 BMI $C111
    0x8D, 0x91, 0xC0,  // C116 STA $C091
    0x60,              // C119 RTS
};
/** 内置串口（槽 2）的状态：位 1 为发送寄存器空。 */
constexpr std::uint8_t SERIAL_TRANSMIT_READY = 0x02;
/** 模拟手柄的计时：每个计数占的 CPU 周期数，加上计时开始的固定延迟。 */
constexpr std::int64_t PADDLE_CYCLES_PER_COUNT = 11;
constexpr std::int64_t PADDLE_BASE_CYCLES = 11;
/** 手柄的方向换成模拟手柄的值。 */
constexpr std::uint8_t PADDLE_LOW = 0;
constexpr std::uint8_t PADDLE_CENTER = 128;
constexpr std::uint8_t PADDLE_HIGH = 255;

/** $C0xx 里各个寄存器组的起点（高 4 位）。 */
constexpr std::uint8_t GROUP_KEYBOARD = 0x0;
constexpr std::uint8_t GROUP_STROBE = 0x1;
constexpr std::uint8_t GROUP_CASSETTE_OUT = 0x2;
constexpr std::uint8_t GROUP_SPEAKER = 0x3;
constexpr std::uint8_t GROUP_VIDEO = 0x5;
constexpr std::uint8_t GROUP_INPUT = 0x6;
constexpr std::uint8_t GROUP_PADDLE_TRIGGER = 0x7;
constexpr std::uint8_t GROUP_LANGUAGE_CARD = 0x8;
constexpr std::uint8_t GROUP_PRINTER = 0x9;
constexpr std::uint8_t GROUP_SERIAL = 0xA;
constexpr std::uint8_t GROUP_AUX = 0xB;
constexpr std::uint8_t GROUP_DISK = 0xE;

/** 把一个字节的位序翻转（位 0 与位 7 互换，依次类推）。 */
std::uint8_t reverseBits(std::uint8_t value) {
  std::uint8_t result = 0;
  for (int bit = 0; bit < 8; ++bit) {
    result = static_cast<std::uint8_t>((result << 1) | ((value >> bit) & 1U));
  }
  return result;
}

}  // namespace

Cec1Board::Cec1Board(std::vector<std::uint8_t> rom, const hw::Clock& clock, logic::hardware::KeyboardApple2& keyboard, logic::hardware::Disk2& disk, logic::hardware::Speaker1Bit& speaker, hw::TapeLine& tape,
                     hw::ParallelPort& parallel, hw::MasterTime cpuTicksPerCycle)
    : clock_(clock),
      keyboard_(keyboard),
      disk_(disk),
      speaker_(speaker),
      tape_(tape),
      parallel_(parallel),
      text_(parallel),
      cpuTicksPerCycle_(cpuTicksPerCycle),
      rom_(std::move(rom)),
      ram_(RAM_SIZE, 0),
      languageCard_(LANGUAGE_CARD_SIZE, 0) {
  if (rom_.size() != ROM_SIZE && rom_.size() != ROM_SIZE + LIBRARY_SIZE) {
    throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "cec1 的 ROM 必须是 " + std::to_string(ROM_SIZE) + " 或 " + std::to_string(ROM_SIZE + LIBRARY_SIZE) + " 字节，收到 " + std::to_string(rom_.size()) + " 字节");
  }
}

void Cec1Board::reset(bool power) {
  if (power) {
    std::fill(ram_.begin(), ram_.end(), std::uint8_t{0});
    std::fill(languageCard_.begin(), languageCard_.end(), std::uint8_t{0});
  }
  switches_ = Switches{};
  tapeOutput_ = false;
  paddleTrigger_ = -1'000'000'000;
  if (power) {
    parallel_.reset();
    text_.clear();
  }
}

// ---------------------------------------------------------------- 译码

Cec1Board::Location Cec1Board::locateRead(std::uint16_t address) const {
  using Kind = Location::Kind;
  if (address < EXTENSION_FIRST) return {Kind::Ram, address};
  if (address < WINDOW_FIRST) {
    if (!switches_.ramRead) return {Kind::Ram, address};
    // 汉字字库：接了 U33、U34 时按 $C0B0 的低 4 位选页读芯片，没有接时读内置字库。
    if (rom_.size() == ROM_SIZE) return {Kind::Font, builtinFontOffset(address)};
    return {Kind::Rom, libraryOffset(address)};
  }
  if (address < IO_FIRST) {
    if (!switches_.ramRead) return {Kind::Ram, address};
    const std::size_t base = (switches_.aux & AUX_SELECT_LOGO) != 0 ? U7_LOW : U35_LOW;
    return {Kind::Rom, base + (address - WINDOW_FIRST)};
  }
  if (address < SLOT_ROM_FIRST) return {Kind::Register, static_cast<std::size_t>(address - IO_FIRST)};
  if (address < HIGH_FIRST) {
    // 打印机接口卡的状态寄存器在槽 1 的 ROM 空间里，内部 ROM 开关为 1 时读不到。
    if (!switches_.internalRom && address >= PRINTER_STATUS_FIRST && address <= PRINTER_STATUS_LAST) return {Kind::Register, static_cast<std::size_t>(address - IO_FIRST)};
    if (!switches_.internalRom && address >= SLOT_ROM_FIRST && address < SLOT_ROM_FIRST + PRINTER_DRIVER.size()) return {Kind::PrinterRom, static_cast<std::size_t>(address - SLOT_ROM_FIRST)};
    const bool internal = switches_.internalRom || (address >= SLOT_THREE_FIRST && address <= SLOT_THREE_LAST && !switches_.slotThreeRom);
    return {Kind::Rom, (internal ? U7_HIGH : U35_HIGH) + (address - IO_FIRST)};
  }
  // 语言卡读 RAM 时，辅助零页开关把这段空间切到辅存（汉字程序 ROM）；读 ROM 时不起作用。
  if (switches_.lcReadRam) {
    if (switches_.altZeroPage) return {Kind::Rom, U35_HIGH + (address - IO_FIRST)};
    return {Kind::LanguageCard, languageCardOffset(address)};
  }
  return {Kind::Rom, U7_HIGH + (address - IO_FIRST)};
}

std::size_t Cec1Board::libraryOffset(std::uint16_t address) const {
  // 每个字模 32 字节由两片芯片交替给出：偏移除 2 是芯片地址，低位选 U33（偶）或 U34（奇）。
  const std::size_t inBank = static_cast<std::size_t>(switches_.aux & LIBRARY_BANK_MASK) * LIBRARY_BANK_SIZE + (address - EXTENSION_FIRST);
  return ROM_SIZE + (inBank & 1U) * LIBRARY_CHIP_SIZE + (inBank >> 1);
}

std::size_t Cec1Board::builtinFontOffset(std::uint16_t address) const {
  return static_cast<std::size_t>(switches_.aux & LIBRARY_BANK_MASK) * LIBRARY_BANK_SIZE + (address - EXTENSION_FIRST);
}

std::uint8_t Cec1Board::printerStatus(std::uint8_t index) const {
  // 状态经一片 8 选 1 的多路器送到数据线位 7：Ack 经反相、忙不反相。
  if (index == PRINTER_STATUS_ACK) return parallel_.ack() ? 0x00 : 0x80;
  if (index == PRINTER_STATUS_BUSY) return parallel_.busyN() ? 0x00 : 0x80;
  return 0x00;
}

std::size_t Cec1Board::languageCardOffset(std::uint16_t address) const {
  if (address < 0xE000) return (switches_.lcBank2 ? 0x1000U : 0U) + (address - HIGH_FIRST);
  return 0x2000U + (address - 0xE000U);
}

// ---------------------------------------------------------------- 总线

hw::BusRead Cec1Board::read(std::uint16_t address) {
  const Location where = locateRead(address);
  switch (where.kind) {
    case Location::Kind::Ram:
      return hw::driveBus(ram_[where.offset]);
    case Location::Kind::LanguageCard:
      return hw::driveBus(languageCard_[where.offset]);
    case Location::Kind::Rom:
      // 字库芯片里的字节与 CPU 读到的位序相反。
      return hw::driveBus(where.offset >= ROM_SIZE ? reverseBits(rom_[where.offset]) : rom_[where.offset]);
    case Location::Kind::Register:
      if (where.offset >= 0x100) return hw::driveBus(printerStatus(static_cast<std::uint8_t>(where.offset & 0x0FU)));
      return readIo(static_cast<std::uint8_t>(where.offset), true);
    case Location::Kind::Font:
      return hw::driveBus(cec1_font::HANZI_FONT[where.offset]);
    case Location::Kind::PrinterRom:
      return hw::driveBus(PRINTER_DRIVER[where.offset]);
    case Location::Kind::None:
      break;
  }
  return hw::releaseBus();
}

hw::BusRead Cec1Board::peek(std::uint16_t address) {
  const Location where = locateRead(address);
  if (where.kind == Location::Kind::Register && where.offset < 0x100) return readIo(static_cast<std::uint8_t>(where.offset), false);
  return read(address);
}

void Cec1Board::write(std::uint16_t address, std::uint8_t value) {
  if (address < IO_FIRST) {
    // 辅存写开关为 1 时，$4000-$BFFF 的写进了辅存里的 ROM，丢弃。
    if (switches_.ramWrite && address >= EXTENSION_FIRST) return;
    ram_[address] = value;
    return;
  }
  if (address < SLOT_ROM_FIRST) {
    writeIo(static_cast<std::uint8_t>(address - IO_FIRST), value);
    return;
  }
  if (address < HIGH_FIRST) return;
  // 辅助零页开关为 1 时这段空间是辅存里的 ROM，写不进去。
  if (switches_.lcWriteRam && !switches_.altZeroPage) languageCard_[languageCardOffset(address)] = value;
}

// ---------------------------------------------------------------- $C000-$C0FF

bool Cec1Board::statusFlag(std::uint8_t reg) const {
  switch (reg & 0x0FU) {
    case 0x1:
      return switches_.lcBank2;
    case 0x2:
      return switches_.lcReadRam;
    case 0x3:
      return switches_.ramRead;
    case 0x4:
      return switches_.ramWrite;
    case 0x5:
      return switches_.internalRom;
    case 0x6:
      return switches_.altZeroPage;
    case 0x7:
      return switches_.slotThreeRom;
    case 0x8:
      return switches_.store80;
    case 0x9:
      return video_ != nullptr && !video_->inVerticalBlank();
    case 0xA:
      return switches_.text;
    case 0xB:
      return switches_.mixed;
    case 0xC:
      return switches_.page2;
    case 0xD:
      return switches_.hires;
    case 0xE:
      return switches_.altCharset;
    case 0xF:
      return switches_.column80;
    default:
      return false;
  }
}

bool Cec1Board::paddleTiming(unsigned paddle) const {
  const hw::MasterTime limit = (PADDLE_BASE_CYCLES + PADDLE_CYCLES_PER_COUNT * paddleValues_[paddle]) * cpuTicksPerCycle_;
  return clock_.now() - paddleTrigger_ < limit;
}

hw::BusRead Cec1Board::readIo(std::uint8_t reg, bool side) {
  const std::uint8_t group = reg >> 4;
  switch (group) {
    case GROUP_KEYBOARD:
      return hw::driveBus(keyboard_.data());
    case GROUP_STROBE: {
      const std::uint8_t key = keyboard_.data() & 0x7FU;
      if (reg == 0x10) {
        const bool anyKey = keyboard_.anyKeyDown();
        if (side) keyboard_.clearStrobe();
        return hw::driveBus(static_cast<std::uint8_t>(key | (anyKey ? 0x80U : 0U)));
      }
      return hw::driveBus(static_cast<std::uint8_t>(key | (statusFlag(reg) ? 0x80U : 0U)));
    }
    case GROUP_CASSETTE_OUT:
      if (side) {
        tapeOutput_ = !tapeOutput_;
        tape_.drive(tapeOutput_);
      }
      return hw::releaseBus();
    case GROUP_SPEAKER:
      if (side) speaker_.toggle(clock_.now());
      return hw::releaseBus();
    case GROUP_VIDEO:
      if (side) accessVideoSwitch(reg);
      return hw::releaseBus();
    case GROUP_INPUT: {
      switch (reg & 0x07U) {
        case 0x0:
          return hw::driveBus(side && tape_.level() ? 0x80 : 0x00);
        case 0x1:
          return hw::driveBus((button0_ || keyboard_.openApple()) ? 0x80 : 0x00);
        case 0x2:
          return hw::driveBus((button1_ || keyboard_.solidApple()) ? 0x80 : 0x00);
        case 0x3:
          return hw::driveBus((button2_ || keyboard_.westernKeyDown()) ? 0x80 : 0x00);
        default:
          return hw::driveBus(paddleTiming((reg & 0x07U) - 4U) ? 0x80 : 0x00);
      }
    }
    case GROUP_PADDLE_TRIGGER:
      if (side) paddleTrigger_ = clock_.now();
      return hw::releaseBus();
    case GROUP_LANGUAGE_CARD:
      if (side) accessLanguageCard(reg);
      return hw::releaseBus();
    case GROUP_SERIAL:
      if (reg == 0xAE) return hw::driveBus(SERIAL_TRANSMIT_READY);
      if (reg == 0xAF) return hw::driveBus(0);
      return hw::releaseBus();
    case GROUP_DISK: {
      if (!side) return hw::releaseBus();
      const std::optional<std::uint8_t> value = disk_.read(reg & 0x0FU);
      return value.has_value() ? hw::driveBus(*value) : hw::releaseBus();
    }
    default:
      return hw::releaseBus();
  }
}

void Cec1Board::writeIo(std::uint8_t reg, std::uint8_t value) {
  const std::uint8_t group = reg >> 4;
  switch (group) {
    case GROUP_KEYBOARD:
      writeSwitch(reg);
      break;
    case GROUP_STROBE:
      if (reg == 0x10) keyboard_.clearStrobe();
      break;
    case GROUP_CASSETTE_OUT:
      tapeOutput_ = !tapeOutput_;
      tape_.drive(tapeOutput_);
      break;
    case GROUP_SPEAKER:
      speaker_.toggle(clock_.now());
      break;
    case GROUP_VIDEO:
      accessVideoSwitch(reg);
      break;
    case GROUP_PADDLE_TRIGGER:
      paddleTrigger_ = clock_.now();
      break;
    case GROUP_LANGUAGE_CARD:
      accessLanguageCard(reg);
      break;
    case GROUP_AUX:
      if (reg == 0xB0) switches_.aux = value;
      break;
    case GROUP_PRINTER:
      // 打印机接口卡的数据寄存器：写入的同时产生选通脉冲。
      if (reg == PRINTER_DATA_REGISTER) parallel_.writeData(value);
      else if (reg == PRINTER_TEXT_REGISTER) text_.write(value);
      break;
    case GROUP_DISK:
      disk_.write(reg & 0x0FU, value);
      break;
    default:
      break;
  }
}

void Cec1Board::writeSwitch(std::uint8_t reg) {
  const bool on = (reg & 1U) != 0;
  switch ((reg & 0x0FU) >> 1) {
    case 0:
      switches_.store80 = on;
      break;
    case 1:
      switches_.ramRead = on;
      break;
    case 2:
      switches_.ramWrite = on;
      break;
    case 3:
      switches_.internalRom = on;
      break;
    case 4:
      switches_.altZeroPage = on;
      break;
    case 5:
      switches_.slotThreeRom = on;
      break;
    case 6:
      switches_.column80 = on;
      break;
    default:
      switches_.altCharset = on;
      break;
  }
}

void Cec1Board::accessVideoSwitch(std::uint8_t reg) {
  const bool on = (reg & 1U) != 0;
  switch ((reg & 0x0FU) >> 1) {
    case 0:
      switches_.text = on;
      break;
    case 1:
      switches_.mixed = on;
      break;
    case 2:
      switches_.page2 = on;
      break;
    case 3:
      switches_.hires = on;
      break;
    default:
      switches_.annunciators[((reg & 0x0FU) >> 1) - 4U] = on;
      break;
  }
}

void Cec1Board::accessLanguageCard(std::uint8_t reg) {
  const bool odd = (reg & 1U) != 0;
  switches_.lcBank2 = (reg & 0x08U) == 0;
  // A1A0 为 00 与 11 读 RAM，01 与 10 读 ROM。
  const unsigned mode = reg & 3U;
  switches_.lcReadRam = mode == 0 || mode == 3;
  // 写允许就是地址位 0：奇数地址允许写 RAM，偶数地址禁止。
  switches_.lcWriteRam = odd;
}

// ---------------------------------------------------------------- 视频与手柄

logic::hardware::Apple2VideoMode Cec1Board::videoMode() const {
  logic::hardware::Apple2VideoMode mode;
  mode.text = switches_.text;
  mode.mixed = switches_.mixed;
  mode.page2 = switches_.page2;
  mode.hires = switches_.hires;
  mode.colorEnabled = (switches_.aux & AUX_COLOR_ENABLE) != 0;
  return mode;
}

void Cec1Board::setPaddles(std::uint8_t buttons) {
  button0_ = (buttons & hw::pad_button::A) != 0;
  button1_ = (buttons & hw::pad_button::B) != 0;
  button2_ = (buttons & (hw::pad_button::SELECT | hw::pad_button::START)) != 0;
  const auto axis = [buttons](std::uint8_t low, std::uint8_t high) { return (buttons & low) != 0 ? PADDLE_LOW : ((buttons & high) != 0 ? PADDLE_HIGH : PADDLE_CENTER); };
  paddleValues_[0] = axis(hw::pad_button::LEFT, hw::pad_button::RIGHT);
  paddleValues_[1] = axis(hw::pad_button::UP, hw::pad_button::DOWN);
}

}  // namespace ember::bootstrap
