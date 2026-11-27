/*
 * ------------------------------------------------------------------------
 * 名称：sb2000.cpp
 * 说明：主机型号 sb2000（小霸王 2000 学习机，PAL）的装配、电源、运行、输入与调试接口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：CPU 总线：$0000-$1FFF 内部 RAM（2K 镜像）、$2000-$3FFF 图形端口（$2000-$2008、调色板 $2040-$207F）、
 *       $4000-$4013 与 $4015 PSG、$4014 精灵 DMA、$4016-$4017 手柄口、$4020-$4022 键盘、$4023-$4024 鼠标、$4025 并口数据、$4026 扩展 I/O、$4027 DAC、
 *       $4031 初始设置、$4032-$4033 中断、$4034-$4036 定时器、$4040-$4047 程序分页、$4048-$404F 通用 DMA、$4080-$4081 实时时钟、
 *       $4100-$43FF 超级 I/O（显存页、FC 模式开关、语音端口 $4302）、$4400-$5FFF 工作窗口、$6000-$7FFF 外部 RAM（8K）、$8000-$FFFF 程序窗口（分页到 BIOS ROM 或扩展 RAM）；
 *       没有器件的地址（如 $4018-$401F）读到总线上最近的值；
 *       显存总线（16 位）：$0000-$7FFF 字符 RAM，$8000-$FFFF 扩展显存的 32K 窗口；
 *       FC 兼容模式（$4301 写 $55）下 $2000-$3FFF 是标准 PPU、$4000-$4015 是标准 APU、$8000-$FFFF 是 MMC3 映射的程序窗口，显存总线是另一条 14 位的总线（见 sb2000-fc-board.hpp）。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/sb2000/sb2000.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;

namespace {

using debugging::checkRange;
using debugging::field;
using debugging::unknownSpace;

constexpr std::size_t INTERNAL_RAM_SIZE = 2048;
constexpr std::size_t EXTERNAL_RAM_SIZE = 8192;
constexpr std::size_t CPU_SPACE_SIZE = 0x10000;
constexpr std::size_t VIDEO_SPACE_SIZE = 0x10000;
constexpr std::size_t OAM_SIZE = 256;
constexpr std::size_t PALETTE_SIZE = 64;
/** 每行的主时钟周期数（341 点）。 */
constexpr hw::MasterTime LINE_TICKS = static_cast<hw::MasterTime>(logic::hardware::PpuUm6576::DOTS_PER_SCANLINE) * Sb2000Machine::PPU_TICKS_PER_DOT;
/** FC 兼容模式每帧的行数（PAL）。 */
constexpr int FC_SCANLINES_PER_FRAME = 312;
/** FC 兼容模式下标准 PPU 的调色板项数。 */
constexpr std::size_t FC_PALETTE_SIZE = 32;

void requireBios(const std::vector<std::uint8_t>& bios) {
  if (bios.size() != Sb2000Machine::BIOS_SIZE) {
    throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "sb2000 的 BIOS 必须是 " + std::to_string(Sb2000Machine::BIOS_SIZE) + " 字节，收到 " + std::to_string(bios.size()) + " 字节");
  }
}

}  // namespace

