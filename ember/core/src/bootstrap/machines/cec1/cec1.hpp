/*
 * ------------------------------------------------------------------------
 * 名称：cec1.hpp
 * 说明：主机型号 cec1（中华学习机 CEC-I）的装配：6502（带十进制模式）、苹果 II 类视频生成器、键盘、
 *       一位扬声器、磁带口、模拟手柄与按键、内置软驱接口（5.25 英寸苹果格式）与主板。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：时钟：主时钟 14,318,180 Hz，CPU 14 分频（约 1.0227 MHz），每行 65 个 CPU 周期、
 *       每帧 262 行（约 60 Hz）；
 *       CPU 总线：整个 $0000-$FFFF 由主板译码（存储、ROM、软开关与 I/O，见 cec1-board.hpp）；
 *       没有 PPU、APU、中断源与卡带：整机机型没有卡带，Machine 契约要求的卡槽是一个空的、
 *       不挂载的卡槽；槽 1 的打印机接口卡接 ESC/P 打印机（没有打印机输出时不接）；
 *       磁带机与软驱 A 是独立于主机的设备，由外面创建并在多台主机之间共用，
 *       软驱接口的盘片是 143360 或 163840 字节的 .dsk 镜像；
 *       具体实现只在这里创建并连线，模块之间不互相引用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bootstrap/machines/cec1/cec1-board.hpp"
#include "bootstrap/machines/common/cpu-debug-kit.hpp"
#include "contracts/hardware/floppy-activity.hpp"
#include "contracts/hardware/machine.hpp"
#include "logic/hardware/apu-2a03/apu-2a03.hpp"
#include "logic/hardware/bus/mapped-bus.hpp"
#include "logic/hardware/cartridge-slot/standard-cartridge-slot.hpp"
#include "logic/hardware/cpu-6502/cpu-6502.hpp"
#include "logic/hardware/disk2/disk2.hpp"
#include "logic/hardware/floppy-drive/standard-floppy-drive.hpp"
#include "logic/hardware/keyboard-apple2/keyboard-apple2.hpp"
#include "logic/hardware/lines/line-output.hpp"
#include "logic/hardware/lines/wired-or-line.hpp"
#include "logic/hardware/printer-escp/printer-escp.hpp"
#include "logic/hardware/scheduler/master-scheduler.hpp"
#include "logic/hardware/speaker-1bit/speaker-1bit.hpp"
#include "logic/hardware/tape-deck/tape-deck.hpp"
#include "logic/hardware/video-apple2/video-apple2.hpp"

namespace ember::bootstrap {

/** 中华学习机 CEC-I。 */
class Cec1Machine final : public contracts::hardware::Machine, private contracts::hardware::MachineDebugger {
 public:
  /** 主时钟频率（赫兹）。 */
  static constexpr double MASTER_CLOCK_HZ = 14'318'180.0;
  /** 每个 CPU 周期的主时钟周期数。 */
  static constexpr contracts::hardware::MasterTime CPU_TICKS_PER_CYCLE = 14;
  /** 每行的 CPU 周期数。 */
  static constexpr contracts::hardware::MasterTime CYCLES_PER_LINE = 65;
  /** 软驱盘片上一个位的时间：4 个 CPU 周期（主时钟周期数，带小数）。 */
  static constexpr double DISK_BIT_TICKS = 4.0 * CPU_TICKS_PER_CYCLE;
  /** 马达关指令之后到真正停转的时间：1 秒（主时钟周期数）。 */
  static constexpr contracts::hardware::MasterTime DISK_MOTOR_OFF_TICKS = 14'318'180;
  /** 打印机一直没有新数据时，把当前页送出去的等待时间：6 秒（主时钟周期数）。 */
  static constexpr contracts::hardware::MasterTime PRINTER_TIMEOUT_TICKS = 6 * 14'318'180;

  /**
   * @param rom ROM 的内容，大小必须是 64K：U7 在前、U35 在后（不含文件头），
   * 或再接 256K 的汉字字库（128K 的 U33 在前、128K 的 U34 在后）。
   * @param tapeDeck 磁带机：独立于主机的设备，由外面创建并在多台主机之间共用，主机通电时接上它；
   * 为空时主机自己带一台（测试用）。
   * @param floppyActivity 软驱马达开关变化时通知它，必须活到主机销毁；为空时不通知。
   * @param floppyDrive 软驱 A：独立于主机的设备，由外面创建并在多台主机之间共用，主机创建时接入、
   * 销毁时解除，盘片留在里面；为空时主机自己带一个（测试用）。
   * @param printerOutput 槽 1 打印机接口卡接的打印机，打印出的页交给它，必须活到主机销毁；
   * 为空时不接打印机。
   * @throws shared::CoreError hardware.invalidArgument：ROM 大小不对。
   */
  explicit Cec1Machine(std::vector<std::uint8_t> rom, std::shared_ptr<logic::hardware::TapeDeck> tapeDeck = nullptr, contracts::hardware::FloppyActivity* floppyActivity = nullptr,
                       std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive = nullptr, contracts::hardware::PrinterOutput* printerOutput = nullptr);
  ~Cec1Machine() override;

