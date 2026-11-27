/*
 * ------------------------------------------------------------------------
 * 名称：yuxing.cpp
 * 说明：主机型号 yuxing（裕兴学习机）的装配、电源、运行、输入与调试接口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：CPU 总线：$0000-$1FFF 内部 RAM（2K 镜像）、$2000-$3FFF PPU、$4000-$4013 与 $4015 APU、$4014 精灵 DMA、$4016-$4017 手柄、鼠标与磁带（写 $4017 转给 APU 的帧计数器）、
 *       $4020-$FFFF 主板（存储与整机寄存器，见 yuxing-board.hpp）；$4018-$401F 无设备（开放总线）；
 *       PPU 总线：$0000-$3EFF 由主板译码（字符 RAM 与名称表显存），调色板在 PPU 内。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/yuxing/yuxing.hpp"

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
/** $4016 的位：写入的位 0 是送给磁带的信号，读出的位 1 是磁带送回的信号。 */
constexpr std::uint16_t TAPE_ADDRESS = 0x4016;
constexpr std::uint8_t TAPE_OUTPUT_BIT = 0x01;
constexpr std::uint8_t TAPE_INPUT_BIT = 0x02;

/** 调色板的存储下标：$3F10、$3F14、$3F18、$3F1C 镜像 $3F00、$3F04、$3F08、$3F0C。 */
std::size_t paletteSlot(std::uint32_t address) {
  std::size_t index = address & 0x1FU;
  if ((index & 0x13U) == 0x10U) index &= ~std::size_t{0x10U};
  return index;
}

}  // namespace

// ---------------------------------------------------------------- 手柄、鼠标与磁带口

hw::BusRead YuxingMachine::InputPort::read(std::uint16_t address) {
  hw::BusRead result = padApu_.read(address);
  // 鼠标的数据位与手柄的数据位同在位 0。
  result.value = static_cast<std::uint8_t>(result.value | mouse_.read());
  if (address == TAPE_ADDRESS) {
    if (tape_.level()) result.value = static_cast<std::uint8_t>(result.value | TAPE_INPUT_BIT);
    // 打印机就绪也是位 1。
    if (printer_.ready()) result.value = static_cast<std::uint8_t>(result.value | TAPE_INPUT_BIT);
    result.drivenMask = static_cast<std::uint8_t>(result.drivenMask | TAPE_INPUT_BIT);
  }
  return result;
}

void YuxingMachine::InputPort::write(std::uint16_t address, std::uint8_t value) {
  padApu_.write(address, value);
  if (address == TAPE_ADDRESS) {
    tape_.drive((value & TAPE_OUTPUT_BIT) != 0);
    mouse_.write(value);
    printer_.write(value);
  }
}

// ---------------------------------------------------------------- 装配