Sb2000Machine::Sb2000Machine(std::vector<std::uint8_t> bios, hw::PrinterOutput* printerOutput, hw::FloppyActivity* floppyActivity, std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive,
                             std::shared_ptr<logic::hardware::TapeDeck> tapeDeck)
    : cartridgeIrq_(irq_),
      bios_(std::move(bios)),
      ram_(INTERNAL_RAM_SIZE),
      ramDevice_(ram_),
      externalRam_(EXTERNAL_RAM_SIZE),
      externalRamDevice_(externalRam_),
      program_(paging_, bios_),
      mmc3Chip_(irq_),
      mmc3_(mmc3Chip_),
      workWindow_(sysinit_, ram_),
      fcProgram_(program_.extendedRam(), mmc3_),
      fcVideo_(video_, mmc3_),
      printer_(printerOutput != nullptr ? std::make_unique<logic::hardware::PrinterEscp>(scheduler_, *printerOutput, PRINTER_TIMEOUT_TICKS) : nullptr),
      parallel_(printer_ != nullptr ? static_cast<hw::ParallelPort&>(*printer_) : static_cast<hw::ParallelPort&>(nullParallel_)),
      irqController_(irq_),
      floppy_(floppyDrive != nullptr ? std::move(floppyDrive) : std::make_shared<logic::hardware::StandardFloppyDrive>()),
      tapeDeck_(tapeDeck != nullptr ? std::move(tapeDeck) : std::make_shared<logic::hardware::TapeDeck>()),
      fdc_(scheduler_, irqController_, logic::hardware::IrqUm6576::SOURCE_FLOPPY, FDC_STEP_TICKS, FDC_POLL_STEPS),
      speech_(scheduler_, MASTER_CLOCK_HZ),
      superIo_(video_, fdc_, speech_),
      timer_(scheduler_, irqController_, CPU_TICKS_PER_CYCLE, LINE_TICKS),
      expio_(parallel_),
      dma_(cpuBus_, scheduler_, rdy_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, *this),
      keyboardDevice_(scheduler_, MILLISECOND_TICKS),
      mouseDevice_(scheduler_, MILLISECOND_TICKS),
      keyboardPort_(keyboardDevice_, irqController_, scheduler_, MILLISECOND_TICKS),
      mousePort_(mouseDevice_, irqController_),
      printerPort_(parallel_),
      pad_(parallel_),
      psg_(scheduler_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, MASTER_CLOCK_HZ / static_cast<double>(CPU_TICKS_PER_CYCLE)),
      ppu_(scheduler_, videoBus_, nmi_, hw::ClockDomain{PPU_TICKS_PER_DOT}, 0),
      fcApu_(cpuBus_, scheduler_, irq_, rdy_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, MASTER_CLOCK_HZ / static_cast<double>(CPU_TICKS_PER_CYCLE), true),
      fcPpu_(scheduler_, fcVideoBus_, nmi_, hw::ClockDomain{PPU_TICKS_PER_DOT}, 0, logic::hardware::Ppu2c02::Options{FC_SCANLINES_PER_FRAME, false}),
      padApuPort_(pad_, fcApu_),
      ppuSwitch_(fcMode_, ppu_, fcPpu_),
      apuSwitch_(fcMode_, psg_, fcApu_),
      padSwitch_(fcMode_, pad_, padApuPort_),
      programSwitch_(fcMode_, program_, fcProgram_),
      cpu_(cpuBus_, scheduler_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, irq_, nmi_, rdy_),
      debug_(cpuBus_, cpu_) {
  // FC 兼容模式用点精度的 2C02，6576 模式按行处理。
  debug_.setVideoPosition([this] { return fcMode_ ? fcPpu_.positionAt(scheduler_.now()) : ppu_.position(); });
  requireBios(bios_);

  cpuBus_.mount(0x0000, 0x1FFF, ramDevice_);
  cpuBus_.mount(0x2000, 0x3FFF, ppuSwitch_);
  cpuBus_.mount(0x4000, 0x4013, apuSwitch_);
  cpuBus_.mount(0x4014, 0x4014, dma_);
  cpuBus_.mount(0x4015, 0x4015, apuSwitch_);
  cpuBus_.mount(0x4016, 0x4017, padSwitch_);
  cpuBus_.mount(0x4020, 0x4022, keyboardPort_);
  cpuBus_.mount(0x4023, 0x4024, mousePort_);
  cpuBus_.mount(0x4025, 0x4025, printerPort_);
  cpuBus_.mount(0x4026, 0x4026, expio_);
  cpuBus_.mount(0x4027, 0x4027, dac_);
  cpuBus_.mount(0x4031, 0x4031, sysinit_);
  cpuBus_.mount(0x4032, 0x4033, irqController_);
  cpuBus_.mount(0x4034, 0x4036, timer_);
  cpuBus_.mount(0x4040, 0x4047, paging_);
  cpuBus_.mount(0x4048, 0x404F, dma_);
  cpuBus_.mount(0x4080, 0x4081, rtc_);
  cpuBus_.mount(0x4100, 0x43FF, superIo_);
  cpuBus_.mount(0x4400, 0x5FFF, workWindow_);
  cpuBus_.mount(0x6000, 0x7FFF, externalRamDevice_);
  cpuBus_.mount(0x8000, 0xFFFF, programSwitch_);

  videoBus_.mount(0x0000, 0xFFFF, video_);
  fcVideoBus_.mount(0x0000, 0x3EFF, fcVideo_);

  for (unsigned index = 0; index < joypads_.size(); ++index) pad_.port(index).plug(&joypads_[index]);
  fdc_.attachDrive(0, floppy_.get());
  floppy_->attach(this, floppyActivity == nullptr ? std::function<void(bool)>() : [floppyActivity](bool on) { floppyActivity->motorChanged(0, on); });
  dac_.onLevel = [this](std::uint8_t level) { psg_.setDacLevel(level); };
  dma_.timingControlOn = [this] { return (keyboardPort_.timingControl() & 0x80) != 0; };
  expio_.onMouseReset = [this] { mouseDevice_.pulseControl(); };
  expio_.onSpeechReset = [this] { speech_.reset(false); };
  speech_.onLevel = [this](double level) { psg_.setVoiceLevel(level); };
  // $4301 写 $55 发生在 CPU 的总线访问里，只记下要求，等这条指令结束后再切换（见 runInstruction）。
  superIo_.onFcMode = [this] { fcSwitchPending_ = true; };
  workWindow_.onWindowSelect = [this](std::uint8_t value) { mmc3_.selectWindow(value); };
}

