/*
 * ------------------------------------------------------------------------
 * 名称：cpu-debug-kit.hpp
 * 说明：以 6502 为 CPU 的主机共用的调试部件：断点（含条件）、记录、事件记录、调用栈与子程序统计、代码数据标记、反汇编、CPU 状态的读写，以及内存空间访问的公共检查。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：主机型号（fc、sb2000、bbg 等）各自拥有一个，只负责把自己的内存空间与芯片状态接到调试接口上；
 *       断点与记录靠 CPU 总线上的探针与每条指令之前的钩子生效，调用栈靠 CPU 报告的 JSR、RTS、RTI 与中断，规则见 contracts/hardware/machine-debugger.hpp。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/hardware/machine-debugger.hpp"
#include "logic/hardware/bus/mapped-bus.hpp"
#include "logic/hardware/cpu-6502/cpu-6502.hpp"
#include "logic/hardware/debug/breakpoint-table.hpp"
#include "logic/hardware/debug/call-tracker.hpp"
#include "logic/hardware/debug/code-data-log.hpp"
#include "logic/hardware/debug/cpu-disassembler.hpp"
#include "logic/hardware/debug/event-log.hpp"
#include "logic/hardware/debug/expression.hpp"
#include "logic/hardware/debug/probe-hub.hpp"
#include "logic/hardware/debug/recorder-table.hpp"

namespace ember::bootstrap {

namespace debugging {

/** 检查访问范围，越界时抛出 hardware.spaceOutOfRange。 */
void checkRange(std::string_view space, std::size_t size, std::uint32_t address, std::size_t length);

/** 抛出 hardware.unknownSpace。 */
[[noreturn]] void unknownSpace(std::string_view space);

/** 组一项状态。 */
contracts::hardware::StateField field(const char* name, std::int64_t value, const char* description);

}  // namespace debugging

/** 以 6502 为 CPU 的主机共用的调试部件。 */
class CpuDebugKit final : private contracts::hardware::InterruptObserver, private contracts::hardware::CallObserver {
 public:
  /** 取当前扫描位置的函数，由主机型号按自己的 PPU 提供。 */
  using VideoPositionFunction = std::function<contracts::hardware::VideoPosition()>;

  /**
   * @param bus CPU 总线，探针装在它上面。
   * @param cpu CPU，本对象登记为它的中断与调用观察者。
   */
  CpuDebugKit(logic::hardware::CpuAddressBus& bus, logic::hardware::Cpu6502& cpu);

  CpuDebugKit(const CpuDebugKit&) = delete;
  CpuDebugKit& operator=(const CpuDebugKit&) = delete;

  /** 设置取扫描位置的函数（事件记录与断点条件里的 frame、scanline、dot 用它）；没有设置时位置恒为 0。 */
  void setVideoPosition(VideoPositionFunction position) { position_ = std::move(position); }

  // ------------------------------------------------ 运行时的钩子

  /**
   * 运行一条指令之前调用：这个地址上有执行断点就返回 true，主机应该停下（指令没有执行）；
   * 刚命中的地址在下一次运行时越过一次，不重复命中。
   */
  bool stopAtExecuteBreakpoint();

  /** 即将执行一条指令：通知记录表、事件记录与代码数据标记。 */
  void beforeInstruction();

  /** 已经有断点命中，等着主机取走。 */
  [[nodiscard]] bool hasPendingHit() const { return breakpoints_.hasPending(); }

  /** 上电、复位、改 CPU 寄存器、单步之前：清掉“要越过的断点地址”与未取走的命中。 */
  void clearStopState();

  /** 上电与复位时：清掉未取走的命中。 */
  void discardPendingHit() { breakpoints_.discardPending(); }

  /** 单步与按授权量运行共用的循环里，机型各自提供的操作。 */
  struct StepOperations {
    /** 执行恰好一条指令（含随后响应的中断序列）并让 PPU 追赶到当前时间。 */
    std::function<void()> instruction;
    /** CPU 周期数。 */
    std::function<std::uint64_t()> cycles;
    /** 已完成的帧数。 */
    std::function<std::uint64_t()> frames;
    /** 当前的扫描线。 */
    std::function<int()> line;
  };

  /**
   * 单步或按授权量运行：按单位运行 count 个；stopAtBreakpoints 为 false 时忽略断点（照常进记录），为 true 时断点生效，命中后提前返回。
   */
  contracts::hardware::StepResult step(contracts::hardware::StepUnit unit, std::uint64_t count, bool stopAtBreakpoints, const StepOperations& operations);

  /** 不带副作用地读 CPU 总线。 */
  [[nodiscard]] std::uint8_t peek(std::uint16_t address) const { return bus_.peek(address); }

  /** 构造时暂停断点（alsoRecording 为 true 时连记录一起暂停），析构时恢复：让调试访问不触发读写断点，调试写内存也不进记录。 */
  class Suspend {
   public:
    Suspend(CpuDebugKit& kit, bool alsoRecording);
    ~Suspend();
    Suspend(const Suspend&) = delete;
    Suspend& operator=(const Suspend&) = delete;