  Cec1Machine(const Cec1Machine&) = delete;
  Cec1Machine& operator=(const Cec1Machine&) = delete;

  void powerOn() override;
  void powerOff() override;
  void softReset() override;
  [[nodiscard]] bool isPoweredOn() const override { return poweredOn_; }
  void run(contracts::hardware::MasterTime ticks) override;
  void runFrame() override;
  [[nodiscard]] contracts::hardware::FrameBuffer frame() const override { return video_.frame(); }
  [[nodiscard]] double framesPerSecond() const override { return MASTER_CLOCK_HZ / static_cast<double>(CPU_TICKS_PER_CYCLE * CYCLES_PER_LINE * logic::hardware::VideoApple2::SCANLINES_PER_FRAME); }
  [[nodiscard]] unsigned audioSampleRate() const override { return logic::hardware::Apu2a03::SAMPLE_RATE; }
  void takeAudio(std::vector<float>& samples) override;
  void setButtons(unsigned port, std::uint8_t buttons) override;
  [[nodiscard]] unsigned padPortCount() const override { return 1; }
  void input(const contracts::hardware::InputEvent& event) override;
  /** 中华学习机的键盘属于机器。 */
  [[nodiscard]] bool keyboardIsMachineInput() const override { return true; }
  [[nodiscard]] contracts::hardware::CpuRegisters cpuRegisters() const override { return cpu_.registers(); }
  [[nodiscard]] const contracts::hardware::Clock& clock() const override { return scheduler_; }
  contracts::hardware::CartridgeSlot& cartridgeSlot() override { return slot_; }
  contracts::hardware::IrqOutput& cartridgeIrq() override { return cartridgeIrq_; }
  contracts::hardware::MachineDebugger* debugger() override { return this; }
  [[nodiscard]] unsigned floppyDriveCount() const override { return 1; }
  contracts::hardware::FloppyDrive* floppyDrive(unsigned index) override { return index == 0 ? floppy_.get() : nullptr; }
  contracts::hardware::TapeControl* tape() override { return tapeDeck_.get(); }
  contracts::hardware::PrinterControl* printer() override { return printer_.get(); }

  // 测试与调试用
  [[nodiscard]] Cec1Board& board() { return board_; }
  [[nodiscard]] const Cec1Board& board() const { return board_; }
  [[nodiscard]] logic::hardware::KeyboardApple2& keyboard() { return keyboard_; }
  [[nodiscard]] logic::hardware::Disk2& disk() { return disk_; }
  [[nodiscard]] logic::hardware::VideoApple2& video() { return video_; }
  /** 不经 CPU 读 CPU 总线，读不改变状态。 */
  std::uint8_t peekCpuBus(std::uint16_t address) { return cpuBus_.peek(address); }

 private:
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
  /** 低分辨率图形的 16 种颜色（没有 64 色调色板）。 */
  [[nodiscard]] std::vector<std::uint32_t> colorTable() const override;

  logic::hardware::MasterScheduler scheduler_;
  logic::hardware::WiredOrIrqLine irq_;
  logic::hardware::WiredOrIrqLine nmi_;
  logic::hardware::WiredOrRdyLine rdy_;
  /** 卡槽的 IRQ 引脚（整机机型没有卡带，只为满足 Machine 契约）。 */
  logic::hardware::LineIrqOutput cartridgeIrq_;
  /** 空卡槽：不挂载到任何总线。 */
  logic::hardware::StandardCartridgeSlot slot_;
  logic::hardware::CpuAddressBus cpuBus_;

  std::shared_ptr<logic::hardware::StandardFloppyDrive> floppy_;
  /** 磁带机：独立于主机，通电时接上。 */
  std::shared_ptr<logic::hardware::TapeDeck> tapeDeck_;
  logic::hardware::KeyboardApple2 keyboard_;
  logic::hardware::Speaker1Bit speaker_;
  /** 没有接打印机时的并口。 */
  contracts::hardware::NullParallelPort nullParallel_;
  /** 槽 1 的打印机；没有打印机输出时为空。 */
  std::unique_ptr<logic::hardware::PrinterEscp> printer_;
  contracts::hardware::ParallelPort& parallel_;
  logic::hardware::Disk2 disk_;
  Cec1Board board_;
  logic::hardware::VideoApple2 video_;
  logic::hardware::Cpu6502 cpu_;
  /** 断点、记录、代码数据标记与反汇编。 */
  CpuDebugKit debug_;

  bool poweredOn_{false};
};

}  // namespace ember::bootstrap