Sb2000Machine::~Sb2000Machine() {
  // 软驱与磁带机在主机之外继续存在：只解除自己的接入，盘片与磁带留在里面。
  floppy_->detach(this);
  try {
    tapeDeck_->unbind(scheduler_);
  } catch (...) {
    // 析构时无法再报告，录音文件保留已写入的部分。
  }
}

void Sb2000Machine::resetDevices(bool power) {
  sysinit_.reset(power);
  irqController_.reset(power);
  timer_.reset(power);
  rtc_.reset(power);
  paging_.reset(power);
  expio_.reset(power);
  dac_.reset(power);
  dma_.reset(power);
  fdc_.reset(power);
  keyboardDevice_.reset(power);
  mouseDevice_.reset(power);
  keyboardPort_.reset(power);
  mousePort_.reset(power);
  printerPort_.reset(power);
  pad_.reset(power);
  psg_.reset(power);
  ppu_.reset(power);
  parallel_.reset();
  speech_.reset(power);
}

void Sb2000Machine::powerOn() {
  // 磁带机重新接到本机的调度器（先停止，录音在这里写完），它的时间基准随时钟一起归零。
  tapeDeck_->bind(scheduler_, MASTER_CLOCK_HZ);
  tapeDeck_->setAudioRate(logic::hardware::Apu2a03::SAMPLE_RATE);
  // 时钟先归零并丢弃旧事件，各设备复位时再登记自己的事件；CPU 最后复位，复位序列会推进时钟。
  scheduler_.reset();
  // 上电总是 6576 模式：FC 模式的芯片静默（旧事件已随调度器清掉，这里只释放它们在线上的来源），MMC3 回到初始值。
  fcMode_ = false;
  fcSwitchPending_ = false;
  frameOffset_ = 0;
  fcPpu_.halt();
  fcApu_.halt();
  mmc3_.reset();
  ram_.fill(0);
  externalRam_.fill(0);
  program_.clear();
  video_.clear();
  cartridgeIrq_.set(false);
  resetDevices(true);
  poweredOn_ = true;
  mouseRemainderX_ = 0;
  mouseRemainderY_ = 0;
  debug_.clearStopState();
  cpu_.reset(true);
  debug_.discardPendingHit();
}

void Sb2000Machine::powerOff() {
  poweredOn_ = false;
  // 断电时磁带机停止并断开，磁带留在里面。
  tapeDeck_->unbind(scheduler_);
}

void Sb2000Machine::softReset() {
  if (!poweredOn_) return;
  if (fcMode_) {
    // FC 兼容模式：只复位 CPU、手柄口、DMA、APU 与 PPU，模式、MMC3 寄存器与全部存储保持（游戏中软复位不会回到 DOS）。
    dma_.reset(false);
    pad_.reset(false);
    fcApu_.reset(false);
    fcPpu_.reset(false);
  } else {
    resetDevices(false);
  }
  cpu_.reset(false);
  debug_.clearStopState();
}