   private:
    CpuDebugKit& kit_;
    logic::hardware::ProbeHub::Suspension before_;
  };

  // ------------------------------------------------ MachineDebugger 里与 CPU 总线相关的部分

  contracts::hardware::Breakpoint addBreakpoint(contracts::hardware::BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition);
  std::optional<contracts::hardware::Breakpoint> updateBreakpoint(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition);
  bool removeBreakpoint(std::uint32_t id);
  std::size_t clearBreakpoints();
  [[nodiscard]] std::vector<contracts::hardware::Breakpoint> breakpoints() const { return breakpoints_.list(); }
  std::optional<contracts::hardware::BreakpointHit> takeBreakpointHit() { return breakpoints_.take(); }

  std::uint32_t startRecorder(const contracts::hardware::RecorderSpec& spec);
  contracts::hardware::RecorderOutput stopRecorder(std::uint32_t id);
  [[nodiscard]] contracts::hardware::RecorderSlice readRecorder(std::uint32_t id, std::uint64_t from, std::uint32_t maxLines) const { return recorders_.read(id, from, maxLines); }

  [[nodiscard]] std::vector<contracts::hardware::DisassembledInstruction> disassemble(std::uint16_t address, std::uint32_t count);

  void startCdl(std::size_t size, contracts::hardware::CdlResolver resolver) { cdl_.start(size, std::move(resolver)); }
  void stopCdl() { cdl_.stop(); }
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> cdlFlags() const;

  [[nodiscard]] std::vector<contracts::hardware::CallFrame> callStack() const { return tracker_.stack(); }
  void startProfiler();
  void stopProfiler();
  [[nodiscard]] std::optional<contracts::hardware::ProfileReport> profile() const;

  void startEventLog(std::uint32_t maxEvents);
  void stopEventLog();
  [[nodiscard]] std::optional<contracts::hardware::EventLogOutput> eventLog(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents) const;

  /** CPU 状态模块 cpu 的读。 */
  [[nodiscard]] std::vector<contracts::hardware::StateField> readCpuState() const;

  /** CPU 状态模块 cpu 的写：先检查，再整体写入。 */
  void writeCpuState(std::span<const contracts::hardware::StateField> fields);

 private:
  /** 断点条件求值时读主机状态。 */
  class Context final : public logic::hardware::ExpressionContext {
   public:
    explicit Context(const CpuDebugKit& kit) : kit_(kit) {}
    [[nodiscard]] contracts::hardware::CpuRegisters registers() const override { return kit_.cpu_.registers(); }
    [[nodiscard]] std::uint8_t peek(std::uint16_t address) const override { return kit_.bus_.peek(address); }
    [[nodiscard]] contracts::hardware::VideoPosition position() const override { return kit_.currentPosition(); }

   private:
    const CpuDebugKit& kit_;
  };

  /** 总线访问写成事件的探针。 */
  class EventProbe final : public contracts::hardware::AccessProbe {
   public:
    explicit EventProbe(CpuDebugKit& kit) : kit_(kit) {}
    void onAccess(contracts::hardware::AccessKind kind, std::uint16_t address, std::uint8_t value) override { kit_.recordAccessEvent(kind, address, value); }

   private:
    CpuDebugKit& kit_;
  };

  // InterruptObserver
  void onInterrupt(contracts::hardware::InterruptKind kind, std::uint16_t returnAddress, std::uint16_t handler) override;
  // CallObserver
  void onCall(std::uint16_t callAddress, std::uint16_t target) override;
  void onReturn(bool fromInterrupt) override;
  void onResetBegin() override;
  void onReset() override;

  /** 有读写断点、I/O 记录或事件记录时在 CPU 总线上安装探针，没有时去掉。 */
  void refreshProbe();

  [[nodiscard]] contracts::hardware::VideoPosition currentPosition() const { return position_ ? position_() : contracts::hardware::VideoPosition{}; }

  /** 记一次寄存器访问事件。 */
  void recordAccessEvent(contracts::hardware::AccessKind kind, std::uint16_t address, std::uint8_t value);

  logic::hardware::CpuAddressBus& bus_;
  logic::hardware::Cpu6502& cpu_;
  Context context_;
  logic::hardware::BreakpointTable breakpoints_;
  /** 无副作用地读 CPU 总线。 */
  logic::hardware::PeekFunction peek_;
  logic::hardware::RecorderTable recorders_;
  logic::hardware::EventLog events_;
  EventProbe eventProbe_;
  logic::hardware::ProbeHub probeHub_;
  logic::hardware::CodeDataLog cdl_;
  logic::hardware::CallTracker tracker_;
  VideoPositionFunction position_;
  /** 刚命中执行断点的地址：下一次运行先越过它，不再重复命中。 */
  std::optional<std::uint16_t> skipExecuteAt_;
  /** 刚复位完成、第一条指令还没有执行，等着查复位断点。 */
  bool resetPending_{false};
  /** 最近一条指令开始时的 PC，事件记录说明是哪条指令访问的（只在事件记录进行时更新）。 */
  std::uint16_t lastPc_{0};
};

}  // namespace ember::bootstrap
