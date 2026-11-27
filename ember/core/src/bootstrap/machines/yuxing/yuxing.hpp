/*
 * ------------------------------------------------------------------------
 * 名称：yuxing.hpp
 * 说明：主机型号 yuxing（裕兴学习机，Dendy 制式）的装配：2A03 的 CPU 与 APU、2C02 的 PPU、精灵 DMA、手柄口 1 与 2、裕兴键盘、串口鼠标、软驱控制器、语音芯片、磁带机、打印机、MMC3 电路与主板。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：时钟：主时钟 26,601,712 Hz，CPU 15 分频（约 1.7734 MHz），PPU 5 分频（每个 CPU 周期 3 个点），每行 341 点、每帧 312 行、vblank 从第 291 行起（50 Hz）；APU 用 NTSC 的周期表；
 *       CPU 总线：$0000-$1FFF 内部 RAM（2K 镜像）、$2000-$3FFF PPU、$4000-$4013 与 $4015 APU、$4014 精灵 DMA、
 *       $4016-$4017 手柄 1 与 2、鼠标、磁带与打印机（写 $4017 转给 APU 的帧计数器，打印机见 yuxing-printer-port.hpp）、$4020-$FFFF 主板（见 yuxing-board.hpp）；$4018-$401F 无设备（开放总线）；
 *       PPU 总线：$0000-$3EFF 由主板译码（字符 RAM 与名称表显存），调色板在 PPU 内；
 *       整机机型没有卡带，Machine 契约要求的卡槽是一个空的、不挂载的卡槽；磁带机与软驱 A 都是独立于主机的设备，由外面创建并在多台主机之间共用；
 *       软驱控制器的中断不接 CPU，程序靠查询主状态；语音芯片的输出按主时钟时间混进 APU 的声音；
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
#include "bootstrap/machines/common/voice-mixer.hpp"
#include "bootstrap/machines/yuxing/yuxing-board.hpp"
#include "bootstrap/machines/yuxing/yuxing-printer-port.hpp"
#include "contracts/hardware/floppy-activity.hpp"
#include "contracts/hardware/interrupt-sink.hpp"
#include "contracts/hardware/machine.hpp"
#include "logic/hardware/apu-2a03/apu-2a03.hpp"
#include "logic/hardware/bus/mapped-bus.hpp"
#include "logic/hardware/cartridge-slot/standard-cartridge-slot.hpp"
#include "logic/hardware/cpu-6502/cpu-6502.hpp"
#include "logic/hardware/dma-2a03/dma-2a03.hpp"
#include "logic/hardware/fdc-upd765/fdc-upd765.hpp"
#include "logic/hardware/floppy-drive/standard-floppy-drive.hpp"
#include "logic/hardware/joypad/joypad.hpp"
#include "logic/hardware/keyboard-yuxing/keyboard-yuxing.hpp"
#include "logic/hardware/lines/line-output.hpp"
#include "logic/hardware/lines/wired-or-line.hpp"
#include "logic/hardware/mapper-chips/mmc3/mmc3.hpp"
#include "logic/hardware/mouse-yuxing/mouse-yuxing.hpp"
#include "logic/hardware/pad-2a03/pad-2a03.hpp"
#include "logic/hardware/ppu-2c02/ppu-2c02.hpp"
#include "logic/hardware/printer-escp/printer-escp.hpp"
#include "logic/hardware/ram/ram-block.hpp"
#include "logic/hardware/scheduler/master-scheduler.hpp"
#include "logic/hardware/speech-lpc/speech-lpc.hpp"
#include "logic/hardware/tape-deck/tape-deck.hpp"

namespace ember::bootstrap {

/** 裕兴学习机（Dendy 制式）。 */
class YuxingMachine final : public contracts::hardware::Machine, private contracts::hardware::MachineDebugger {
 public:
  /** 主时钟频率（赫兹）。 */
  static constexpr double MASTER_CLOCK_HZ = 26'601'712.0;
  /** 每个 CPU 周期的主时钟周期数。 */
  static constexpr contracts::hardware::MasterTime CPU_TICKS_PER_CYCLE = 15;
  /** 每个 PPU 点的主时钟周期数。 */
  static constexpr contracts::hardware::MasterTime PPU_TICKS_PER_DOT = 5;
  /** 每帧的行数与 vblank 起始行。 */
  static constexpr int SCANLINES_PER_FRAME = 312;
  static constexpr int VBLANK_LINE = 291;
  /** 软驱控制器状态机每一步的主时钟周期数（约 2.4 微秒）。 */
  static constexpr contracts::hardware::MasterTime FDC_STEP_TICKS = 64;
  /** 软驱控制器轮询一个驱动器占的步数（约 220 微秒）。 */
  static constexpr unsigned FDC_POLL_STEPS = 91;
  /** 打印机没有数据多久之后输出当前页（6 秒，主时钟周期数）。 */
  static constexpr contracts::hardware::MasterTime PRINTER_TIMEOUT_TICKS = 6 * 26'601'712;