void Sb2000Machine::enterFcMode() {
  fcSwitchPending_ = false;
  frameOffset_ = completedFrames();
  // 6576 的外设寄存器复位：初始设置识别进度清零（$4400-$5FFF 变回没有 6576 模式时的样子）、中断、定时器、键盘口、鼠标口、扩展 I/O、DAC；
  // 软驱、实时时钟、程序分页寄存器、通用 DMA 与 PSG 不动。
  sysinit_.reset(true);
  irqController_.reset(false);
  timer_.reset(false);
  keyboardPort_.reset(false);
  mousePort_.reset(false);
  expio_.reset(false);
  dac_.reset(false);
  // 换成标准 PPU 与 APU：6576 的 PPU 不再产生帧与 NMI。
  ppu_.halt();
  fcMode_ = true;
  mmc3_.resetIrq();
  fcApu_.reset(true);
  fcPpu_.reset(true);
  // CPU 从新程序窗口末尾的复位向量重新开始。
  cpu_.reset(false);
}

void Sb2000Machine::runInstruction() {
  cpu_.run(1);
  if (fcSwitchPending_) enterFcMode();
}

void Sb2000Machine::catchUpPpu() {
  if (fcMode_) fcPpu_.catchUp(scheduler_.now());
}

void Sb2000Machine::catchUpAudio() {
  if (fcMode_) fcApu_.catchUp(scheduler_.now());
  else psg_.catchUp(scheduler_.now());
}

void Sb2000Machine::run(hw::MasterTime ticks) {
  if (!poweredOn_ || ticks <= 0) return;
  const hw::MasterTime cycles = (ticks + CPU_TICKS_PER_CYCLE - 1) / CPU_TICKS_PER_CYCLE;
  // 逐条指令运行，使写 $4301 的指令结束后能立即切换。
  for (std::uint32_t done = 0; done < static_cast<std::uint32_t>(cycles);) {
    done += cpu_.run(1);
    if (fcSwitchPending_) enterFcMode();
  }
  catchUpPpu();
  catchUpAudio();
}

void Sb2000Machine::runFrame() {
  if (!poweredOn_) return;
  const std::uint64_t target = completedFrames() + 1;
  while (completedFrames() < target) {
    if (debug_.stopAtExecuteBreakpoint()) return;
    debug_.beforeInstruction();
    runInstruction();
    catchUpPpu();
    if (debug_.hasPendingHit()) return;
  }
  catchUpAudio();
}

void Sb2000Machine::takeAudio(std::vector<float>& samples) {
  if (poweredOn_) catchUpAudio();
  if (fcMode_) fcApu_.takeAudio(samples);
  else psg_.takeAudio(samples);
  tapeDeck_->mixAudio(samples);
}

void Sb2000Machine::setButtons(unsigned port, std::uint8_t buttons) {
  if (port >= joypads_.size()) throw shared::CoreError(hw::ERROR_PORT_NOT_FOUND, "手柄口只有 2 个，编号为 0 与 1");
  joypads_[port].setButtons(buttons);
}

// ---------------------------------------------------------------- 输入

void Sb2000Machine::input(const hw::InputEvent& event) {
  if (const auto* key = std::get_if<hw::KeyInput>(&event)) {
    // 键盘只接在学习机上：物理键位换成扫描码，不认识的键丢弃。
    if (const auto code = logic::hardware::KeyboardSb2000::scanCodeFor(key->code)) keyboardDevice_.setKey(*code, key->down);
  } else if (const auto* mouse = std::get_if<hw::MouseInput>(&event)) {
    applyMouse(*mouse);
  }
}

