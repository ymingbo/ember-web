/*
 * ------------------------------------------------------------------------
 * 名称：bbg.cpp
 * 说明：主机型号 bbg（步步高学习机）的装配、电源、运行、输入与调试接口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：CPU 总线：$0000-$1FFF 内部 RAM（2K 镜像）、$2000-$3FFF PPU（经主板代理，写 $2000、$2001、$2005 时同步写系统工作区）、$4000-$4013 与 $4015 APU、
 *       $4014 精灵 DMA、$4016-$4017 手柄 1、键盘与鼠标（写 $4017 转给 APU 的帧计数器）、$4020-$FFFF 主板（存储与整机寄存器，见 bbg-board.hpp）；$4018-$401F 无设备（开放总线）；
 *       PPU 总线：$0000-$3EFF 由主板译码（字符 RAM 与名称表显存），调色板在 PPU 内。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/bbg/bbg.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;

namespace {

using debugging::checkRange;
using debugging::field;
using debugging::unknownSpace;

constexpr std::size_t INTERNAL_RAM_SIZE = 2048;
constexpr std::size_t CPU_SPACE_SIZE = 0x10000;
constexpr std::size_t PPU_SPACE_SIZE = 0x4000;
constexpr std::size_t OAM_SIZE = 256;
constexpr std::size_t PALETTE_SIZE = 32;
/** 软驱控制器请求中断时用的来源位（中断不接 CPU，只为满足模块的接口）。 */
constexpr std::uint8_t FDC_INTERRUPT_SOURCE = 0x08;

/** 调色板的存储下标：$3F10、$3F14、$3F18、$3F1C 镜像 $3F00、$3F04、$3F08、$3F0C。 */
std::size_t paletteSlot(std::uint32_t address) {
  std::size_t index = address & 0x1FU;
  if ((index & 0x13U) == 0x10U) index &= ~std::size_t{0x10U};
  return index;
}

}  // namespace

// ---------------------------------------------------------------- 装配

BbgMachine::BbgMachine(const BbgVariant& variant, std::vector<std::uint8_t> bios, hw::PrinterOutput* printerOutput, hw::FloppyActivity* floppyActivity,
                       std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive, std::shared_ptr<logic::hardware::TapeDeck> tapeDeck)
    : cartridgeIrq_(irq_),
      ram_(INTERNAL_RAM_SIZE),
      ramDevice_(ram_),
      apu_(cpuBus_, scheduler_, irq_, rdy_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, MASTER_CLOCK_HZ / static_cast<double>(CPU_TICKS_PER_CYCLE), false),
      ppu_(scheduler_, ppuBus_, nmi_, hw::ClockDomain{PPU_TICKS_PER_DOT}, 0, logic::hardware::Ppu2c02::Options{SCANLINES_PER_FRAME, false, VBLANK_LINE}),
      dma_(cpuBus_, scheduler_, rdy_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}),
      inputPort_(pad_.port(0), apu_, keyboard_, &mouse_),
      floppy_(floppyDrive != nullptr ? std::move(floppyDrive) : std::make_shared<logic::hardware::StandardFloppyDrive>()),
      tapeDeck_(tapeDeck != nullptr ? std::move(tapeDeck) : std::make_shared<logic::hardware::TapeDeck>()),
      fdc_(scheduler_, ignoredInterrupts_, FDC_INTERRUPT_SOURCE, FDC_STEP_TICKS, FDC_POLL_STEPS),
      speech_(scheduler_, MASTER_CLOCK_HZ, logic::hardware::SpeechLpc::Variant::Bbg),
      printer_(printerOutput != nullptr ? std::make_unique<logic::hardware::PrinterEscp>(scheduler_, *printerOutput, PRINTER_TIMEOUT_TICKS) : nullptr),
      parallel_(printer_ != nullptr ? static_cast<hw::ParallelPort&>(*printer_) : static_cast<hw::ParallelPort&>(nullParallel_)),
      mmc3_(irq_),
      board_(variant, std::move(bios), scheduler_, irq_, ppu_, ppu_, fdc_, parallel_, speech_, mmc3_, static_cast<hw::MasterTime>(logic::hardware::Ppu2c02::DOTS_PER_SCANLINE) * PPU_TICKS_PER_DOT,
             PPU_TICKS_PER_DOT),
      cpu_(cpuBus_, scheduler_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, irq_, nmi_, rdy_),
      debug_(cpuBus_, cpu_) {
  debug_.setVideoPosition([this] { return ppu_.positionAt(scheduler_.now()); });
  cpuBus_.mount(0x0000, 0x1FFF, ramDevice_);
  cpuBus_.mount(0x2000, 0x3FFF, board_.ppuPort());
  cpuBus_.mount(0x4000, 0x4013, apu_);
  cpuBus_.mount(0x4014, 0x4014, dma_);
  cpuBus_.mount(0x4015, 0x4015, apu_);
  cpuBus_.mount(0x4016, 0x4017, inputPort_);
  cpuBus_.mount(0x4020, 0xFFFF, board_.cpuSide());

  ppuBus_.mount(0x0000, 0x3EFF, board_.videoSide());

  pad_.port(0).plug(&joypad_);
  fdc_.attachDrive(0, floppy_.get());
  floppy_->attach(this, floppyActivity == nullptr ? std::function<void(bool)>() : [floppyActivity](bool on) { floppyActivity->motorChanged(0, on); });
  speech_.onLevel = [this](double level) { voiceMixer_.note(scheduler_.now(), level); };
}

