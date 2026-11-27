/*
 * ------------------------------------------------------------------------
 * 名称：pyramid.cpp
 * 说明：主机型号 pec9588 与 pec586（金字塔学习机）的装配、电源、运行、输入与调试接口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/pyramid/pyramid.hpp"

#include <algorithm>
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
constexpr std::uint16_t KEYBOARD_WRITE_ADDRESS = 0x4016;
constexpr std::uint16_t KEYBOARD_READ_ADDRESS = 0x4017;
/** PEC-9588 的打印机就绪线：读 $4017 的位 2。 */
constexpr std::uint8_t PRINTER_READY_BIT_9588 = 0x04;

/** 调色板的存储下标：$3F10、$3F14、$3F18、$3F1C 镜像 $3F00、$3F04、$3F08、$3F0C。 */
std::size_t paletteSlot(std::uint32_t address) {
  std::size_t index = address & 0x1FU;
  if ((index & 0x13U) == 0x10U) index &= ~std::size_t{0x10U};
  return index;
}

}  // namespace

// ---------------------------------------------------------------- 手柄与键盘口

hw::BusRead PyramidMachine::InputPort::read(std::uint16_t address) {
  hw::BusRead result = padApu_.read(address);
  if (address == KEYBOARD_READ_ADDRESS) {
    result.value = static_cast<std::uint8_t>(result.value | keyboard_.read());
    result.drivenMask = static_cast<std::uint8_t>(result.drivenMask | logic::hardware::KeyboardPyramid::DATA_MASK);
    if (board_.model() == PyramidBoard::Model::Pec9588 && board_.printerReady()) {
      result.value = static_cast<std::uint8_t>(result.value | PRINTER_READY_BIT_9588);
      result.drivenMask = static_cast<std::uint8_t>(result.drivenMask | PRINTER_READY_BIT_9588);
    }
  }
  return result;
}

void PyramidMachine::InputPort::write(std::uint16_t address, std::uint8_t value) {
  padApu_.write(address, value);
  if (address == KEYBOARD_WRITE_ADDRESS) keyboard_.write(value);
}

// ---------------------------------------------------------------- 装配

PyramidMachine::PyramidMachine(PyramidBoard::Model model, std::vector<std::uint8_t> rom, std::shared_ptr<logic::hardware::TapeDeck> tapeDeck, hw::PrinterOutput* printerOutput,
                               hw::FloppyActivity* floppyActivity, std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive)
    : cartridgeIrq_(irq_),
      ram_(INTERNAL_RAM_SIZE),
      ramDevice_(ram_),
      apu_(cpuBus_, scheduler_, irq_, rdy_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}, MASTER_CLOCK_HZ / static_cast<double>(CPU_TICKS_PER_CYCLE)),
      // 精灵取色是否后移由主板决定；回调只在渲染时调用，那时主板已经建好。
      ppu_(scheduler_, ppuBus_, nmi_, hw::ClockDomain{PPU_TICKS_PER_DOT}, PPU_PHASE_TICKS,
           [this] {
             logic::hardware::Ppu2c02::Options options{logic::hardware::Ppu2c02::SCANLINES_PER_FRAME, true};
             options.spritePaletteShifted = [this] { return board_.textMode(); };
             return options;
           }()),
      dma_(cpuBus_, scheduler_, rdy_, hw::ClockDomain{CPU_TICKS_PER_CYCLE}),
      tapeDeck_(tapeDeck != nullptr ? std::move(tapeDeck) : std::make_shared<logic::hardware::TapeDeck>()),
      floppy_(floppyDrive != nullptr ? std::move(floppyDrive) : std::make_shared<logic::hardware::StandardFloppyDrive>()),
      hasFloppy_(model == PyramidBoard::Model::Pec9588),
      disk2_(scheduler_, DISK_BIT_TICKS, DISK_MOTOR_OFF_TICKS, logic::hardware::disk2::pyramidFormatOf),
      padApuPort_(pad_, apu_),
      inputPort_(padApuPort_, keyboard_, board_),
      printer_(printerOutput != nullptr ? std::make_unique<logic::hardware::PrinterEscp>(scheduler_, *printerOutput, PRINTER_TIMEOUT_TICKS) : nullptr),
      board_(model, std::move(rom), *tapeDeck_, printer_.get(), hasFloppy_ ? &disk2_ : nullptr),
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
  if (hasFloppy_) {
    disk2_.attachDrive(floppy_.get());
    floppy_->attach(this, floppyActivity == nullptr ? std::function<void(bool)>() : [floppyActivity](bool on) { floppyActivity->motorChanged(0, on); });
  }
}