void Sb2000Machine::applyMouse(const hw::MouseInput& mouse) {
  if (mouse.kind == hw::MouseKind::Wheel) return;
  if (mouse.kind == hw::MouseKind::Leave) {
    mouseRemainderX_ = 0;
    mouseRemainderY_ = 0;
    mouseDevice_.move(0, 0, 0);
    return;
  }
  // 位移的单位是标准帧的像素；凑不够 1 个单位的小数留到下一次。
  mouseRemainderX_ += mouse.dx;
  mouseRemainderY_ += mouse.dy;
  const double wholeX = std::trunc(mouseRemainderX_);
  const double wholeY = std::trunc(mouseRemainderY_);
  mouseRemainderX_ -= wholeX;
  mouseRemainderY_ -= wholeY;
  // 左键 1、右键 2；中键与串口鼠标无关。
  const std::uint8_t buttons = mouse.buttons & (logic::hardware::MouseSb2000::BUTTON_LEFT | logic::hardware::MouseSb2000::BUTTON_RIGHT);
  mouseDevice_.move(static_cast<int>(wholeX), static_cast<int>(wholeY), buttons);
}

// ---------------------------------------------------------------- 通用 DMA 的存储

std::uint8_t Sb2000Machine::readSource(bool extendedRam, std::uint32_t offset) {
  const std::vector<std::uint8_t>& source = extendedRam ? program_.extendedRam() : bios_;
  return offset < source.size() ? source[offset] : std::uint8_t{0};
}

void Sb2000Machine::writeWork(std::uint16_t address, std::uint8_t value) {
  // 真机实测：目标在程序窗口（$8000 以上）时写不进去。
  if (address < 0x8000U) cpuBus_.write(address, value);
}

// ---------------------------------------------------------------- 调试接口

std::vector<hw::MemorySpace> Sb2000Machine::memorySpaces() const {
  return {
      {"cpu", "CPU 总线 $0000-$FFFF：读不产生副作用，寄存器类地址读出 0；写经真实的总线，与 CPU 写一样有副作用", CPU_SPACE_SIZE, true},
      {"ppu", "显存总线 $0000-$FFFF：$0000-$7FFF 字符 RAM，$8000-$FFFF 扩展显存的 32K 窗口（页由 $4300 选择）；FC 兼容模式下是 14 位的显存总线（$0000-$1FFF 图案、$2000-$3EFF 名称表，地址自动回绕）；读不产生副作用", VIDEO_SPACE_SIZE, true},
      {"board.ram", "内部 RAM（2K，$0000-$1FFF 每 2K 镜像一次）", INTERNAL_RAM_SIZE, true},
      {"board.externalRam", "外部 RAM（8K，$6000-$7FFF）", EXTERNAL_RAM_SIZE, true},
      {"board.bios", "BIOS ROM（512KB，只读，不含 iNES 文件头）", Sb2000Machine::BIOS_SIZE, false},
      {"board.extendedRam", "扩展 RAM（512KB，程序窗口的页号不小于 $80 时用）", Sb2000ProgramMemory::EXTENDED_RAM_SIZE, true},
      {"board.characterRam", "字符 RAM（32KB，显存地址 $0000-$7FFF）", Sb2000VideoMemory::CHARACTER_RAM_SIZE, true},
      {"board.extendedVram", "扩展显存（512KB，每页 32KB，显存地址 $8000-$FFFF 经 $4300 选页）", Sb2000VideoMemory::EXTENDED_VRAM_SIZE, true},
      {"chip.oam", "PPU 的精灵属性存储（OAM）", OAM_SIZE, true},
      {"chip.palette", "PPU 的调色板（6576 模式 64 项，FC 兼容模式 32 项，每项低 6 位）", fcMode_ ? FC_PALETTE_SIZE : PALETTE_SIZE, true},
  };
}

