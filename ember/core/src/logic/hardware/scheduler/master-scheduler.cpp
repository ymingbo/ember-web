/*
 * ------------------------------------------------------------------------
 * 名称：master-scheduler.cpp
 * 说明：调度器的实现：推进主时钟并按时间顺序触发到期的事件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/scheduler/master-scheduler.hpp"

#include <algorithm>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

/** 推进过程中标记“正在推进”，离开作用域（含异常）时清除。 */
class AdvanceGuard {
 public:
  explicit AdvanceGuard(bool& flag) : flag_(flag) { flag_ = true; }
  ~AdvanceGuard() { flag_ = false; }
  AdvanceGuard(const AdvanceGuard&) = delete;
  AdvanceGuard& operator=(const AdvanceGuard&) = delete;

 private:
  bool& flag_;
};

}  // namespace

void MasterScheduler::advance(hw::MasterTime ticks) {
  if (ticks < 0) throw shared::CoreError(hw::ERROR_CLOCK_INVALID_ADVANCE, "时钟不能向后推进");
  if (advancing_) throw shared::CoreError(hw::ERROR_CLOCK_INVALID_ADVANCE, "事件回调中不能推进时钟");
  const AdvanceGuard guard(advancing_);

  const hw::MasterTime target = now_ + ticks;
  // 回调里可能登记新事件，每次都从队首重新取。
  while (!queue_.empty() && queue_.begin()->first.first <= target) {
    const auto first = queue_.begin();
    const hw::MasterTime when = first->first.first;
    const Entry entry = first->second;
    timeById_.erase(first->first.second);
    queue_.erase(first);
    // 登记在过去的事件不让时钟倒退。
    now_ = std::max(now_, when);
    entry.listener->onEvent(when, entry.tag);
  }
  now_ = target;
}

hw::EventId MasterScheduler::scheduleAt(hw::MasterTime when, hw::EventListener& listener, std::uint32_t tag) {
  const hw::EventId id = nextId_++;
  queue_.emplace(Key{when, id}, Entry{&listener, tag});
  timeById_.emplace(id, when);
  return id;
}

bool MasterScheduler::cancel(hw::EventId id) {
  const auto found = timeById_.find(id);
  if (found == timeById_.end()) return false;
  queue_.erase(Key{found->second, id});
  timeById_.erase(found);
  return true;
}

void MasterScheduler::reset() {
  now_ = 0;
  queue_.clear();
  timeById_.clear();
}

}  // namespace ember::logic::hardware