  /**
   * @param rom ROM 的内容，大小必须是 1024K（不含 iNES 文件头）。
   * @param tapeDeck 磁带机：独立于主机的设备，由外面创建并在多台主机之间共用，主机通电时接上它；为空时主机自己带一台（测试用）。
   * @param floppyActivity 软驱马达开关变化时通知它，必须活到主机销毁；为空时不通知。
   * @param floppyDrive 软驱 A：独立于主机的设备，由外面创建并在多台主机之间共用，主机创建时接入、销毁时解除，盘片留在里面；为空时主机自己带一个（测试用）。
   * @param printerOutput 打印机输出的页交给它，必须活到主机销毁；为空时不接打印机。
   * @throws shared::CoreError hardware.invalidArgument：ROM 大小不对。
   */
  explicit YuxingMachine(std::vector<std::uint8_t> rom, std::shared_ptr<logic::hardware::TapeDeck> tapeDeck = nullptr, contracts::hardware::FloppyActivity* floppyActivity = nullptr,
                         std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive = nullptr, contracts::hardware::PrinterOutput* printerOutput = nullptr);
  ~YuxingMachine() override;

  YuxingMachine(const YuxingMachine&) = delete;
  YuxingMachine& operator=(const YuxingMachine&) = delete;

  void powerOn() override;
  void powerOff() override;
  void softReset() override;
  [[nodiscard]] bool isPoweredOn() const override { return poweredOn_; }
  void run(contracts::hardware::MasterTime ticks) override;
  void runFrame() override;
  [[nodiscard]] contracts::hardware::FrameBuffer frame() const override { return ppu_.frame(); }
  [[nodiscard]] double framesPerSecond() const override { return MASTER_CLOCK_HZ / (static_cast<double>(PPU_TICKS_PER_DOT) * 341.0 * SCANLINES_PER_FRAME); }
  [[nodiscard]] unsigned audioSampleRate() const override { return logic::hardware::Apu2a03::SAMPLE_RATE; }
  void takeAudio(std::vector<float>& samples) override;
  void setButtons(unsigned port, std::uint8_t buttons) override;
  [[nodiscard]] unsigned padPortCount() const override { return static_cast<unsigned>(joypads_.size()); }
  void input(const contracts::hardware::InputEvent& event) override;
  /** 裕兴的键盘属于机器。 */
  [[nodiscard]] bool keyboardIsMachineInput() const override { return true; }
  /** 裕兴的鼠标接在手柄口的信号线上，属于机器。 */
  [[nodiscard]] bool hasMouse() const override { return true; }
  [[nodiscard]] contracts::hardware::CpuRegisters cpuRegisters() const override { return cpu_.registers(); }
  [[nodiscard]] const contracts::hardware::Clock& clock() const override { return scheduler_; }
  contracts::hardware::CartridgeSlot& cartridgeSlot() override { return slot_; }
  contracts::hardware::IrqOutput& cartridgeIrq() override { return cartridgeIrq_; }
  contracts::hardware::MachineDebugger* debugger() override { return this; }
  [[nodiscard]] unsigned floppyDriveCount() const override { return 1; }
  contracts::hardware::FloppyDrive* floppyDrive(unsigned index) override { return index == 0 ? floppy_.get() : nullptr; }
  contracts::hardware::PrinterControl* printer() override { return printer_.get(); }
  contracts::hardware::TapeControl* tape() override { return tapeDeck_.get(); }