void Sb2000Machine::readMemory(std::string_view space, std::uint32_t address, std::span<std::uint8_t> out) {
  const auto copyFrom = [&](const std::vector<std::uint8_t>& bytes) {
    checkRange(space, bytes.size(), address, out.size());
    std::copy_n(bytes.begin() + address, out.size(), out.begin());
  };
  if (space == "cpu") {
    checkRange(space, CPU_SPACE_SIZE, address, out.size());
    for (std::size_t index = 0; index < out.size(); ++index) out[index] = cpuBus_.peek(static_cast<std::uint16_t>(address + index));
  } else if (space == "ppu") {
    checkRange(space, VIDEO_SPACE_SIZE, address, out.size());
    for (std::size_t index = 0; index < out.size(); ++index) {
      const auto target = static_cast<std::uint16_t>(address + index);
      out[index] = fcMode_ ? fcVideoBus_.peek(static_cast<std::uint16_t>(target & 0x3FFFU)) : videoBus_.peek(target);
    }
  } else if (space == "board.ram") {
    checkRange(space, INTERNAL_RAM_SIZE, address, out.size());
    ram_.readBlock(address, out);
  } else if (space == "board.externalRam") {
    checkRange(space, EXTERNAL_RAM_SIZE, address, out.size());
    externalRam_.readBlock(address, out);
  } else if (space == "board.bios") {
    copyFrom(bios_);
  } else if (space == "board.extendedRam") {
    copyFrom(program_.extendedRam());
  } else if (space == "board.characterRam") {
    copyFrom(video_.characterRam());
  } else if (space == "board.extendedVram") {
    copyFrom(video_.extendedVram());
  } else if (space == "chip.oam") {
    checkRange(space, OAM_SIZE, address, out.size());
    const std::span<const std::uint8_t> oam = fcMode_ ? fcPpu_.oam() : ppu_.oam();
    std::copy_n(oam.begin() + address, out.size(), out.begin());
  } else if (space == "chip.palette") {
    const std::span<const std::uint8_t> palette = fcMode_ ? fcPpu_.palette() : ppu_.palette();
    checkRange(space, palette.size(), address, out.size());
    std::copy_n(palette.begin() + address, out.size(), out.begin());
  } else {
    unknownSpace(space);
  }
}

void Sb2000Machine::writeMemory(std::string_view space, std::uint32_t address, std::span<const std::uint8_t> bytes) {
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
    checkRange(space, VIDEO_SPACE_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      const auto target = static_cast<std::uint16_t>(address + index);
      if (fcMode_) fcVideoBus_.write(static_cast<std::uint16_t>(target & 0x3FFFU), bytes[index]);
      else videoBus_.write(target, bytes[index]);
    }
  } else if (space == "board.ram") {
    checkRange(space, INTERNAL_RAM_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) ram_.write(address + index, bytes[index]);
  } else if (space == "board.externalRam") {
    checkRange(space, EXTERNAL_RAM_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) externalRam_.write(address + index, bytes[index]);
  } else if (space == "board.bios") {
    throw shared::CoreError(hw::ERROR_SPACE_READ_ONLY, "空间 board.bios 是只读的");
  } else if (space == "board.extendedRam") {
    copyTo(program_.extendedRam());
  } else if (space == "board.characterRam") {
    copyTo(video_.characterRam());
  } else if (space == "board.extendedVram") {
    copyTo(video_.extendedVram());
  } else if (space == "chip.oam") {
    checkRange(space, OAM_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      if (fcMode_) fcPpu_.pokeOam(address + index, bytes[index]);
      else ppu_.pokeOam(address + index, bytes[index]);
    }
  } else if (space == "chip.palette") {
    checkRange(space, fcMode_ ? FC_PALETTE_SIZE : PALETTE_SIZE, address, bytes.size());
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      if (fcMode_) fcPpu_.pokePalette(address + index, bytes[index]);
      else ppu_.pokePalette(address + index, bytes[index]);
    }
  } else {
    unknownSpace(space);
  }
}