BbgMachine::~BbgMachine() {
  // 软驱与磁带机在主机之外继续存在：只解除自己的接入，盘片与磁带留在里面。
  floppy_->detach(this);
  try {
    tapeDeck_->unbind(scheduler_);
  } catch (...) {
    // 析构时无法再报告，录音文件保留已写入的部分。
  }
}

void BbgMachine::powerOn() {
  // 磁带机重新接到本机的调度器（先停止，录音在这里写完），它的时间基准随时钟一起归零。
  tapeDeck_->bind(scheduler_, MASTER_CLOCK_HZ);
  tapeDeck_->setAudioRate(logic::hardware::Apu2a03::SAMPLE_RATE);
  // 时钟先归零并丢弃旧事件，各设备复位时再登记自己的事件；CPU 最后复位，复位序列会推进时钟。
  scheduler_.reset();
  ram_.fill(0);
  voiceMixer_.reset();
  keyboard_.reset();
  keyboard_.releaseAll();
  mouse_.reset();
  mouseRemainderX_ = 0;
  mouseRemainderY_ = 0;
  fdc_.reset(true);
  speech_.reset(true);
  parallel_.reset();
  apu_.reset(true);
  dma_.reset(true);
  ppu_.reset(true);
  cartridgeIrq_.set(false);
  board_.reset(true);
  board_.startLineClock();
  poweredOn_ = true;
  debug_.clearStopState();
  cpu_.reset(true);
  debug_.discardPendingHit();
}

void BbgMachine::powerOff() {
  poweredOn_ = false;
  board_.stopLineClock();
  // 断电时磁带机停止并断开，磁带留在里面。
  tapeDeck_->unbind(scheduler_);
}

void BbgMachine::softReset() {
  if (!poweredOn_) return;
  keyboard_.reset();
  mouse_.reset();
  fdc_.reset(false);
  speech_.reset(false);
  apu_.reset(false);
  dma_.reset(false);
  ppu_.reset(false);
  board_.reset(false);
  board_.startLineClock();
  cpu_.reset(false);
  debug_.clearStopState();
}

void BbgMachine::run(hw::MasterTime ticks) {
  if (!poweredOn_ || ticks <= 0) return;
  const hw::MasterTime cycles = (ticks + CPU_TICKS_PER_CYCLE - 1) / CPU_TICKS_PER_CYCLE;
  cpu_.run(static_cast<std::uint32_t>(cycles));
  ppu_.catchUp(scheduler_.now());
  apu_.catchUp(scheduler_.now());
}

void BbgMachine::runFrame() {
  if (!poweredOn_) return;
  const std::uint64_t target = ppu_.frameCount() + 1;
  while (ppu_.frameCount() < target) {
    if (debug_.stopAtExecuteBreakpoint()) return;
    debug_.beforeInstruction();
    cpu_.run(1);
    ppu_.catchUp(scheduler_.now());
    if (debug_.hasPendingHit()) return;
  }
  apu_.catchUp(scheduler_.now());
}

void BbgMachine::takeAudio(std::vector<float>& samples) {
  if (poweredOn_) apu_.catchUp(scheduler_.now());
  apu_.takeAudio(samples);
  voiceMixer_.mixInto(samples, scheduler_.now());
  tapeDeck_->mixAudio(samples);
}

void BbgMachine::setButtons(unsigned port, std::uint8_t buttons) {
  if (port >= 1) throw shared::CoreError(hw::ERROR_PORT_NOT_FOUND, "步步高只有 1 个手柄口，编号为 0");
  joypad_.setButtons(buttons);
}