  // 测试与调试用
  [[nodiscard]] logic::hardware::Ppu2c02& ppu() { return ppu_; }
  [[nodiscard]] const logic::hardware::Ppu2c02& ppu() const { return ppu_; }
  [[nodiscard]] logic::hardware::Apu2a03& apu() { return apu_; }
  [[nodiscard]] YuxingBoard& board() { return board_; }
  [[nodiscard]] const YuxingBoard& board() const { return board_; }
  [[nodiscard]] logic::hardware::KeyboardYuxing& keyboard() { return keyboard_; }
  [[nodiscard]] logic::hardware::MouseYuxing& mouse() { return mouse_; }
  [[nodiscard]] logic::hardware::SpeechLpc& speech() { return speech_; }
  /** 2K 内部 RAM。 */
  [[nodiscard]] const contracts::hardware::StorageRegion& internalRam() const { return ram_; }
  /** 不经 CPU 读 CPU 总线，读不改变状态。 */
  std::uint8_t peekCpuBus(std::uint16_t address) { return cpuBus_.peek(address); }

 private:
  /** 软驱控制器的中断不接 CPU：程序查询状态。 */
  class IgnoredInterrupts final : public contracts::hardware::InterruptSink {
   public:
    void raise(std::uint8_t /*sourceMask*/) override {}
  };

  /** $4016-$4017：在手柄口与 APU 帧计数器之上，加上鼠标的数据位（位 0）、磁带（写 $4016 的位 0 是送给磁带的信号，读 $4016 的位 1 是磁带送回的信号）与打印机。 */
  class InputPort final : public contracts::hardware::BusDevice {
   public:
    InputPort(contracts::hardware::BusDevice& padApu, contracts::hardware::TapeLine& tape, logic::hardware::MouseYuxing& mouse, YuxingPrinterPort& printer)
        : padApu_(padApu), tape_(tape), mouse_(mouse), printer_(printer) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    contracts::hardware::BusDevice& padApu_;
    contracts::hardware::TapeLine& tape_;
    logic::hardware::MouseYuxing& mouse_;
    YuxingPrinterPort& printer_;
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

  /** 把鼠标事件换算成鼠标的位移与按键。 */
  void applyMouse(const contracts::hardware::MouseInput& mouse);

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
  std::array<logic::hardware::Joypad, logic::hardware::Pad2a03::PORT_COUNT> joypads_;
  /** 磁带机：独立于主机的设备；主机通电时才绑定自己的调度器。 */
  std::shared_ptr<logic::hardware::TapeDeck> tapeDeck_;
  /** 虚拟针式打印机；没有输出方时为空。 */
  std::unique_ptr<logic::hardware::PrinterEscp> printer_;
  /** $4016 上的打印机接口。 */
  YuxingPrinterPort printerPort_;
  /** $4016-$4017：手柄口与 APU 帧计数器共用，写入同时转给两边。 */
  PadApuPort padApuPort_;
  logic::hardware::KeyboardYuxing keyboard_;
  logic::hardware::MouseYuxing mouse_;
  InputPort inputPort_;
  IgnoredInterrupts ignoredInterrupts_;
  std::shared_ptr<logic::hardware::StandardFloppyDrive> floppy_;
  logic::hardware::FdcUpd765 fdc_;
  logic::hardware::SpeechLpc speech_;
  VoiceMixer voiceMixer_;
  logic::hardware::mapper_chips::Mmc3 mmc3_;
  YuxingBoard board_;
  logic::hardware::Cpu6502 cpu_;
  /** 断点、记录、代码数据标记与反汇编。 */
  CpuDebugKit debug_;

  /** 鼠标位移里还没有凑够 1 个单位的小数部分。 */
  double mouseRemainderX_{0};
  double mouseRemainderY_{0};
  bool poweredOn_{false};
};

}  // namespace ember::bootstrap
