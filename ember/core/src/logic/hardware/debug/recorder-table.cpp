/*
 * ------------------------------------------------------------------------
 * 名称：recorder-table.cpp
 * 说明：记录表的实现，见 recorder-table.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/debug/recorder-table.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "logic/hardware/debug/io-address.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

namespace {

/** 按格式写成文本。 */
template <typename... Args>
std::string format(const char* pattern, Args... args) {
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer), pattern, args...);
  return buffer;
}

const char* interruptName(hw::InterruptKind kind) {
  switch (kind) {
    case hw::InterruptKind::Irq:
      return "IRQ";
    case hw::InterruptKind::Nmi:
      return "NMI";
    case hw::InterruptKind::Brk:
      return "BRK";
  }
  return "";
}

}  // namespace

RecorderTable::RecorderTable(PeekFunction peek) : peek_(std::move(peek)) {}

std::uint32_t RecorderTable::start(const hw::RecorderSpec& spec) {
  if (recorders_.size() >= MAX_RECORDERS) throw shared::CoreError(hw::ERROR_RECORDER_LIMIT, "同时进行的记录最多 " + std::to_string(MAX_RECORDERS) + " 项");
  Recorder recorder;
  recorder.id = nextId_++;
  recorder.spec = spec;
  if (spec.kind == hw::RecorderKind::Instruction && spec.unique) recorder.seen.assign(0x10000, false);
  recorders_.push_back(std::move(recorder));
  refreshFlags();
  return recorders_.back().id;
}

hw::RecorderOutput RecorderTable::stop(std::uint32_t id) {
  const auto found = std::find_if(recorders_.begin(), recorders_.end(), [id](const Recorder& recorder) { return recorder.id == id; });
  if (found == recorders_.end()) throw shared::CoreError(hw::ERROR_NO_RECORDER, "没有编号为 " + std::to_string(id) + " 的记录");
  hw::RecorderOutput output;
  output.lines.assign(std::make_move_iterator(found->lines.begin()), std::make_move_iterator(found->lines.end()));
  output.truncated = found->truncated;
  output.dropped = found->dropped;
  recorders_.erase(found);
  refreshFlags();
  return output;
}

hw::RecorderSlice RecorderTable::read(std::uint32_t id, std::uint64_t from, std::uint32_t maxLines) const {
  const auto found = std::find_if(recorders_.begin(), recorders_.end(), [id](const Recorder& recorder) { return recorder.id == id; });
  if (found == recorders_.end()) throw shared::CoreError(hw::ERROR_NO_RECORDER, "没有编号为 " + std::to_string(id) + " 的记录");
  hw::RecorderSlice slice;
  slice.oldest = found->dropped;
  slice.total = found->dropped + found->lines.size();
  slice.truncated = found->truncated;
  slice.first = std::min(std::max(from, slice.oldest), slice.total);
  const std::uint64_t count = std::min<std::uint64_t>(maxLines, slice.total - slice.first);
  const auto begin = found->lines.begin() + static_cast<std::ptrdiff_t>(slice.first - slice.oldest);
  slice.lines.assign(begin, begin + static_cast<std::ptrdiff_t>(count));
  return slice;
}

void RecorderTable::clear() {
  recorders_.clear();
  refreshFlags();
}

void RecorderTable::refreshFlags() {
  needsInstruction_ = false;
  recordsIo_ = false;
  recordsIrq_ = false;
  for (const Recorder& recorder : recorders_) {
    if (recorder.spec.kind == hw::RecorderKind::Irq) recordsIrq_ = true;
    else needsInstruction_ = true;
    if (recorder.spec.kind == hw::RecorderKind::Io) recordsIo_ = true;
  }
}

bool RecorderTable::accepts(Recorder& recorder) {
  if (recorder.spec.ring || recorder.lines.size() < recorder.spec.maxLines) return true;
  recorder.truncated = true;
  return false;
}