YuxingMachine::YuxingMachine(std::vector<std::uint8_t> rom, std::shared_ptr<logic::hardware::TapeDeck> tapeDeck, hw::FloppyActivity* floppyActivity,
                             std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive, hw::PrinterOutput* printerOutput)
    : cartridgeIrq_(irq_),
      ram_(INTERNAL_RAM_SIZE),
      ramDevice_(ram_),
      apu_(cpuBus_, scheduler_, irq_, rdy_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, MASTER_CLOCK_HZ / static_cast<double>(CPU_TICKS_PER_CYCLE), false),
      // 精灵取色是否后移由主板决定；回调只在渲染时调用，那时主板已经建好。
      ppu_(scheduler_, ppuBus_, nmi_, hw::ClockDomain{PPU_TICKS_PER_DOT}, 0,
           [this] {
             logic::hardware::Ppu2c02::Options options{SCANLINES_PER_FRAME, false, VBLANK_LINE};
             options.spritePaletteShifted = [this] { return board_.spritePaletteShifted(); };
             options.dropVramWriteWhileRendering = true;
             return options;
           }()),
      dma_(cpuBus_, scheduler_, rdy_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}),
      tapeDeck_(tapeDeck != nullptr ? std::move(tapeDeck) : std::make_shared<logic::hardware::TapeDeck>()),
      printer_(printerOutput != nullptr ? std::make_unique<logic::hardware::PrinterEscp>(scheduler_, *printerOutput, PRINTER_TIMEOUT_TICKS) : nullptr),
      printerPort_(printer_.get()),
      padApuPort_(pad_, apu_),
      inputPort_(padApuPort_, *tapeDeck_, mouse_, printerPort_),
      floppy_(floppyDrive != nullptr ? std::move(floppyDrive) : std::make_shared<logic::hardware::StandardFloppyDrive>()),
      fdc_(scheduler_, ignoredInterrupts_, FDC_INTERRUPT_SOURCE, FDC_STEP_TICKS, FDC_POLL_STEPS),
      speech_(scheduler_, MASTER_CLOCK_HZ, logic::hardware::SpeechLpc::Variant::Yuxing),
      mmc3_(irq_),
      board_(std::move(rom), fdc_, speech_, keyboard_, mmc3_),
      cpu_(cpuBus_, scheduler_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, irq_, nmi_, rdy_),
      debug_(cpuBus_, cpu_) {
  debug_.setVideoPosition([this] { return ppu_.positionAt(scheduler_.now()); });
  cpuBus_.mount(0x0000, 0x1FFF, ramDevice_);
  cpuBus_.mount(0x2000, 0x3FFF, ppu_);
  cpuBus_.mount(0x4000, 0x4013, apu_);
  cpuBus_.mount(0x4014, 0x4014, dma_);
  cpuBus_.mount(0x4015, 0x4015, apu_);
  cpuBus_.mount(0x4016, 0x4017, inputPort_);
  cpuBus_.mount(0x4020, 0xFFFF, board_.cpuSide());

  ppuBus_.mount(0x0000, 0x3EFF, board_.videoSide());

  for (unsigned index = 0; index < joypads_.size(); ++index) pad_.port(index).plug(&joypads_[index]);
  fdc_.attachDrive(0, floppy_.get());
  floppy_->attach(this, floppyActivity == nullptr ? std::function<void(bool)>() : [floppyActivity](bool on) { floppyActivity->motorChanged(0, on); });
  speech_.onLevel = [this](double level) { voiceMixer_.note(scheduler_.now(), level); };
}

YuxingMachine::~YuxingMachine() {
  // 软驱与磁带机在主机之外继续存在：只解除自己的接入，盘片与磁带留在里面。
  floppy_->detach(this);
  try {
    tapeDeck_->unbind(scheduler_);
  } catch (...) {
    // 析构时无法再报告，录音文件保留已写入的部分。
  }
}

void YuxingMachine::powerOn() {
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
  apu_.reset(true);
  dma_.reset(true);
  ppu_.reset(true);
  cartridgeIrq_.set(false);
  board_.reset(true);
  printerPort_.reset();
  if (printer_ != nullptr) printer_->reset();
  poweredOn_ = true;
  debug_.clearStopState();
  cpu_.reset(true);
  debug_.discardPendingHit();
}

void YuxingMachine::powerOff() {
  poweredOn_ = false;
  // 断电时磁带机停止并断开，磁带留在里面。
  tapeDeck_->unbind(scheduler_);
}

void YuxingMachine::softReset() {
  if (!poweredOn_) return;
  keyboard_.reset();
  mouse_.reset();
  fdc_.reset(false);
  speech_.reset(false);
  apu_.reset(false);
  dma_.reset(false);
  ppu_.reset(false);
  board_.reset(false);
  printerPort_.reset();
  if (printer_ != nullptr) printer_->reset();
  cpu_.reset(false);
  debug_.clearStopState();
}

void YuxingMachine::run(hw::MasterTime ticks) {
  if (!poweredOn_ || ticks <= 0) return;
  const hw::MasterTime cycles = (ticks + CPU_TICKS_PER_CYCLE - 1) / CPU_TICKS_PER_CYCLE;
  cpu_.run(static_cast<std::uint32_t>(cycles));
  ppu_.catchUp(scheduler_.now());
  apu_.catchUp(scheduler_.now());
}

