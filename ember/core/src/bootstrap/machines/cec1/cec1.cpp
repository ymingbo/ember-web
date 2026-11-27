/*
 * ------------------------------------------------------------------------
 * 名称：cec1.cpp
 * 说明：主机型号 cec1（中华学习机 CEC-I）的装配、电源、运行、输入与调试接口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：整个 CPU 地址空间由主板译码（见 cec1-board.hpp）；没有 PPU、APU 与中断源，
 *       画面由视频生成器按帧画出，声音只有一位扬声器与磁带。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/cec1/cec1.hpp"

#include <algorithm>
#include <string>
#include <variant>

#include "bootstrap/machines/cec1/cec1-font.hpp"
#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;

namespace {

using debugging::checkRange;
using debugging::field;
using debugging::unknownSpace;

constexpr std::size_t CPU_SPACE_SIZE = 0x10000;

}  // namespace

// ---------------------------------------------------------------- 装配

Cec1Machine::Cec1Machine(std::vector<std::uint8_t> rom, std::shared_ptr<logic::hardware::TapeDeck> tapeDeck, hw::FloppyActivity* floppyActivity, std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive,
                         hw::PrinterOutput* printerOutput)
    : cartridgeIrq_(irq_),
      floppy_(floppyDrive != nullptr ? std::move(floppyDrive) : std::make_shared<logic::hardware::StandardFloppyDrive>()),
      tapeDeck_(tapeDeck != nullptr ? std::move(tapeDeck) : std::make_shared<logic::hardware::TapeDeck>()),
      speaker_(MASTER_CLOCK_HZ, logic::hardware::Apu2a03::SAMPLE_RATE),
      printer_(printerOutput != nullptr ? std::make_unique<logic::hardware::PrinterEscp>(scheduler_, *printerOutput, PRINTER_TIMEOUT_TICKS) : nullptr),
      parallel_(printer_ != nullptr ? static_cast<hw::ParallelPort&>(*printer_) : static_cast<hw::ParallelPort&>(nullParallel_)),
      disk_(scheduler_, DISK_BIT_TICKS, DISK_MOTOR_OFF_TICKS, logic::hardware::disk2::appleFormatOf),
      board_(std::move(rom), scheduler_, keyboard_, disk_, speaker_, *tapeDeck_, parallel_, CPU_TICKS_PER_CYCLE),
      video_(scheduler_, board_, CPU_TICKS_PER_CYCLE * CYCLES_PER_LINE),
      cpu_(cpuBus_, scheduler_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, irq_, nmi_, rdy_),
      debug_(cpuBus_, cpu_) {
  // 完整的 6502：ADC 与 SBC 支持十进制模式。
  cpu_.setDecimalMode(true);
  board_.setVideo(video_);
  debug_.setVideoPosition([this] { return hw::VideoPosition{video_.frameCount(), video_.scanline(), 0}; });
  cpuBus_.mount(0x0000, 0xFFFF, board_);
  disk_.attachDrive(floppy_.get());
  floppy_->attach(this, floppyActivity == nullptr ? std::function<void(bool)>() : [floppyActivity](bool on) { floppyActivity->motorChanged(0, on); });
}

Cec1Machine::~Cec1Machine() {
  // 软驱与磁带机在主机之外继续存在：只解除自己的接入，盘片与磁带留在里面。
  floppy_->detach(this);
  try {
    tapeDeck_->unbind(scheduler_);
  } catch (...) {
    // 析构时无法再报告，录音文件保留已写入的部分。
  }
}

void Cec1Machine::powerOn() {
  // 磁带机重新接到本机的调度器（先停止，录音在这里写完），它的时间基准随时钟一起归零。
  tapeDeck_->bind(scheduler_, MASTER_CLOCK_HZ);
  tapeDeck_->setAudioRate(logic::hardware::Apu2a03::SAMPLE_RATE);
  // 时钟先归零并丢弃旧事件，各设备复位时再登记自己的事件；CPU 最后复位，复位序列会推进时钟。
  scheduler_.reset();
  keyboard_.powerOn();
  speaker_.reset();
  board_.reset(true);
  disk_.reset(true);
  video_.reset(true);
  poweredOn_ = true;
  debug_.clearStopState();
  cpu_.reset(true);
  debug_.discardPendingHit();
}

void Cec1Machine::powerOff() {
  poweredOn_ = false;
  video_.halt();
  // 断电时磁带机停止并断开，磁带留在里面；软驱马达停转。
  tapeDeck_->unbind(scheduler_);
  disk_.reset(true);
}

void Cec1Machine::softReset() {
  if (!poweredOn_) return;
  board_.reset(false);
  disk_.reset(false);
  video_.reset(false);
  cpu_.reset(false);
  debug_.clearStopState();
}

void Cec1Machine::run(hw::MasterTime ticks) {
  if (!poweredOn_ || ticks <= 0) return;
  const hw::MasterTime cycles = (ticks + CPU_TICKS_PER_CYCLE - 1) / CPU_TICKS_PER_CYCLE;
  cpu_.run(static_cast<std::uint32_t>(cycles));
}

void Cec1Machine::runFrame() {
  if (!poweredOn_) return;
  const std::uint64_t target = video_.frameCount() + 1;
  while (video_.frameCount() < target) {
    if (debug_.stopAtExecuteBreakpoint()) return;
    debug_.beforeInstruction();
    cpu_.run(1);
    if (debug_.hasPendingHit()) return;
  }
}

void Cec1Machine::takeAudio(std::vector<float>& samples) {
  speaker_.take(samples, scheduler_.now());
  tapeDeck_->mixAudio(samples);
}

void Cec1Machine::setButtons(unsigned port, std::uint8_t buttons) {
  if (port >= 1) throw shared::CoreError(hw::ERROR_PORT_NOT_FOUND, "中华学习机只有 1 个手柄口，编号为 0");
  board_.setPaddles(buttons);
}

// ---------------------------------------------------------------- 输入

void Cec1Machine::input(const hw::InputEvent& event) {
  // 键位表里没有的键与鼠标事件丢弃。
  if (const auto* key = std::get_if<hw::KeyInput>(&event)) keyboard_.setKey(key->code, key->down);
}

// ---------------------------------------------------------------- 调试接口

std::vector<hw::MemorySpace> Cec1Machine::memorySpaces() const {
  return {
      {"cpu", "CPU 总线 $0000-$FFFF：读不产生副作用，软开关与 I/O 读出 0；写经真实的总线，与 CPU 写一样有副作用", CPU_SPACE_SIZE, true},
      {"board.ram", "主存（64K）", Cec1Board::RAM_SIZE, true},
      {"board.languageCard", "语言卡存储（16K：bank 1 的 4K、bank 2 的 4K 与 $E000-$FFFF 的 8K）", Cec1Board::LANGUAGE_CARD_SIZE, true},
      {"board.rom", "ROM 文件原样：U7（32K）、U35（32K），接了汉字字库时后面是 U33（128K）与 U34（128K），只读", board_.rom().size(), false},
      {"board.font", "内置汉字字库（256K，只读，没有接 U33、U34 时汉字程序读这份）", cec1_font::FONT_SIZE, false},
  };
}

void Cec1Machine::readMemory(std::string_view space, std::uint32_t address, std::span<std::uint8_t> out) {
  const auto copyFrom = [&](const std::vector<std::uint8_t>& bytes) {
    checkRange(space, bytes.size(), address, out.size());
    std::copy_n(bytes.begin() + address, out.size(), out.begin());
  };
  if (space == "cpu") {
    checkRange(space, CPU_SPACE_SIZE, address, out.size());
    for (std::size_t index = 0; index < out.size(); ++index) out[index] = cpuBus_.peek(static_cast<std::uint16_t>(address + index));
  } else if (space == "board.ram") {
    copyFrom(board_.ram());
  } else if (space == "board.languageCard") {
    copyFrom(board_.languageCard());
  } else if (space == "board.rom") {
    copyFrom(board_.rom());
  } else if (space == "board.font") {
    checkRange(space, cec1_font::FONT_SIZE, address, out.size());
    std::copy_n(cec1_font::HANZI_FONT.begin() + address, out.size(), out.begin());
  } else {
    unknownSpace(space);
  }
}

void Cec1Machine::writeMemory(std::string_view space, std::uint32_t address, std::span<const std::uint8_t> bytes) {
  // 调试写入不触发读写断点，也不进记录。
  const CpuDebugKit::Suspend suspend(debug_, true);
  const auto copyTo = [&](std::vector<std::uint8_t>& target) {
    checkRange(space, target.size(), address, bytes.size());
    std::copy(bytes.begin(), bytes.end(), target.begin() + address);
  };
  if (space == "cpu") {
    checkRange(space, CPU_SPACE_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) cpuBus_.write(static_cast<std::uint16_t>(address + index), bytes[index]);
  } else if (space == "board.ram") {
    copyTo(board_.ram());
  } else if (space == "board.languageCard") {
    copyTo(board_.languageCard());
  } else if (space == "board.rom") {
    throw shared::CoreError(hw::ERROR_SPACE_READ_ONLY, "空间 board.rom 是只读的");
  } else if (space == "board.font") {
    throw shared::CoreError(hw::ERROR_SPACE_READ_ONLY, "空间 board.font 是只读的");
  } else {
    unknownSpace(space);
  }
}

hw::AddressTarget Cec1Machine::resolve(std::string_view space, std::uint32_t address) const {
  if (space != "cpu") unknownSpace(space);
  checkRange(space, CPU_SPACE_SIZE, address, 1);
  const Cec1Board::Location where = board_.locateRead(static_cast<std::uint16_t>(address));
  switch (where.kind) {
    case Cec1Board::Location::Kind::Ram:
      return {"ram", "board.ram", where.offset, "主存"};
    case Cec1Board::Location::Kind::LanguageCard:
      return {"ram", "board.languageCard", where.offset, "语言卡存储"};
    case Cec1Board::Location::Kind::Rom:
      return {"rom", "board.rom", where.offset, "ROM（U7 低 16K 是 LOGO、高 16K 是监控与 CEC-BASIC，U35 低 16K 是汉字表、高 16K 是汉字程序与槽 ROM，U33 与 U34 是汉字字库）"};
    case Cec1Board::Location::Kind::Register:
      return {"register", "board.registers", where.offset, "软开关与 I/O（$C000-$C0FF 与打印机接口卡的状态寄存器）"};
    case Cec1Board::Location::Kind::Font:
      return {"rom", "board.font", where.offset, "内置汉字字库（没有接 U33、U34 时）"};
    case Cec1Board::Location::Kind::PrinterRom:
      return {"none", "", 0, "打印机接口卡的驱动程序（槽 1）"};
    case Cec1Board::Location::Kind::None:
      break;
  }
  return {"none", "", 0, "没有设备，读到总线上最近的值"};
}

std::vector<std::string> Cec1Machine::stateModules() const { return {"cpu", "board", "disk"}; }

std::vector<hw::StateField> Cec1Machine::readState(std::string_view module) const {
  if (module == "cpu") return debug_.readCpuState();
  if (module == "board") {
    const Cec1Board::Switches& state = board_.switches();
    return {field("text", state.text ? 1 : 0, "文本模式（$C050 关、$C051 开）"),
            field("mixed", state.mixed ? 1 : 0, "混合模式（$C052 关、$C053 开）"),
            field("page2", state.page2 ? 1 : 0, "第 2 页（$C054 关、$C055 开）"),
            field("hires", state.hires ? 1 : 0, "高分辨率（$C056 关、$C057 开）"),
            field("ramRead", state.ramRead ? 1 : 0, "辅存读开关（$C002 关、$C003 开）"),
            field("ramWrite", state.ramWrite ? 1 : 0, "辅存写开关（$C004 关、$C005 开）"),
            field("internalRom", state.internalRom ? 1 : 0, "内部 ROM 开关（$C006 关、$C007 开）"),
            field("altZeroPage", state.altZeroPage ? 1 : 0, "辅助零页开关（$C008 关、$C009 开），为 1 且语言卡读 RAM 时 $D000-$FFFF 读汉字程序"),
            field("slotThreeRom", state.slotThreeRom ? 1 : 0, "槽 3 ROM 开关（$C00A 关、$C00B 开）"),
            field("languageCardReadRam", state.lcReadRam ? 1 : 0, "语言卡读 RAM"),
            field("languageCardWriteRam", state.lcWriteRam ? 1 : 0, "语言卡写允许"),
            field("languageCardBank2", state.lcBank2 ? 1 : 0, "语言卡选 bank 2"),
            field("aux", state.aux, "$C0B0 寄存器：位 0 到 3 字库页，位 4 允许彩色，位 5 选 LOGO 或汉字表")};
  }
  if (module == "disk") {
    return {field("motor", disk_.motorOn() ? 1 : 0, "马达是否在转"),
            field("track", disk_.track(), "读头所在的磁道"),
            field("halfTrack", disk_.halfTrack(), "读头所在的半磁道位置"),
            field("writeMode", disk_.writeMode() ? 1 : 0, "写模式（Q7）"),
            field("loadMode", disk_.loadMode() ? 1 : 0, "装入态（Q6）"),
            field("phases", disk_.phases(), "四个相位磁铁的状态（位 0 到 3）")};
  }
  throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
}

void Cec1Machine::writeState(std::string_view module, std::span<const hw::StateField> fields) {
  if (module == "board" || module == "disk") throw shared::CoreError(hw::ERROR_INVALID_STATE, std::string(module) + " 的状态只读");
  if (module != "cpu") throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
  debug_.writeCpuState(fields);
}

hw::StepResult Cec1Machine::step(hw::StepUnit unit, std::uint64_t count, bool stopAtBreakpoints) {
  if (!poweredOn_ || count == 0) return {};
  return debug_.step(unit, count, stopAtBreakpoints, {[&] { cpu_.run(1); }, [&] { return cpu_.registers().cycles; }, [&] { return video_.frameCount(); }, [&] { return video_.scanline(); }});
}

std::vector<std::uint32_t> Cec1Machine::colorTable() const { return logic::hardware::VideoApple2::loresColors(); }

}  // namespace ember::bootstrap
