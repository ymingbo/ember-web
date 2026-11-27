/*
 * ------------------------------------------------------------------------
 * 名称：cpu-debug-kit.cpp
 * 说明：以 6502 为 CPU 的主机共用的调试部件的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/common/cpu-debug-kit.hpp"

#include <cstdio>
#include <string>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "logic/hardware/debug/io-address.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;

namespace debugging {

void checkRange(std::string_view space, std::size_t size, std::uint32_t address, std::size_t length) {
  if (address > size || length > size - address) {
    throw shared::CoreError(hw::ERROR_SPACE_OUT_OF_RANGE, "空间 " + std::string(space) + " 的大小是 " + std::to_string(size) + " 字节，访问 " + std::to_string(address) + " 起的 " + std::to_string(length) + " 字节超出范围");
  }
}

void unknownSpace(std::string_view space) { throw shared::CoreError(hw::ERROR_UNKNOWN_SPACE, "没有这个内存空间：" + std::string(space)); }

hw::StateField field(const char* name, std::int64_t value, const char* description) { return hw::StateField{name, value, description}; }

}  // namespace debugging

using debugging::field;

CpuDebugKit::CpuDebugKit(logic::hardware::CpuAddressBus& bus, logic::hardware::Cpu6502& cpu)
    : bus_(bus),
      cpu_(cpu),
      context_(*this),
      peek_([this](std::uint16_t address) { return bus_.peek(address); }),
      recorders_(peek_),
      eventProbe_(*this),
      probeHub_(breakpoints_, recorders_, events_, eventProbe_) {
  breakpoints_.setContext(&context_);
  cpu_.setInterruptObserver(this);
  cpu_.setCallObserver(this);
}

bool CpuDebugKit::stopAtExecuteBreakpoint() {
  // 复位断点：复位完成后的第一次查询，即第一条指令执行之前。
  if (resetPending_) {
    resetPending_ = false;
    if (breakpoints_.hasEvent() && breakpoints_.hitsEvent(hw::BreakpointKind::Reset, cpu_.registers().pc)) return true;
  }
  if (!breakpoints_.hasExecute()) return false;
  const std::uint16_t pc = cpu_.registers().pc;
  const bool skip = skipExecuteAt_ == pc;
  skipExecuteAt_.reset();
  if (skip || !breakpoints_.hitsExecute(pc)) return false;
  skipExecuteAt_ = pc;
  return true;
}

void CpuDebugKit::beforeInstruction() {
  // 单步不查断点，复位后第一条指令一旦执行，复位断点就不再有效。
  resetPending_ = false;
  const bool recording = recorders_.needsInstructionHook();
  const bool eventing = events_.active();
  if (!recording && !eventing && !cdl_.active()) return;
  const hw::CpuRegisters registers = cpu_.registers();
  if (eventing) lastPc_ = registers.pc;
  if (recording) recorders_.onInstruction(registers);
  if (cdl_.active()) cdl_.onInstruction(registers, peek_);
}

void CpuDebugKit::clearStopState() {
  skipExecuteAt_.reset();
  breakpoints_.discardPending();
}

hw::StepResult CpuDebugKit::step(hw::StepUnit unit, std::uint64_t count, bool stopAtBreakpoints, const StepOperations& operations) {
  hw::StepResult result;
  // 单步忽略断点；按授权量运行时断点生效，不清“要越过的地址”，继续运行才能越过刚命中的断点。
  std::optional<Suspend> suspend;
  if (!stopAtBreakpoints) {
    suspend.emplace(*this, false);
    clearStopState();
  }
  const std::uint64_t startCycles = operations.cycles();
  const std::uint64_t startFrames = operations.frames();
  // 执行一条指令；断点要求停下时返回 false。
  const auto instruction = [&] {
    if (stopAtBreakpoints && stopAtExecuteBreakpoint()) return false;
    beforeInstruction();
    operations.instruction();
    ++result.instructions;
    return !(stopAtBreakpoints && hasPendingHit());
  };
  switch (unit) {
    case hw::StepUnit::Instruction:
      for (std::uint64_t done = 0; done < count; ++done) {
        if (!instruction()) break;
      }
      break;
    case hw::StepUnit::Cycle:
      while (operations.cycles() - startCycles < count) {
        if (!instruction()) break;
      }
      break;
    case hw::StepUnit::Scanline: {
      int last = operations.line();
      for (std::uint64_t remaining = count; remaining > 0;) {
        if (!instruction()) break;
        if (operations.line() != last) {
          last = operations.line();
          --remaining;
        }
      }
      break;
    }
    case hw::StepUnit::Frame:
      while (operations.frames() - startFrames < count) {
        if (!instruction()) break;
      }
      break;
  }
  result.cycles = operations.cycles() - startCycles;
  result.frames = operations.frames() - startFrames;
  return result;
}

CpuDebugKit::Suspend::Suspend(CpuDebugKit& kit, bool alsoRecording) : kit_(kit), before_(kit.probeHub_.suspend(true, alsoRecording)) {}

CpuDebugKit::Suspend::~Suspend() { kit_.probeHub_.restore(before_); }

void CpuDebugKit::refreshProbe() { bus_.setProbe(probeHub_.needed() ? &probeHub_ : nullptr); }

hw::Breakpoint CpuDebugKit::addBreakpoint(hw::BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition) {
  const hw::Breakpoint added = breakpoints_.add(kind, first, last, condition);
  refreshProbe();
  return added;
}

std::optional<hw::Breakpoint> CpuDebugKit::updateBreakpoint(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition) {
  auto updated = breakpoints_.update(id, enabled, condition);
  refreshProbe();
  return updated;
}

bool CpuDebugKit::removeBreakpoint(std::uint32_t id) {
  const bool removed = breakpoints_.remove(id);
  refreshProbe();
  return removed;
}

std::size_t CpuDebugKit::clearBreakpoints() {
  const std::size_t removed = breakpoints_.clear();
  refreshProbe();
  return removed;
}

std::uint32_t CpuDebugKit::startRecorder(const hw::RecorderSpec& spec) {
  const std::uint32_t id = recorders_.start(spec);
  refreshProbe();
  return id;
}

hw::RecorderOutput CpuDebugKit::stopRecorder(std::uint32_t id) {
  hw::RecorderOutput output = recorders_.stop(id);
  refreshProbe();
  return output;
}

std::vector<hw::DisassembledInstruction> CpuDebugKit::disassemble(std::uint16_t address, std::uint32_t count) {
  std::vector<hw::DisassembledInstruction> list;
  list.reserve(count);
  const hw::CpuRegisters registers = cpu_.registers();
  std::uint16_t pc = address;
  for (std::uint32_t index = 0; index < count; ++index) {
    const logic::hardware::Disassembly code = logic::hardware::disassemble(pc, registers, peek_);
    std::string bytes;
    for (std::uint8_t offset = 0; offset < code.length; ++offset) {
      char text[4];
      std::snprintf(text, sizeof(text), "%02X", peek_(static_cast<std::uint16_t>(pc + offset)));
      if (offset > 0) bytes += ' ';
      bytes += text;
    }
    list.push_back({pc, std::move(bytes), code.text, code.unofficial});
    pc = static_cast<std::uint16_t>(pc + code.length);
  }
  return list;
}

std::optional<std::vector<std::uint8_t>> CpuDebugKit::cdlFlags() const {
  if (!cdl_.exists()) return std::nullopt;
  return cdl_.flags();
}

void CpuDebugKit::startProfiler() { tracker_.startProfile(cpu_.registers().cycles); }

void CpuDebugKit::stopProfiler() { tracker_.stopProfile(cpu_.registers().cycles); }

std::optional<hw::ProfileReport> CpuDebugKit::profile() const { return tracker_.profile(cpu_.registers().cycles); }

void CpuDebugKit::startEventLog(std::uint32_t maxEvents) {
  events_.start(maxEvents);
  refreshProbe();
}

void CpuDebugKit::stopEventLog() {
  events_.stop();
  refreshProbe();
}

std::optional<hw::EventLogOutput> CpuDebugKit::eventLog(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents) const {
  if (!events_.exists()) return std::nullopt;
  return events_.output(fromFrame, maxEvents, currentPosition().frame);
}

void CpuDebugKit::recordAccessEvent(hw::AccessKind kind, std::uint16_t address, std::uint8_t value) {
  if (!logic::hardware::isIoAddress(kind, address)) return;
  const hw::VideoPosition position = currentPosition();
  hw::DebugEvent event;
  event.frame = position.frame;
  event.scanline = position.scanline;
  event.dot = position.dot;
  event.kind = kind == hw::AccessKind::Read ? hw::EventKind::Read : hw::EventKind::Write;
  event.address = address;
  event.value = value;
  event.pc = lastPc_;
  event.cycles = cpu_.registers().cycles;
  events_.record(event);
}

void CpuDebugKit::onInterrupt(hw::InterruptKind kind, std::uint16_t returnAddress, std::uint16_t handler) {
  const hw::CpuRegisters registers = cpu_.registers();
  const hw::CallKind callKind = kind == hw::InterruptKind::Brk ? hw::CallKind::Brk : (kind == hw::InterruptKind::Nmi ? hw::CallKind::Nmi : hw::CallKind::Irq);
  // BRK 的返回地址在 BRK 之后两个字节；IRQ 与 NMI 打断的是还没有执行的那条指令。
  const auto callAddress = static_cast<std::uint16_t>(kind == hw::InterruptKind::Brk ? returnAddress - 2 : returnAddress);
  tracker_.enter(callKind, callAddress, handler, returnAddress, registers.sp, 3, registers.cycles);
  // 调试访问（单步）期间不查断点，与读写断点一样。
  if (breakpoints_.hasEvent() && !probeHub_.breakpointsSuspended()) {
    breakpoints_.hitsEvent(kind == hw::InterruptKind::Brk ? hw::BreakpointKind::Brk : (kind == hw::InterruptKind::Nmi ? hw::BreakpointKind::Nmi : hw::BreakpointKind::Irq), handler);
  }
  if (recorders_.recordsIrq()) recorders_.onInterrupt(kind, returnAddress, handler, registers.cycles);
  if (events_.active()) {
    const hw::VideoPosition position = currentPosition();
    hw::DebugEvent event;
    event.frame = position.frame;
    event.scanline = position.scanline;
    event.dot = position.dot;
    event.kind = kind == hw::InterruptKind::Brk ? hw::EventKind::Brk : (kind == hw::InterruptKind::Nmi ? hw::EventKind::Nmi : hw::EventKind::Irq);
    event.address = handler;
    event.pc = returnAddress;
    event.cycles = registers.cycles;
    events_.record(event);
  }
}

void CpuDebugKit::onCall(std::uint16_t callAddress, std::uint16_t target) {
  const hw::CpuRegisters registers = cpu_.registers();
  // JSR 是三字节指令，调用返回后执行它后面的那一条。
  tracker_.enter(hw::CallKind::Jsr, callAddress, target, static_cast<std::uint16_t>(callAddress + 3), registers.sp, 2, registers.cycles);
}

void CpuDebugKit::onReturn(bool /*fromInterrupt*/) {
  const hw::CpuRegisters registers = cpu_.registers();
  tracker_.leave(registers.sp, registers.cycles);
}