void YuxingMachine::runFrame() {
  if (!poweredOn_) return;
  // 鼠标的位移与按键每帧开始时锁存一次。
  mouse_.syncFrame();
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

void YuxingMachine::takeAudio(std::vector<float>& samples) {
  if (poweredOn_) apu_.catchUp(scheduler_.now());
  apu_.takeAudio(samples);
  tapeDeck_->mixAudio(samples);
  voiceMixer_.mixInto(samples, scheduler_.now());
}

void YuxingMachine::setButtons(unsigned port, std::uint8_t buttons) {
  if (port >= joypads_.size()) throw shared::CoreError(hw::ERROR_PORT_NOT_FOUND, "手柄口只有 2 个，编号为 0 与 1");
  joypads_[port].setButtons(buttons);
}

// ---------------------------------------------------------------- 输入

void YuxingMachine::input(const hw::InputEvent& event) {
  if (const auto* key = std::get_if<hw::KeyInput>(&event)) {
    // 键位表里没有的键丢弃。
    keyboard_.setKey(key->code, key->down);
  } else if (const auto* mouse = std::get_if<hw::MouseInput>(&event)) {
    applyMouse(*mouse);
  }
}

void YuxingMachine::applyMouse(const hw::MouseInput& mouse) {
  if (mouse.kind == hw::MouseKind::Wheel) return;
  if (mouse.kind == hw::MouseKind::Leave) {
    mouse_.releasePointer();
    return;
  }
  // 界面的按键位：1 左键、2 右键、4 中键；裕兴鼠标只有左右键。
  std::uint8_t buttons = 0;
  if ((mouse.buttons & 1U) != 0) buttons = static_cast<std::uint8_t>(buttons | logic::hardware::MouseYuxing::BUTTON_LEFT);
  if ((mouse.buttons & 2U) != 0) buttons = static_cast<std::uint8_t>(buttons | logic::hardware::MouseYuxing::BUTTON_RIGHT);

  /* 交**绝对坐标**，位移由 MouseYuxing::syncFrame() 自己算。
   *
   * 旧版是把界面送来的 dx/dy 累加后 clamp 到 ±127 —— 快速甩动时
   * 一帧累加几百像素，超出部分被 clamp 丢掉，指针永远追不上
   * （"指针不同步"的真正原因）。
   *
   * 改成绝对坐标逼近后，单帧最多走 MAX_STEP，剩下的留在目标里
   * 下一帧接着追，一位位移都不会少。 */
  mouse_.setPointer(static_cast<int>(mouse.x), static_cast<int>(mouse.y), buttons);
}

// ---------------------------------------------------------------- 调试接口

std::vector<hw::MemorySpace> YuxingMachine::memorySpaces() const {
  return {
      {"cpu", "CPU 总线 $0000-$FFFF：读不产生副作用，寄存器类地址读出 0；写经真实的总线，与 CPU 写一样有副作用", CPU_SPACE_SIZE, true},
      {"ppu", "PPU 总线 $0000-$3FFF：图案区是字符 RAM，$2000 起是名称表显存，$3F00 起是调色板；读不产生副作用", PPU_SPACE_SIZE, true},
      {"board.ram", "内部 RAM（2K，$0000-$1FFF 每 2K 镜像一次）", INTERNAL_RAM_SIZE, true},
      {"board.rom", "ROM（1024K，只读，不含 iNES 文件头）", YuxingBoard::ROM_SIZE, false},
      {"board.programRam", "程序 RAM（1024K，$6000 起与 $8000 起的窗口按页接到这里）", YuxingBoard::PROGRAM_RAM_SIZE, true},
      {"board.mmc3WorkRam", "MMC3 工作 RAM（8K，MMC3 模式且 reg5500 位 2 为 1 时接到 $6000 窗口）", YuxingBoard::MMC3_WORK_RAM_SIZE, true},
      {"board.staticRam", "静态 RAM（32K，小容量型号的 reg5500 位 0-1 为 1-3 时接到 $6000 窗口）", YuxingBoard::STATIC_RAM_SIZE, true},
      {"board.characterRam", "字符 RAM（128K，按页接到图案区）", YuxingBoard::CHARACTER_RAM_SIZE, true},
      {"board.nametable", "名称表显存（2K）", YuxingBoard::NAMETABLE_SIZE, true},
      {"chip.oam", "PPU 的精灵属性存储（OAM）", OAM_SIZE, true},
      {"chip.palette", "PPU 的调色板（32 字节，$3F10、$3F14、$3F18、$3F1C 是 $3F00、$3F04、$3F08、$3F0C 的镜像，这里是未镜像的存储）", PALETTE_SIZE, true},
  };
}

void YuxingMachine::readMemory(std::string_view space, std::uint32_t address, std::span<std::uint8_t> out) {
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
  } else if (space == "board.programRam") {
    copyFrom(board_.programRam());
  } else if (space == "board.mmc3WorkRam") {
    copyFrom(board_.mmc3WorkRam());
  } else if (space == "board.staticRam") {
    copyFrom(board_.staticRam());
  } else if (space == "board.characterRam") {
    copyFrom(board_.characterRam());
  } else if (space == "board.nametable") {
    checkRange(space, YuxingBoard::NAMETABLE_SIZE, address, out.size());
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

void YuxingMachine::writeMemory(std::string_view space, std::uint32_t address, std::span<const std::uint8_t> bytes) {
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
  } else if (space == "board.programRam") {
    copyTo(board_.programRam());
  } else if (space == "board.mmc3WorkRam") {
    copyTo(board_.mmc3WorkRam());
  } else if (space == "board.staticRam") {
    copyTo(board_.staticRam());
  } else if (space == "board.characterRam") {
    copyTo(board_.characterRam());
  } else if (space == "board.nametable") {
    checkRange(space, YuxingBoard::NAMETABLE_SIZE, address, bytes.size());
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

hw::AddressTarget YuxingMachine::resolve(std::string_view space, std::uint32_t address) const {
  if (space == "cpu") {
    checkRange(space, CPU_SPACE_SIZE, address, 1);
    if (address < 0x2000U) return {"ram", "board.ram", address & (INTERNAL_RAM_SIZE - 1), "内部 RAM，$0000-$1FFF 每 2K 镜像一次"};
    if (address < 0x4000U) return {"register", "chip.ppu", address & 7U, "PPU 寄存器，$2000-$3FFF 每 8 字节镜像一次"};
    if (address == 0x4014U) return {"register", "chip.dma", 0, "精灵 DMA"};
    if (address <= 0x4013U || address == 0x4015U) return {"register", "chip.apu", address - 0x4000U, "APU 寄存器"};
    if (address <= 0x4017U) return {"register", "chip.input", address - 0x4016U, "手柄 1 与 2、鼠标、磁带与打印机（写 $4017 同时属于 APU 的帧计数器）"};
    if (address < 0x4020U) return {"none", "", 0, "没有设备，读到总线上最近的值"};
    const YuxingBoard::CpuLocation where = board_.locate(static_cast<std::uint16_t>(address));
    switch (where.kind) {
      case YuxingBoard::CpuLocation::Kind::Register:
        return {"register", "board.registers", where.offset, "整机寄存器（软驱、键盘、语音、页寄存器）"};
      case YuxingBoard::CpuLocation::Kind::Rom:
        return {"rom", "board.rom", where.offset, "ROM"};
      case YuxingBoard::CpuLocation::Kind::ProgramRam:
        return {"ram", "board.programRam", where.offset, "程序 RAM"};
      case YuxingBoard::CpuLocation::Kind::Mmc3WorkRam:
        return {"ram", "board.mmc3WorkRam", where.offset, "MMC3 工作 RAM"};
      case YuxingBoard::CpuLocation::Kind::StaticRam:
        return {"ram", "board.staticRam", where.offset, "静态 RAM"};
      case YuxingBoard::CpuLocation::Kind::None:
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

std::vector<std::string> YuxingMachine::stateModules() const { return {"cpu", "ppu", "board"}; }

std::vector<hw::StateField> YuxingMachine::readState(std::string_view module) const {
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
    const YuxingBoard::Registers registers = board_.registers();
    return {field("reg4800", registers.reg4800, "$4800：位 0-4 ROM 页，位 5 页寄存器模式，位 7 分屏"),
            field("reg5500", registers.reg5500, "$5500：位 0-1 $6000 窗口，位 2 程序 RAM 模式，位 3 水平镜像，位 6 32K 页，位 7 4 分屏"),
            field("reg5501", registers.reg5501, "$5501：位 0-3 字符页，位 7 MMC3 模式"),
            field("pageRegister", registers.pageRegister, "程序 RAM 的页寄存器（$8000 起写入）"),
            field("keyboardRows", registers.keyboardRows, "键盘的行选择（$4202、$4203）"),
            field("mmc3Mode", registers.mmc3Mode ? 1 : 0, "是否在 MMC3 模式"),
            field("horizontalMirroring", registers.horizontalMirroring ? 1 : 0, "名称表水平镜像"),
            field("textMode", registers.textMode ? 1 : 0, "汉字单色模式"),
            field("fourWaySplit", registers.fourWaySplit ? 1 : 0, "4 分屏")};
  }
  throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
}

void YuxingMachine::writeState(std::string_view module, std::span<const hw::StateField> fields) {
  if (module == "ppu" || module == "board") throw shared::CoreError(hw::ERROR_INVALID_STATE, std::string(module) + " 的状态只读");
  if (module != "cpu") throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
  debug_.writeCpuState(fields);
}

hw::StepResult YuxingMachine::step(hw::StepUnit unit, std::uint64_t count, bool stopAtBreakpoints) {
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
