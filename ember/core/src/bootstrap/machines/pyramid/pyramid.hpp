/*
 * ------------------------------------------------------------------------
 * 名称：pyramid.hpp
 * 说明：主机型号 pec9588（金字塔 PEC-9588 学习机）与 pec586（金字塔 PEC-586(686) 学习机，NTSC）的装配：
 *       2K 内部 RAM、2C02 PPU、2A03 APU、精灵 DMA、手柄口、键盘、磁带机、并口打印机、主板，
 *       以及 pec9588 的 5.25 英寸软驱。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：时钟与经典 FC 相同（主时钟 21.477272 MHz，CPU 12 分频、PPU 4 分频）；
 *       CPU 总线：$0000-$1FFF 内部 RAM（2K 镜像）、$2000-$3FFF PPU、$4000-$4013 与 $4015 APU、
 *       $4014 精灵 DMA、$4016-$4017 手柄口与键盘（写 $4017 同时转给 APU 的帧计数器）、
 *       $4020-$FFFF 主板（见 pyramid-board.hpp）；PPU 总线：$0000-$3EFF 由主板译码，调色板在 PPU 内；
 *       整机机型没有卡带，Machine 契约要求的卡槽是一个空的、不挂载的卡槽；
 *       磁带机与软驱 A 是独立于主机的设备，由外面创建并在多台主机之间共用，主机通电时接上磁带机；
 *       只有 pec9588 接软驱：软驱控制器 Disk2 经主板挂在 $5600-$56FF，pec586 没有软驱；
 *       具体实现只在这里创建并连线，模块之间不互相引用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bootstrap/machines/common/cpu-debug-kit.hpp"
#include "bootstrap/machines/common/pad-apu-port.hpp"
#include "bootstrap/machines/pyramid/pyramid-board.hpp"
#include "contracts/hardware/floppy-activity.hpp"
#include "contracts/hardware/machine.hpp"
#include "logic/hardware/apu-2a03/apu-2a03.hpp"
#include "logic/hardware/bus/mapped-bus.hpp"
#include "logic/hardware/cartridge-slot/standard-cartridge-slot.hpp"
#include "logic/hardware/cpu-6502/cpu-6502.hpp"
#include "logic/hardware/disk2/disk2.hpp"
#include "logic/hardware/dma-2a03/dma-2a03.hpp"
#include "logic/hardware/floppy-drive/standard-floppy-drive.hpp"
#include "logic/hardware/joypad/joypad.hpp"
#include "logic/hardware/keyboard-pyramid/keyboard-pyramid.hpp"
#include "logic/hardware/lines/line-output.hpp"
#include "logic/hardware/lines/wired-or-line.hpp"
#include "logic/hardware/pad-2a03/pad-2a03.hpp"
#include "logic/hardware/ppu-2c02/ppu-2c02.hpp"
#include "logic/hardware/printer-escp/printer-escp.hpp"
#include "logic/hardware/ram/ram-block.hpp"
#include "logic/hardware/scheduler/master-scheduler.hpp"
#include "logic/hardware/tape-deck/tape-deck.hpp"

namespace ember::bootstrap {

/** 金字塔学习机（NTSC）。 */
class PyramidMachine final : public contracts::hardware::Machine, private contracts::hardware::MachineDebugger {
 public:
  /** 主时钟频率（赫兹）。 */
  static constexpr double MASTER_CLOCK_HZ = 21'477'272.0;
  /** 每个 CPU 周期的主时钟周期数。 */
  static constexpr contracts::hardware::MasterTime CPU_TICKS_PER_CYCLE = 12;
  /** 每个 PPU 点的主时钟周期数。 */
  static constexpr contracts::hardware::MasterTime PPU_TICKS_PER_DOT = 4;
  /** 上电后 PPU 第 0 点相对时间 0 的延迟（主时钟周期数）。 */
  static constexpr contracts::hardware::MasterTime PPU_PHASE_TICKS = 1;
  /** 打印机没有数据多久之后输出当前页（6 秒，主时钟周期数）。 */
  static constexpr contracts::hardware::MasterTime PRINTER_TIMEOUT_TICKS = 6 * 21'477'272;
  /** 软驱盘片上一个位的时间：4 微秒（主时钟周期数，带小数）。 */
  static constexpr double DISK_BIT_TICKS = MASTER_CLOCK_HZ * 4e-6;
  /** 马达关指令之后到真正停转的时间：1 秒（主时钟周期数）。 */
  static constexpr contracts::hardware::MasterTime DISK_MOTOR_OFF_TICKS = 21'477'272;