// ---------------------------------------------------------------- 输入

void BbgMachine::input(const hw::InputEvent& event) {
  if (const auto* key = std::get_if<hw::KeyInput>(&event)) {
    // 键位表里没有的键丢弃。
    keyboard_.setKey(key->code, key->down);
  } else if (const auto* mouse = std::get_if<hw::MouseInput>(&event)) {
    applyMouse(*mouse);
  }
}

void BbgMachine::applyMouse(const hw::MouseInput& mouse) {
  if (mouse.kind == hw::MouseKind::Wheel) return;
  if (mouse.kind == hw::MouseKind::Leave) {
    mouseRemainderX_ = 0;
    mouseRemainderY_ = 0;
    mouse_.move(0, 0, 0);
    return;
  }
  // 位移的单位是标准帧的像素；凑不够 1 个单位的小数留到下一次。
  mouseRemainderX_ += mouse.dx;
  mouseRemainderY_ += mouse.dy;
  const double wholeX = std::trunc(mouseRemainderX_);
  const double wholeY = std::trunc(mouseRemainderY_);
  mouseRemainderX_ -= wholeX;
  mouseRemainderY_ -= wholeY;
  // 界面的按键位：1 左键、2 右键、4 中键。
  std::uint8_t buttons = 0;
  if ((mouse.buttons & 1U) != 0) buttons = static_cast<std::uint8_t>(buttons | logic::hardware::MouseBbg::BUTTON_LEFT);
  if ((mouse.buttons & 2U) != 0) buttons = static_cast<std::uint8_t>(buttons | logic::hardware::MouseBbg::BUTTON_RIGHT);
  if ((mouse.buttons & 4U) != 0) buttons = static_cast<std::uint8_t>(buttons | logic::hardware::MouseBbg::BUTTON_MIDDLE);
  mouse_.move(static_cast<int>(wholeX), static_cast<int>(wholeY), buttons);
}

// ---------------------------------------------------------------- 调试接口

std::vector<hw::MemorySpace> BbgMachine::memorySpaces() const {
  const BbgVariant& variant = board_.variant();
  return {
      {"cpu", "CPU 总线 $0000-$FFFF：读不产生副作用，寄存器类地址读出 0；写经真实的总线，与 CPU 写一样有副作用", CPU_SPACE_SIZE, true},
      {"ppu", "PPU 总线 $0000-$3FFF：图案区是字符 RAM，$2000 起是名称表显存，$3F00 起是调色板；读不产生副作用", PPU_SPACE_SIZE, true},
      {"board.ram", "内部 RAM（2K，$0000-$1FFF 每 2K 镜像一次）", INTERNAL_RAM_SIZE, true},
      {"board.rom", "BIOS ROM（只读，不含 iNES 文件头）", variant.romSize, false},
      {"board.dram", "DRAM（顶部 32K 的前 16K 是系统工作区，对应 $4000-$7FFF）", variant.dramSize, true},
      {"board.characterRam", "字符 RAM（256K，按 1K 页接到图案区）", BbgBoard::CHARACTER_RAM_SIZE, true},
      {"board.nametable", "名称表显存（2K）", BbgBoard::NAMETABLE_SIZE, true},
      {"chip.oam", "PPU 的精灵属性存储（OAM）", OAM_SIZE, true},
      {"chip.palette", "PPU 的调色板（32 字节，$3F10、$3F14、$3F18、$3F1C 是 $3F00、$3F04、$3F08、$3F0C 的镜像，这里是未镜像的存储）", PALETTE_SIZE, true},
  };
}