PyramidMachine::~PyramidMachine() {
  // 软驱与磁带机在主机之外继续存在：只解除自己的接入，盘片与磁带留在里面。
  if (hasFloppy_) floppy_->detach(this);
  try {
    tapeDeck_->unbind(scheduler_);
  } catch (...) {
    // 析构时无法再报告，录音文件保留已写入的部分。
  }
}

void PyramidMachine::powerOn() {
  // 磁带机重新接到本机的调度器（先停止，录音在这里写完），它的时间基准随时钟一起归零。
  tapeDeck_->bind(scheduler_, MASTER_CLOCK_HZ);
  tapeDeck_->setAudioRate(logic::hardware::Apu2a03::SAMPLE_RATE);
  keyboard_.reset();
  keyboard_.releaseAll();
  // 时钟先归零并丢弃旧事件，各设备复位时再登记自己的事件；CPU 最后复位，复位序列会推进时钟。
  scheduler_.reset();
  ram_.fill(0);
  apu_.reset(true);
  dma_.reset(true);
  ppu_.reset(true);
  cartridgeIrq_.set(false);
  board_.reset(true);
  disk2_.reset(true);
  if (printer_ != nullptr) printer_->reset();
  poweredOn_ = true;
  debug_.clearStopState();
  cpu_.reset(true);
  debug_.discardPendingHit();
}

void PyramidMachine::powerOff() {
  poweredOn_ = false;
  // 断电时磁带机停止并断开，磁带留在里面；软驱马达停转。
  tapeDeck_->unbind(scheduler_);
  disk2_.reset(true);
}

void PyramidMachine::softReset() {
  if (!poweredOn_) return;
  keyboard_.reset();
  apu_.reset(false);
  dma_.reset(false);
  ppu_.reset(false);
  board_.reset(false);
  disk2_.reset(false);
  if (printer_ != nullptr) printer_->reset();
  cpu_.reset(false);
  debug_.clearStopState();
}

void PyramidMachine::run(hw::MasterTime ticks) {
  if (!poweredOn_ || ticks <= 0) return;
  const hw::MasterTime cycles = (ticks + CPU_TICKS_PER_CYCLE - 1) / CPU_TICKS_PER_CYCLE;
  cpu_.run(static_cast<std::uint32_t>(cycles));
  ppu_.catchUp(scheduler_.now());
  apu_.catchUp(scheduler_.now());
}