void RecorderTable::append(Recorder& recorder, std::string line) {
  if (recorder.spec.ring && recorder.lines.size() >= recorder.spec.maxLines) {
    recorder.lines.pop_front();
    ++recorder.dropped;
  }
  recorder.lines.push_back(std::move(line));
}

std::string RecorderTable::instructionLine(const hw::CpuRegisters& registers) const {
  const Disassembly code = disassemble(registers.pc, registers, peek_);
  std::string bytes;
  for (std::uint8_t index = 0; index < code.length; ++index) {
    if (index > 0) bytes += ' ';
    bytes += format("%02X", peek_(static_cast<std::uint16_t>(registers.pc + index)));
  }
  return format("%04X  %-9s%c%-32sA:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu", registers.pc, bytes.c_str(), code.unofficial ? '*' : ' ', code.text.c_str(), registers.a, registers.x, registers.y,
                registers.status, registers.sp, static_cast<unsigned long long>(registers.cycles));
}

std::string RecorderTable::callLine(const hw::CpuRegisters& registers, const hw::RecorderSpec& spec) const {
  std::string line = format("%04X  A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu", registers.pc, registers.a, registers.x, registers.y, registers.status, registers.sp, static_cast<unsigned long long>(registers.cycles));
  if (spec.stackBytes > 0) {
    // 栈顶是 SP 之上的第一个字节，栈在 $0100-$01FF，到页尾回到页首。
    line += " | STACK";
    for (std::uint8_t index = 0; index < spec.stackBytes; ++index) line += format(" %02X", peek_(static_cast<std::uint16_t>(0x0100 | static_cast<std::uint8_t>(registers.sp + 1 + index))));
  }
  for (const hw::MemoryRange& range : spec.ranges) {
    line += format(" | %04X:", range.address);
    for (std::uint32_t index = 0; index < range.length; ++index) line += format(" %02X", peek_(static_cast<std::uint16_t>(range.address + index)));
  }
  return line;
}

void RecorderTable::onInstruction(const hw::CpuRegisters& registers) {
  lastPc_ = registers.pc;
  lastCycles_ = registers.cycles;
  for (Recorder& recorder : recorders_) {
    if (recorder.spec.kind == hw::RecorderKind::Instruction) {
      if (recorder.spec.unique) {
        if (recorder.seen[registers.pc]) continue;
        recorder.seen[registers.pc] = true;
      }
      if (accepts(recorder)) append(recorder, instructionLine(registers));
    } else if (recorder.spec.kind == hw::RecorderKind::Call && recorder.spec.address == registers.pc) {
      const bool skipped = std::any_of(recorder.spec.skip.begin(), recorder.spec.skip.end(), [this](const hw::ByteCondition& condition) { return peek_(condition.address) == condition.value; });
      if (!skipped && accepts(recorder)) append(recorder, callLine(registers, recorder.spec));
    }
  }
}

void RecorderTable::onInterrupt(hw::InterruptKind kind, std::uint16_t returnAddress, std::uint16_t handler, std::uint64_t cycles) {
  for (Recorder& recorder : recorders_) {
    if (recorder.spec.kind != hw::RecorderKind::Irq || !accepts(recorder)) continue;
    append(recorder, format("%s  return:%04X handler:%04X CYC:%llu", interruptName(kind), returnAddress, handler, static_cast<unsigned long long>(cycles)));
  }
}

void RecorderTable::onIo(hw::AccessKind kind, std::uint16_t address, std::uint8_t value) {
  if (!isIoAddress(kind, address)) return;
  for (Recorder& recorder : recorders_) {
    if (recorder.spec.kind != hw::RecorderKind::Io || !accepts(recorder)) continue;
    append(recorder, format("%c $%04X = %02X  PC:%04X CYC:%llu", kind == hw::AccessKind::Read ? 'R' : 'W', address, value, lastPc_, static_cast<unsigned long long>(lastCycles_)));
  }
}

}  // namespace ember::logic::hardware