void BbgMachine::readMemory(std::string_view space, std::uint32_t address, std::span<std::uint8_t> out) {
  const auto copyFrom = [&](const std::vector<std::uint8_t>& bytes) {
    checkRange(space, bytes.size(), address, out.size());
    std::copy_n(bytes.begin() + address, out.size(), out.begin());
  };
  if (space == "cpu") {
    checkRange(space, CPU_SPACE_SIZE, address, out.size());
    for (std::size_t index = 0; index < out.size(); ++index) out[index] = cpuBus_.peek(static_cast<std::uint16_t>(address + index));
  } else if (space == "ppu") {
    checkRange(space, PPU_SPACE_SIZE, address, out.size());
    for (std::size_t index = 0; index < out.size(); ++index) {
      const auto target = static_cast<std::uint16_t>(address + index);
      out[index] = target < 0x3F00U ? ppuBus_.peek(target) : ppu_.palette()[paletteSlot(target)];
    }
  } else if (space == "board.ram") {
    checkRange(space, INTERNAL_RAM_SIZE, address, out.size());
    ram_.readBlock(address, out);
  } else if (space == "board.rom") {
    copyFrom(board_.rom());
  } else if (space == "board.dram") {
    copyFrom(board_.dram());
  } else if (space == "board.characterRam") {
    copyFrom(board_.characterRam());
  } else if (space == "board.nametable") {
    checkRange(space, BbgBoard::NAMETABLE_SIZE, address, out.size());
    std::copy_n(board_.nametable().begin() + address, out.size(), out.begin());
  } else if (space == "chip.oam") {
    checkRange(space, OAM_SIZE, address, out.size());
    std::copy_n(ppu_.oam().begin() + address, out.size(), out.begin());
  } else if (space == "chip.palette") {
    checkRange(space, PALETTE_SIZE, address, out.size());
    std::copy_n(ppu_.palette().begin() + address, out.size(), out.begin());
  } else {
    unknownSpace(space);
  }
}

void BbgMachine::writeMemory(std::string_view space, std::uint32_t address, std::span<const std::uint8_t> bytes) {
  // 调试写入不触发读写断点，也不进记录。
  const CpuDebugKit::Suspend suspend(debug_, true);
  const auto copyTo = [&](std::vector<std::uint8_t>& target) {
    checkRange(space, target.size(), address, bytes.size());
    std::copy(bytes.begin(), bytes.end(), target.begin() + address);
  };
  if (space == "cpu") {
    checkRange(space, CPU_SPACE_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) cpuBus_.write(static_cast<std::uint16_t>(address + index), bytes[index]);
  } else if (space == "ppu") {
    checkRange(space, PPU_SPACE_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      const auto target = static_cast<std::uint16_t>(address + index);
      if (target < 0x3F00U) ppuBus_.write(target, bytes[index]);
      else ppu_.pokePalette(paletteSlot(target), bytes[index]);
    }
  } else if (space == "board.ram") {
    checkRange(space, INTERNAL_RAM_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) ram_.write(address + index, bytes[index]);
  } else if (space == "board.rom") {
    throw shared::CoreError(hw::ERROR_SPACE_READ_ONLY, "空间 board.rom 是只读的");
  } else if (space == "board.dram") {
    copyTo(board_.dram());
  } else if (space == "board.characterRam") {
    copyTo(board_.characterRam());
  } else if (space == "board.nametable") {
    checkRange(space, BbgBoard::NAMETABLE_SIZE, address, bytes.size());
    std::copy(bytes.begin(), bytes.end(), board_.nametable().begin() + address);
  } else if (space == "chip.oam") {
    checkRange(space, OAM_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) ppu_.pokeOam(address + index, bytes[index]);
  } else if (space == "chip.palette") {
    checkRange(space, PALETTE_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) ppu_.pokePalette(address + index, bytes[index]);
  } else {
    unknownSpace(space);
  }
}

hw::AddressTarget BbgMachine::resolve(std::string_view space, std::uint32_t address) const {
  if (space == "cpu") {
    checkRange(space, CPU_SPACE_SIZE, address, 1);
    if (address < 0x2000U) return {"ram", "board.ram", address & (INTERNAL_RAM_SIZE - 1), "内部 RAM，$0000-$1FFF 每 2K 镜像一次"};
    if (address < 0x4000U) return {"register", "chip.ppu", address & 7U, "PPU 寄存器，$2000-$3FFF 每 8 字节镜像一次"};
    if (address == 0x4014U) return {"register", "chip.dma", 0, "精灵 DMA"};
    if (address <= 0x4013U || address == 0x4015U) return {"register", "chip.apu", address - 0x4000U, "APU 寄存器"};
    if (address <= 0x4017U) return {"register", "chip.input", address - 0x4016U, "手柄 1、键盘与鼠标（写 $4017 同时属于 APU 的帧计数器）"};
    if (address < 0x4020U) return {"none", "", 0, "没有设备，读到总线上最近的值"};
    const BbgBoard::CpuLocation where = board_.locate(static_cast<std::uint16_t>(address));
    switch (where.kind) {
      case BbgBoard::CpuLocation::Kind::Register:
        return {"register", "board.registers", where.offset, "整机寄存器（$FF00-$FFFF，写入总是进寄存器）"};
      case BbgBoard::CpuLocation::Kind::Rom:
        return {"rom", "board.rom", where.offset, "BIOS ROM"};
      case BbgBoard::CpuLocation::Kind::Dram:
        return {"ram", "board.dram", where.offset, "DRAM"};
      case BbgBoard::CpuLocation::Kind::None:
        break;
    }
    return {"none", "", 0, "没有设备，读到总线上最近的值"};
  }
  if (space == "ppu") {
    checkRange(space, PPU_SPACE_SIZE, address, 1);
    if (address >= 0x3F00U) return {"ram", "chip.palette", paletteSlot(address), "调色板（每 32 字节镜像一次）"};
    bool isPattern = false;
    const std::size_t offset = board_.locateVideo(static_cast<std::uint16_t>(address), isPattern);
    if (isPattern) return {"ram", "board.characterRam", offset, "图案区，字符 RAM"};
    return {"ram", "board.nametable", offset, "名称表，主板的显存"};
  }
  unknownSpace(space);
}