void PyramidMachine::runFrame() {
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

void PyramidMachine::takeAudio(std::vector<float>& samples) {
  if (poweredOn_) apu_.catchUp(scheduler_.now());
  apu_.takeAudio(samples);
  tapeDeck_->mixAudio(samples);
}

void PyramidMachine::setButtons(unsigned port, std::uint8_t buttons) {
  if (port >= joypads_.size()) throw shared::CoreError(hw::ERROR_PORT_NOT_FOUND, "手柄口只有 2 个，编号为 0 与 1");
  joypads_[port].setButtons(buttons);
}

void PyramidMachine::input(const hw::InputEvent& event) {
  // 键位表里没有的键与鼠标事件丢弃。
  if (const auto* key = std::get_if<hw::KeyInput>(&event)) keyboard_.setKey(key->code, key->down);
}

// ---------------------------------------------------------------- 调试接口

std::vector<hw::MemorySpace> PyramidMachine::memorySpaces() const {
  std::vector<hw::MemorySpace> spaces = {
      {"cpu", "CPU 总线 $0000-$FFFF：读不产生副作用，寄存器类地址读出 0；写经真实的总线，与 CPU 写一样有副作用", CPU_SPACE_SIZE, true},
      {"ppu", "PPU 总线 $0000-$3FFF：图案区是字符 RAM，$2000 起是名称表显存，$3F00 起是调色板；读不产生副作用", PPU_SPACE_SIZE, true},
      {"board.ram", "内部 RAM（2K，$0000-$1FFF 每 2K 镜像一次）", INTERNAL_RAM_SIZE, true},
      {"board.rom", "程序 ROM（512K，只读，不含 iNES 文件头）", PyramidBoard::ROM_SIZE, false},
  };
  if (board_.model() == PyramidBoard::Model::Pec586) {
    spaces.push_back({"board.bootImage", "启动映像（32K，只读，上电后 $8000-$FFFF 的内容，由 ROM 每 16K 块末尾的两段 1K 拼成）", PyramidBoard::BOOT_IMAGE_SIZE, false});
  }
  spaces.push_back({"board.workRam", "工作 RAM（8K，$6000-$7FFF）", PyramidBoard::WORK_RAM_SIZE, true});
  spaces.push_back({"board.characterRam", "字符 RAM（8K，图案区）", PyramidBoard::CHARACTER_RAM_SIZE, true});
  spaces.push_back({"board.nametable", "名称表显存（2K，按 reg0 镜像）", PyramidBoard::NAMETABLE_SIZE, true});
  spaces.push_back({"chip.oam", "PPU 的精灵属性存储（OAM）", OAM_SIZE, true});
  spaces.push_back({"chip.palette", "PPU 的调色板（32 字节，$3F10、$3F14、$3F18、$3F1C 是 $3F00、$3F04、$3F08、$3F0C 的镜像，这里是未镜像的存储）", PALETTE_SIZE, true});
  return spaces;
}

void PyramidMachine::readMemory(std::string_view space, std::uint32_t address, std::span<std::uint8_t> out) {
  const auto copyFrom = [&](const auto& bytes) {
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
  } else if (space == "board.bootImage" && board_.model() == PyramidBoard::Model::Pec586) {
    copyFrom(board_.bootImage());
  } else if (space == "board.workRam") {
    copyFrom(board_.workRam());
  } else if (space == "board.characterRam") {
    copyFrom(board_.characterRam());
  } else if (space == "board.nametable") {
    copyFrom(board_.nametable());
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

void PyramidMachine::writeMemory(std::string_view space, std::uint32_t address, std::span<const std::uint8_t> bytes) {
  // 调试写入不触发读写断点，也不进记录。
  const CpuDebugKit::Suspend suspend(debug_, true);
  const auto copyTo = [&](auto& target) {
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
  } else if (space == "board.rom" || (space == "board.bootImage" && board_.model() == PyramidBoard::Model::Pec586)) {
    throw shared::CoreError(hw::ERROR_SPACE_READ_ONLY, "空间 " + std::string(space) + " 是只读的");
  } else if (space == "board.workRam") {
    copyTo(board_.workRam());
  } else if (space == "board.characterRam") {
    copyTo(board_.characterRam());
  } else if (space == "board.nametable") {
    copyTo(board_.nametable());
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

hw::AddressTarget PyramidMachine::resolve(std::string_view space, std::uint32_t address) const {
  if (space == "cpu") {
    checkRange(space, CPU_SPACE_SIZE, address, 1);
    if (address < 0x2000U) return {"ram", "board.ram", address & (INTERNAL_RAM_SIZE - 1), "内部 RAM，$0000-$1FFF 每 2K 镜像一次"};
    if (address < 0x4000U) return {"register", "chip.ppu", address & 7U, "PPU 寄存器，$2000-$3FFF 每 8 字节镜像一次"};
    if (address == 0x4014U) return {"register", "chip.dma", 0, "精灵 DMA"};
    if (address <= 0x4013U || address == 0x4015U) return {"register", "chip.apu", address - 0x4000U, "APU 寄存器"};
    if (address <= 0x4017U) return {"register", "chip.input", address - 0x4016U, "手柄口与键盘（写 $4017 同时属于 APU 的帧计数器；9588 读 $4017 位 2 是打印机就绪）"};
    if (address < 0x4020U) return {"none", "", 0, "没有设备，读到总线上最近的值"};
    const PyramidBoard::CpuLocation where = board_.locate(static_cast<std::uint16_t>(address));
    switch (where.kind) {
      case PyramidBoard::CpuLocation::Kind::Register:
        if (hasFloppy_ && address >= 0x5600U && address <= 0x56FFU) return {"register", "chip.disk2", address & 0x0FU, "5.25 英寸软驱控制器的寄存器（低 4 位是寄存器号：0-7 相位、8-9 马达、10-11 选驱动器、12-15 读写状态）"};
        return {"register", "board.registers", where.offset, "主板寄存器（$4100-$5FFF 写入 reg[(地址>>8)&7]、$5100 写磁带输出与打印机选通、$5200 写打印机数据、$5300 读 586 的磁带输入与打印机就绪、$5500 读状态与 9588 的磁带输入）"};
      case PyramidBoard::CpuLocation::Kind::Rom:
        return {"rom", "board.rom", where.offset, "程序 ROM"};
      case PyramidBoard::CpuLocation::Kind::BootImage:
        return {"rom", "board.bootImage", where.offset, "启动映像"};
      case PyramidBoard::CpuLocation::Kind::WorkRam:
        return {"ram", "board.workRam", where.offset, "工作 RAM"};
      case PyramidBoard::CpuLocation::Kind::None:
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

std::vector<std::string> PyramidMachine::stateModules() const { return {"cpu", "ppu", "board"}; }

std::vector<hw::StateField> PyramidMachine::readState(std::string_view module) const {
  if (module == "cpu") return debug_.readCpuState();
  if (module == "ppu") {
    return {field("scanline", ppu_.scanline(), "当前扫描线（0 到 261，261 是预渲染行）"),
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
    const auto& registers = board_.registers();
    std::vector<hw::StateField> fields;
    for (std::size_t index = 0; index < registers.size(); ++index) {
      const char* description = index == 0 ? "reg0：位 7 汉字模式、位 3 与位 4 同为 1 时水平镜像、其余位选程序窗口" : "寄存器（$4100-$5FFF 写入，地址高字节的低 3 位选序号）";
      fields.push_back(hw::StateField{"reg" + std::to_string(index), registers[index], description});
    }
    fields.push_back(field("textMode", board_.textMode() ? 1 : 0, "汉字模式：背景图块按单色字模读取"));
    fields.push_back(field("horizontalMirroring", board_.horizontalMirroring() ? 1 : 0, "名称表水平镜像"));
    if (hasFloppy_) {
      fields.push_back(field("diskTrack", disk2_.track(), "软驱读头所在的磁道（0 到 40）"));
      fields.push_back(field("diskMotor", disk2_.motorOn() ? 1 : 0, "软驱马达是否在转（马达关指令之后还转约一秒）"));
      fields.push_back(field("diskWriteMode", disk2_.writeMode() ? 1 : 0, "软驱控制器是否在写模式（Q7）"));
      fields.push_back(field("diskLoadMode", disk2_.loadMode() ? 1 : 0, "软驱控制器是否在装入态（Q6）：写模式下装入数据，读模式下检测写保护"));
    }
    return fields;
  }
  throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
}

void PyramidMachine::writeState(std::string_view module, std::span<const hw::StateField> fields) {
  if (module == "ppu" || module == "board") throw shared::CoreError(hw::ERROR_INVALID_STATE, std::string(module) + " 的状态只读");
  if (module != "cpu") throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + std::string(module));
  debug_.writeCpuState(fields);
}

hw::StepResult PyramidMachine::step(hw::StepUnit unit, std::uint64_t count, bool stopAtBreakpoints) {
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
