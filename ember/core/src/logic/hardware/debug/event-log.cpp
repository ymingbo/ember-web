/*
 * ------------------------------------------------------------------------
 * 名称：event-log.cpp
 * 说明：事件记录的实现，见 event-log.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-24
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/debug/event-log.hpp"

#include <algorithm>

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

void EventLog::start(std::uint32_t capacity) {
  capacity_ = std::clamp<std::uint32_t>(capacity, 1, MAX_EVENTS);
  dropped_ = 0;
  events_.clear();
  active_ = true;
  started_ = true;
}

void EventLog::record(const hw::DebugEvent& event) {
  if (!active_) return;
  if (events_.size() >= capacity_) {
    events_.pop_front();
    ++dropped_;
  }
  events_.push_back(event);
}

hw::EventLogOutput EventLog::output(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents, std::uint64_t currentFrame) const {
  hw::EventLogOutput output;
  output.active = active_;
  output.frame = currentFrame;
  output.dropped = dropped_;
  // 事件按发生的先后存放，帧序号不会变小：从第一个够新的事件起取。
  auto first = events_.begin();
  if (fromFrame.has_value()) first = std::find_if(events_.begin(), events_.end(), [&](const hw::DebugEvent& event) { return event.frame >= *fromFrame; });
  const auto available = static_cast<std::size_t>(events_.end() - first);
  if (available > maxEvents) first += static_cast<std::ptrdiff_t>(available - maxEvents);
  output.events.assign(first, events_.end());
  return output;
}

}  // namespace ember::logic::hardware
