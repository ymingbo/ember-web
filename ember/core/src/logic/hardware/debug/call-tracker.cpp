/*
 * ------------------------------------------------------------------------
 * 名称：call-tracker.cpp
 * 说明：调用跟踪的实现，见 call-tracker.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-24
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/debug/call-tracker.hpp"

#include <algorithm>

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

void CallTracker::enter(hw::CallKind kind, std::uint16_t callAddress, std::uint16_t targetAddress, std::uint16_t returnAddress, std::uint8_t stackPointer, int stackBytes, std::uint64_t cycles) {
  const int stackBefore = static_cast<int>(stackPointer) + stackBytes;
  // 栈上已经没有返回地址的过期帧（程序改过栈指针，或用 RTS 当跳转）先弹掉。
  while (!frames_.empty() && frames_.back().stackBefore <= stackBefore) closeTop(cycles);
  if (frames_.size() >= MAX_DEPTH) frames_.erase(frames_.begin());
  Frame frame;
  frame.info = hw::CallFrame{kind, callAddress, targetAddress, returnAddress, stackPointer};
  frame.stackBefore = stackBefore;
  frame.entryCycles = cycles;
  frames_.push_back(frame);
}

void CallTracker::leave(std::uint8_t stackPointer, std::uint64_t cycles) {
  while (!frames_.empty() && frames_.back().stackBefore <= static_cast<int>(stackPointer)) closeTop(cycles);
}

void CallTracker::closeTop(std::uint64_t cycles) {
  const Frame frame = frames_.back();
  frames_.pop_back();
  if (!profiling_) return;
  const std::uint64_t inclusive = cycles > frame.entryCycles ? cycles - frame.entryCycles : 0;
  const std::uint64_t exclusive = inclusive > frame.childCycles ? inclusive - frame.childCycles : 0;
  const auto [found, inserted] = table_.try_emplace(frame.info.targetAddress);
  if (inserted) {
    found->second.kind = frame.info.kind;
    found->second.address = frame.info.targetAddress;
  }
  found->second.calls += 1;
  found->second.inclusiveCycles += inclusive;
  found->second.exclusiveCycles += exclusive;
  if (frames_.empty()) rootChildCycles_ += inclusive;
  else frames_.back().childCycles += inclusive;
}

void CallTracker::beginReset(std::uint64_t cycles) {
  while (!frames_.empty()) closeTop(cycles);
  if (profiling_) accumulatedCycles_ += cycles > rootEntryCycles_ ? cycles - rootEntryCycles_ : 0;
}

void CallTracker::reset(std::uint64_t cycles) {
  frames_.clear();
  if (profiling_) rootEntryCycles_ = cycles;
}

std::vector<hw::CallFrame> CallTracker::stack() const {
  std::vector<hw::CallFrame> list;
  list.reserve(frames_.size());
  for (auto frame = frames_.rbegin(); frame != frames_.rend(); ++frame) list.push_back(frame->info);
  return list;
}

void CallTracker::startProfile(std::uint64_t cycles) {
  table_.clear();
  stopped_.reset();
  profiling_ = true;
  hasProfile_ = true;
  accumulatedCycles_ = 0;
  rootEntryCycles_ = cycles;
  rootChildCycles_ = 0;
  for (Frame& frame : frames_) {
    frame.entryCycles = cycles;
    frame.childCycles = 0;
  }
}

void CallTracker::stopProfile(std::uint64_t cycles) {
  if (!profiling_) return;
  stopped_ = buildReport(cycles, false);
  profiling_ = false;
}

std::optional<hw::ProfileReport> CallTracker::profile(std::uint64_t cycles) const {
  if (profiling_) return buildReport(cycles, true);
  return hasProfile_ ? stopped_ : std::nullopt;
}

hw::ProfileReport CallTracker::buildReport(std::uint64_t cycles, bool active) const {
  auto table = table_;
  // 还在栈上的帧按此刻返回计：从最里层往外，里层占去的周期算外层的下层调用。
  std::uint64_t innerCycles = 0;
  for (std::size_t index = frames_.size(); index-- > 0;) {
    const Frame& frame = frames_[index];
    const std::uint64_t inclusive = cycles > frame.entryCycles ? cycles - frame.entryCycles : 0;
    const std::uint64_t known = frame.childCycles + innerCycles;
    const auto [found, inserted] = table.try_emplace(frame.info.targetAddress);
    if (inserted) {
      found->second.kind = frame.info.kind;
      found->second.address = frame.info.targetAddress;
    }
    found->second.calls += 1;
    found->second.inclusiveCycles += inclusive;
    found->second.exclusiveCycles += inclusive > known ? inclusive - known : 0;
    innerCycles = inclusive;
  }

  hw::ProfileReport report;
  report.active = active;
  report.totalCycles = accumulatedCycles_ + (cycles > rootEntryCycles_ ? cycles - rootEntryCycles_ : 0);
  hw::ProfileEntry root;
  root.kind = hw::CallKind::Root;
  root.calls = 1;
  root.inclusiveCycles = report.totalCycles;
  const std::uint64_t rootKnown = rootChildCycles_ + innerCycles;
  root.exclusiveCycles = report.totalCycles > rootKnown ? report.totalCycles - rootKnown : 0;
  report.entries.push_back(root);
  for (const auto& [address, entry] : table) report.entries.push_back(entry);
  std::sort(report.entries.begin(), report.entries.end(), [](const hw::ProfileEntry& left, const hw::ProfileEntry& right) {
    if (left.exclusiveCycles != right.exclusiveCycles) return left.exclusiveCycles > right.exclusiveCycles;
    return left.address < right.address;
  });
  return report;
}

}  // namespace ember::logic::hardware