  /**
   * @param model 主板型号。
   * @param rom 程序 ROM 的内容，大小必须是 512K（不含 iNES 文件头）。
   * @param tapeDeck 磁带机：独立于主机的设备，由外面创建并在多台主机之间共用，主机通电时接上它；为空时主机自己带一台（测试用）。
   * @param printerOutput 打印机输出的页交给它，必须活到主机销毁；为空时并口不接打印机。
   * @param floppyActivity 软驱马达开关变化时通知它，必须活到主机销毁；为空时不通知。
   *        pec586 没有软驱，不用。
   * @param floppyDrive 软驱 A：独立于主机的设备，由外面创建并在多台主机之间共用。
   *        pec9588 创建时接入、销毁时解除，盘片留在里面；为空时主机自己带一个（测试用）。
   * @throws shared::CoreError hardware.invalidArgument：ROM 大小不对。
   */
  PyramidMachine(PyramidBoard::Model model, std::vector<std::uint8_t> rom, std::shared_ptr<logic::hardware::TapeDeck> tapeDeck = nullptr, contracts::hardware::PrinterOutput* printerOutput = nullptr,
                 contracts::hardware::FloppyActivity* floppyActivity = nullptr, std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive = nullptr);
  ~PyramidMachine() override;

  PyramidMachine(const PyramidMachine&) = delete;
  PyramidMachine& operator=(const PyramidMachine&) = delete;

  void powerOn() override;
  void powerOff() override;
  void softReset() override;
  [[nodiscard]] bool isPoweredOn() const override { return poweredOn_; }
  void run(contracts::hardware::MasterTime ticks) override;
  void runFrame() override;
  [[nodiscard]] contracts::hardware::FrameBuffer frame() const override { return ppu_.frame(); }
  /** NTSC：每帧 262 行，开启背景的奇数帧少一个点，平均每帧 89341.5 个点。 */
  [[nodiscard]] double framesPerSecond() const override { return MASTER_CLOCK_HZ / (static_cast<double>(PPU_TICKS_PER_DOT) * (341.0 * 262.0 - 0.5)); }
  [[nodiscard]] unsigned audioSampleRate() const override { return logic::hardware::Apu2a03::SAMPLE_RATE; }
  void takeAudio(std::vector<float>& samples) override;
  void setButtons(unsigned port, std::uint8_t buttons) override;
  [[nodiscard]] unsigned padPortCount() const override { return static_cast<unsigned>(joypads_.size()); }
  void input(const contracts::hardware::InputEvent& event) override;
  /** 金字塔学习机的键盘属于机器。 */
  [[nodiscard]] bool keyboardIsMachineInput() const override { return true; }
  [[nodiscard]] contracts::hardware::CpuRegisters cpuRegisters() const override { return cpu_.registers(); }
  [[nodiscard]] const contracts::hardware::Clock& clock() const override { return scheduler_; }
  contracts::hardware::CartridgeSlot& cartridgeSlot() override { return slot_; }
  contracts::hardware::IrqOutput& cartridgeIrq() override { return cartridgeIrq_; }
  contracts::hardware::MachineDebugger* debugger() override { return this; }
  contracts::hardware::PrinterControl* printer() override { return printer_.get(); }
  contracts::hardware::TapeControl* tape() override { return tapeDeck_.get(); }
  [[nodiscard]] unsigned floppyDriveCount() const override { return hasFloppy_ ? 1 : 0; }
  contracts::hardware::FloppyDrive* floppyDrive(unsigned index) override { return hasFloppy_ && index == 0 ? floppy_.get() : nullptr; }

  // 测试与调试用
  [[nodiscard]] logic::hardware::Ppu2c02& ppu() { return ppu_; }
  [[nodiscard]] const logic::hardware::Ppu2c02& ppu() const { return ppu_; }
  [[nodiscard]] logic::hardware::Apu2a03& apu() { return apu_; }
  [[nodiscard]] PyramidBoard& board() { return board_; }
  [[nodiscard]] const PyramidBoard& board() const { return board_; }
  [[nodiscard]] logic::hardware::KeyboardPyramid& keyboard() { return keyboard_; }
  [[nodiscard]] logic::hardware::Disk2& disk2() { return disk2_; }
  /** 2K 内部 RAM。 */
  [[nodiscard]] const contracts::hardware::StorageRegion& internalRam() const { return ram_; }
  /** 不经 CPU 读 CPU 总线，读不改变状态。 */
  std::uint8_t peekCpuBus(std::uint16_t address) { return cpuBus_.peek(address); }

 private:
  /** $4016-$4017：在手柄口与 APU 帧计数器之上，加上键盘的信号线：写 $4016 整个字节给键盘，读 $4017 位 1 是键状态；PEC-9588 的打印机就绪线也在 $4017 的位 2。 */
  class InputPort final : public contracts::hardware::BusDevice {
   public:
    InputPort(contracts::hardware::BusDevice& padApu, logic::hardware::KeyboardPyramid& keyboard, const PyramidBoard& board) : padApu_(padApu), keyboard_(keyboard), board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    contracts::hardware::BusDevice& padApu_;
    logic::hardware::KeyboardPyramid& keyboard_;
    const PyramidBoard& board_;
  };