hw::AddressTarget Sb2000Machine::resolve(std::string_view space, std::uint32_t address) const {
  if (space == "cpu") {
    checkRange(space, CPU_SPACE_SIZE, address, 1);
    if (address < 0x2000U) return {"ram", "board.ram", address & (INTERNAL_RAM_SIZE - 1), "内部 RAM，$0000-$1FFF 每 2K 镜像一次"};
    if (address < 0x2080U) return {"register", "chip.ppu", address - 0x2000U, "图形端口（$2000-$2008）与调色板（$2040-$207F）"};
    if (address < 0x4000U) return {"none", "", 0, "没有器件，读到总线上最近的值"};
    if (address <= 0x4013U || address == 0x4015U) return {"register", fcMode_ ? "chip.apu" : "chip.psg", address - 0x4000U, fcMode_ ? "APU 寄存器" : "PSG 寄存器"};
    if (address == 0x4014U) return {"register", "chip.dma", 0, "精灵 DMA"};
    if (address <= 0x4017U) return {"register", "chip.pad", address - 0x4016U, "手柄口"};
    if (address >= 0x4020U && address <= 0x4022U) return {"register", "chip.keyboard", address - 0x4020U, "键盘接口"};
    if (address == 0x4023U || address == 0x4024U) return {"register", "chip.mouse", address - 0x4023U, "鼠标接口"};
    if (address == 0x4025U) return {"register", "chip.printer", 0, "并口数据"};
    if (address == 0x4026U) return {"register", "chip.expio", 0, "扩展 I/O"};
    if (address == 0x4027U) return {"register", "chip.dac", 0, "DAC"};
    if (address == 0x4031U) return {"register", "chip.sysinit", 0, "初始设置"};
    if (address == 0x4032U || address == 0x4033U) return {"register", "chip.irq", address - 0x4032U, "中断屏蔽与状态"};
    if (address >= 0x4034U && address <= 0x4036U) return {"register", "chip.timer", address - 0x4034U, "定时器"};
    if (address >= 0x4040U && address <= 0x4047U) return {"register", "chip.paging", address - 0x4040U, "程序存储器分页"};
    if (address >= 0x4048U && address <= 0x404FU) return {"register", "chip.dma", address - 0x4048U + 1, "通用 DMA"};
    if (address == 0x4080U || address == 0x4081U) return {"register", "chip.rtc", address - 0x4080U, "实时时钟"};
    if (address >= 0x4100U && address < 0x4400U) return {"register", "board.superIo", address - 0x4100U, "超级 I/O（显存页 $4300、语音端口 $4302）"};
    if (address < 0x4400U) return {"none", "", 0, "没有器件，读到总线上最近的值"};
    if (fcMode_ && address >= 0x5000U && address < 0x6000U) return {"ram", "board.ram", address & (INTERNAL_RAM_SIZE - 1), "FC 兼容模式的 2K 工作 RAM（与内部 RAM 同一块）"};
    if (address < 0x6000U) return {"none", "", 0, fcMode_ ? "没有器件，读到总线上最近的值" : "6576 模式下不是 RAM，读出 $68，写入无效"};
    if (address < 0x8000U) return {"ram", "board.externalRam", address & (EXTERNAL_RAM_SIZE - 1), "外部 RAM（8K）"};
    if (fcMode_) return {"ram", "board.extendedRam", mmc3_.programOffset(static_cast<std::uint16_t>(address)), "程序窗口（FC 兼容模式），MMC3 映射到扩展 RAM 的 128K 窗口"};
    const logic::hardware::PagingUm6576::Location location = paging_.resolve(static_cast<std::uint16_t>(address));
    if (location.ram) return {"ram", "board.extendedRam", location.offset, "程序窗口，当前对应扩展 RAM"};
    return {"rom", "board.bios", location.offset, "程序窗口，当前对应 BIOS ROM"};
  }
  if (space == "ppu") {
    checkRange(space, VIDEO_SPACE_SIZE, address, 1);
    if (fcMode_) {
      const auto masked = static_cast<std::uint16_t>(address & 0x3FFFU);
      if (masked < 0x2000U) return {"rom", "board.extendedVram", mmc3_.characterOffset(masked), "图案区（FC 兼容模式），MMC3 映射到扩展显存的 128K 窗口，只读"};
      return {"ram", "board.characterRam", mmc3_.nametableOffset(masked), "名称表（FC 兼容模式），字符 RAM 的 $2000-$27FF，按 MMC3 的镜像连接"};
    }
    if (address < Sb2000VideoMemory::CHARACTER_RAM_SIZE) return {"ram", "board.characterRam", address, "字符 RAM（32KB）"};
    return {"ram", "board.extendedVram", static_cast<std::size_t>(video_.bank()) * Sb2000VideoMemory::WINDOW_SIZE + (address & 0x7FFFU), "扩展显存的窗口，页由 $4300 选择"};
  }
  unknownSpace(space);
}

std::vector<std::string> Sb2000Machine::stateModules() const { return {"cpu", "ppu", "paging", "mmc3"}; }