std::vector<std::string> BbgMachine::stateModules() const { return {"cpu", "ppu", "board"}; }

std::vector<hw::StateField> BbgMachine::readState(std::string_view module) const {
  if (module == "cpu") return debug_.readCpuState();
  if (module == "ppu") {
    return {field("scanline", ppu_.scanline(), "当前扫描线（0 到 311，311 是预渲染行）"),
            field("dot", ppu_.dot(), "当前点（0 到 340）"),
            field("frame", static_cast<std::int64_t>(ppu_.frameCount()), "已完成的帧数"),
            field("vramAddress", ppu_.vramAddress(), "当前的显存地址 v（15 位）"),
            field("tempAddress", ppu_.tempAddress(), "临时显存地址 t（15 位）"),
            field("fineX", ppu_.fineX(), "精细 X 滚动（0 到 7）"),
            field("writeToggle", ppu_.writeToggle() ? 1 : 0, "写锁存 w"),
            field("vblank", ppu_.vblankFlag() ? 1 : 0, "vblank 标志"),
            field("spriteZeroHit", ppu_.spriteZeroHit() ? 1 : 0, "精灵 0 命中标志"),
            field("spriteOverflow", ppu_.spriteOverflow() ? 1 : 0, "精灵溢出标志"),
            field("control", ppu_.control(), "$2000 的值"),
            field("mask", ppu_.mask(), "$2001 的值"),
            field("status", ppu_.status(), "$2002 的当前取值（位 7 垂直消隐、位 6 精灵 0 命中、位 5 精灵溢出）")};
  }
  if (module == "board") {
    const BbgBoard::Registers registers = board_.registers();
    return {field("videoControl", registers.videoControl, "$FF01 视频控制"),
            field("prgBank0", registers.prgBank0, "$8000 起 8K 窗口的页号"),
            field("prgBank1", registers.prgBank1, "$A000 起 8K 窗口的页号"),
            field("ramEnable", registers.ramEnable ? 1 : 0, "$8000-$BFFF 是否用 DRAM"),
            field("cdBank", registers.cdBank, "$C000 起 8K 窗口的 DRAM 页号（$FF24）"),
            field("efBank", registers.efBank, "$E000 起 8K 窗口的 DRAM 页号（$FF2C）"),
            field("forceRam", registers.forceRam ? 1 : 0, "$FF11 位 7：强制 $C000 起用 DRAM"),
            field("scanlineCounter", registers.scanlineCounter, "扫描线计数器（$FF02、$FF06）"),
            field("mmc3Mode", registers.mmc3Mode ? 1 : 0, "是否在 MMC3 模式"),
            field("splitMode", registers.splitMode ? 1 : 0, "是否在分屏模式"),
            field("line", board_.line(), "行时钟的下一行（0 到 311）")};
  }
  throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
}

void BbgMachine::writeState(std::string_view module, std::span<const hw::StateField> fields) {
  if (module == "ppu" || module == "board") throw shared::CoreError(hw::ERROR_INVALID_STATE, std::string(module) + " 的状态只读");
  if (module != "cpu") throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
  debug_.writeCpuState(fields);
}

hw::StepResult BbgMachine::step(hw::StepUnit unit, std::uint64_t count, bool stopAtBreakpoints) {
  if (!poweredOn_ || count == 0) return {};
  const hw::StepResult result = debug_.step(unit, count, stopAtBreakpoints,
                                            {[&] {
                                               cpu_.run(1);
                                               ppu_.catchUp(scheduler_.now());
                                             },
                                             [&] { return cpu_.registers().cycles; }, [&] { return ppu_.frameCount(); }, [&] { return ppu_.scanline(); }});
  apu_.catchUp(scheduler_.now());
  return result;
}

}  // namespace ember::bootstrap