void CpuDebugKit::onResetBegin() { tracker_.beginReset(cpu_.registers().cycles); }

void CpuDebugKit::onReset() {
  tracker_.reset(cpu_.registers().cycles);
  resetPending_ = true;
}

std::vector<hw::StateField> CpuDebugKit::readCpuState() const {
  const hw::CpuRegisters registers = cpu_.registers();
  return {field("pc", registers.pc, "程序计数器，指向下一条要执行的指令"),
          field("a", registers.a, "累加器"),
          field("x", registers.x, "X 变址寄存器"),
          field("y", registers.y, "Y 变址寄存器"),
          field("sp", registers.sp, "栈指针，栈在 $0100-$01FF"),
          field("status", registers.status, "状态寄存器（N V - - D I Z C，位 5 恒为 1，位 4 恒为 0）"),
          field("cycles", static_cast<std::int64_t>(registers.cycles), "自上电以来的 CPU 周期数（只读）")};
}

void CpuDebugKit::writeCpuState(std::span<const hw::StateField> fields) {
  hw::CpuRegisters registers = cpu_.registers();
  for (const hw::StateField& item : fields) {
    const auto inRange = [&](std::int64_t maximum) {
      if (item.value < 0 || item.value > maximum) throw shared::CoreError(hw::ERROR_INVALID_STATE, "cpu." + item.name + " 的取值必须在 0 到 " + std::to_string(maximum) + " 之间");
    };
    if (item.name == "pc") {
      inRange(0xFFFF);
      registers.pc = static_cast<std::uint16_t>(item.value);
    } else if (item.name == "a" || item.name == "x" || item.name == "y" || item.name == "sp" || item.name == "status") {
      inRange(0xFF);
      const auto value = static_cast<std::uint8_t>(item.value);
      if (item.name == "a") registers.a = value;
      else if (item.name == "x") registers.x = value;
      else if (item.name == "y") registers.y = value;
      else if (item.name == "sp") registers.sp = value;
      else registers.status = value;
    } else {
      throw shared::CoreError(hw::ERROR_INVALID_STATE, "cpu 没有可修改的状态项：" + item.name);
    }
  }
  cpu_.setRegisters(registers);
  skipExecuteAt_.reset();
}

}  // namespace ember::bootstrap