std::vector<hw::StateField> Sb2000Machine::readState(std::string_view module) const {
  if (module == "cpu") return debug_.readCpuState();
  if (module == "ppu" && fcMode_) {
    return {field("scanline", fcPpu_.scanline(), "当前扫描线（0 到 311，311 是预渲染行）"),
            field("dot", fcPpu_.dot(), "当前点（0 到 340）"),
            field("frame", static_cast<std::int64_t>(completedFrames()), "上电以来已完成的帧数"),
            field("vramAddress", fcPpu_.vramAddress(), "当前的显存地址 v（15 位）"),
            field("tempAddress", fcPpu_.tempAddress(), "临时显存地址 t（15 位）"),
            field("fineX", fcPpu_.fineX(), "精细 X 滚动（0 到 7）"),
            field("writeToggle", fcPpu_.writeToggle() ? 1 : 0, "写锁存 w"),
            field("vblank", fcPpu_.vblankFlag() ? 1 : 0, "vblank 标志"),
            field("spriteZeroHit", fcPpu_.spriteZeroHit() ? 1 : 0, "精灵 0 命中标志"),
            field("spriteOverflow", fcPpu_.spriteOverflow() ? 1 : 0, "精灵溢出标志"),
            field("control", fcPpu_.control(), "$2000 的值"),
            field("mask", fcPpu_.mask(), "$2001 的值"),
            field("status", fcPpu_.status(), "$2002 的当前取值（位 7 垂直消隐、位 6 精灵 0 命中、位 5 精灵溢出）")};
  }
  if (module == "ppu") {
    return {field("scanline", ppu_.nextLine(), "下一个要处理的扫描线（0 到 311，241 是垂直消隐的第一行）"),
            field("frame", static_cast<std::int64_t>(ppu_.frameCount()), "已完成的帧数"),
            field("vramAddress", ppu_.vramAddress(), "当前的显存地址"),
            field("control", ppu_.control(), "$2000 的值"),
            field("mask", ppu_.mask(), "$2001 的值"),
            field("status", ppu_.status(), "$2002 的状态位（位 7 垂直消隐、位 6 精灵 0 命中、位 5 精灵溢出）"),
            field("graphicMode", ppu_.reg2008(), "$2008 的值")};
  }
  if (module == "paging") {
    std::vector<hw::StateField> fields;
    for (unsigned window = 0; window < logic::hardware::PagingUm6576::WINDOW_COUNT; ++window) {
      static constexpr const char* NAMES[] = {"window0", "window1", "window2", "window3", "window4", "window5", "window6", "window7"};
      fields.push_back(field(NAMES[window], paging_.bank(window), "程序窗口的页号（$4040-$4047，小于 $80 是 BIOS ROM，不小于 $80 是扩展 RAM）"));
    }
    fields.push_back(field("videoBank", video_.bank(), "扩展显存的页（$4300 的低 4 位）"));
    fields.push_back(field("fcMode", fcMode_ ? 1 : 0, "是否在 FC 兼容模式（$4301 写 $55 后为 1，只有硬复位回到 0）"));
    return fields;
  }
  if (module == "mmc3") {
    std::vector<hw::StateField> fields;
    fields.push_back(field("window", mmc3_.window(), "128K 窗口号（$5006 选择）"));
    for (const auto& item : mmc3_.chipState()) fields.push_back(field(item.name.c_str(), item.value, item.description.c_str()));
    return fields;
  }
  throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
}

void Sb2000Machine::writeState(std::string_view module, std::span<const hw::StateField> fields) {
  if (module == "ppu" || module == "paging" || module == "mmc3") throw shared::CoreError(hw::ERROR_INVALID_STATE, std::string(module) + " 的状态只读");
  if (module != "cpu") throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
  debug_.writeCpuState(fields);
}

hw::StepResult Sb2000Machine::step(hw::StepUnit unit, std::uint64_t count, bool stopAtBreakpoints) {
  if (!poweredOn_ || count == 0) return {};
  const hw::StepResult result = debug_.step(unit, count, stopAtBreakpoints,
                                            {[&] {
                                               runInstruction();
                                               catchUpPpu();
                                             },
                                             [&] { return cpu_.registers().cycles; }, [&] { return completedFrames(); }, [&] { return currentLine(); }});
  catchUpAudio();
  return result;
}

}  // namespace ember::bootstrap