  // MachineDebugger
  [[nodiscard]] std::vector<contracts::hardware::MemorySpace> memorySpaces() const override;
  void readMemory(std::string_view space, std::uint32_t address, std::span<std::uint8_t> out) override;
  void writeMemory(std::string_view space, std::uint32_t address, std::span<const std::uint8_t> bytes) override;
  [[nodiscard]] contracts::hardware::AddressTarget resolve(std::string_view space, std::uint32_t address) const override;
  [[nodiscard]] std::vector<std::string> stateModules() const override;
  [[nodiscard]] std::vector<contracts::hardware::StateField> readState(std::string_view module) const override;
  void writeState(std::string_view module, std::span<const contracts::hardware::StateField> fields) override;
  contracts::hardware::StepResult step(contracts::hardware::StepUnit unit, std::uint64_t count, bool stopAtBreakpoints) override;
  contracts::hardware::Breakpoint addBreakpoint(contracts::hardware::BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition) override { return debug_.addBreakpoint(kind, first, last, condition); }
  std::optional<contracts::hardware::Breakpoint> updateBreakpoint(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition) override { return debug_.updateBreakpoint(id, enabled, condition); }
  bool removeBreakpoint(std::uint32_t id) override { return debug_.removeBreakpoint(id); }
  std::size_t clearBreakpoints() override { return debug_.clearBreakpoints(); }
  [[nodiscard]] std::vector<contracts::hardware::Breakpoint> breakpoints() const override { return debug_.breakpoints(); }
  std::optional<contracts::hardware::BreakpointHit> takeBreakpointHit() override { return debug_.takeBreakpointHit(); }
  std::uint32_t startRecorder(const contracts::hardware::RecorderSpec& spec) override { return debug_.startRecorder(spec); }
  contracts::hardware::RecorderOutput stopRecorder(std::uint32_t id) override { return debug_.stopRecorder(id); }
  [[nodiscard]] contracts::hardware::RecorderSlice readRecorder(std::uint32_t id, std::uint64_t from, std::uint32_t maxLines) const override { return debug_.readRecorder(id, from, maxLines); }
  [[nodiscard]] std::vector<contracts::hardware::DisassembledInstruction> disassemble(std::uint16_t address, std::uint32_t count) override { return debug_.disassemble(address, count); }
  void startCdl(std::size_t size, contracts::hardware::CdlResolver resolver) override { debug_.startCdl(size, std::move(resolver)); }
  void stopCdl() override { debug_.stopCdl(); }
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> cdlFlags() const override { return debug_.cdlFlags(); }
  [[nodiscard]] std::vector<contracts::hardware::CallFrame> callStack() const override { return debug_.callStack(); }
  void startProfiler() override { debug_.startProfiler(); }
  void stopProfiler() override { debug_.stopProfiler(); }
  [[nodiscard]] std::optional<contracts::hardware::ProfileReport> profile() const override { return debug_.profile(); }
  void startEventLog(std::uint32_t maxEvents) override { debug_.startEventLog(maxEvents); }
  void stopEventLog() override { debug_.stopEventLog(); }
  [[nodiscard]] std::optional<contracts::hardware::EventLogOutput> eventLog(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents) const override { return debug_.eventLog(fromFrame, maxEvents); }
  [[nodiscard]] std::vector<std::uint32_t> colorTable() const override {
    const auto table = ppu_.colorTable();
    return {table.begin(), table.end()};
  }

  logic::hardware::MasterScheduler scheduler_;
  logic::hardware::WiredOrIrqLine irq_;
  logic::hardware::WiredOrIrqLine nmi_;
  logic::hardware::WiredOrRdyLine rdy_;
  /** 卡槽的 IRQ 引脚（整机机型没有卡带，只为满足 Machine 契约）。 */
  logic::hardware::LineIrqOutput cartridgeIrq_;
  /** 空卡槽：不挂载到任何总线。 */
  logic::hardware::StandardCartridgeSlot slot_;
  logic::hardware::CpuAddressBus cpuBus_;
  logic::hardware::PpuAddressBus ppuBus_;

  logic::hardware::RamBlock ram_;
  logic::hardware::MirroredRam ramDevice_;
  logic::hardware::Apu2a03 apu_;
  logic::hardware::Ppu2c02 ppu_;
  logic::hardware::Dma2a03 dma_;
  logic::hardware::Pad2a03 pad_;
  /** 磁带机：独立于主机的设备；主机通电时才绑定自己的调度器。 */
  std::shared_ptr<logic::hardware::TapeDeck> tapeDeck_;
  /** 软驱 A：独立于主机的设备；只有 pec9588 接上它（hasFloppy_）。 */
  std::shared_ptr<logic::hardware::StandardFloppyDrive> floppy_;
  bool hasFloppy_;
  /** 5.25 英寸软驱控制器；pec586 里没有接到总线上。 */
  logic::hardware::Disk2 disk2_;
  /** $4016-$4017：手柄口与 APU 帧计数器共用，写入同时转给两边。 */
  PadApuPort padApuPort_;
  logic::hardware::KeyboardPyramid keyboard_;
  InputPort inputPort_;
  std::array<logic::hardware::Joypad, logic::hardware::Pad2a03::PORT_COUNT> joypads_;
  /** 虚拟针式打印机；没有输出方时为空。 */
  std::unique_ptr<logic::hardware::PrinterEscp> printer_;
  PyramidBoard board_;
  logic::hardware::Cpu6502 cpu_;
  /** 断点、记录、代码数据标记与反汇编。 */
  CpuDebugKit debug_;

  bool poweredOn_{false};
};

}  // namespace ember::bootstrap
