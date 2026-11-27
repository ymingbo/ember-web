/*
 * ------------------------------------------------------------------------
 * 名称：master-scheduler.hpp
 * 说明：调度器的实现：主时钟加按时间点排序的事件队列。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：事件按（时间点，登记序号）排序，同一时间点先登记的先触发；推进时钟时到期的事件在时钟到达终点之前触发。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>

#include "contracts/hardware/scheduler.hpp"

namespace ember::logic::hardware {

/** 主时钟调度器。 */
class MasterScheduler final : public contracts::hardware::Scheduler {
 public:
  [[nodiscard]] contracts::hardware::MasterTime now() const override { return now_; }

  void advance(contracts::hardware::MasterTime ticks) override;

  contracts::hardware::EventId scheduleAt(contracts::hardware::MasterTime when, contracts::hardware::EventListener& listener, std::uint32_t tag) override;

  bool cancel(contracts::hardware::EventId id) override;

  /** 还在等待的事件数。 */
  [[nodiscard]] std::size_t pendingCount() const { return queue_.size(); }

  /** 回到上电状态：时钟归零，丢弃全部事件。 */
  void reset();

 private:
  struct Entry {
    contracts::hardware::EventListener* listener;
    std::uint32_t tag;
  };
  /** 排序键：时间点与事件编号（编号随登记递增）。 */
  using Key = std::pair<contracts::hardware::MasterTime, contracts::hardware::EventId>;

  contracts::hardware::MasterTime now_{0};
  contracts::hardware::EventId nextId_{1};
  bool advancing_{false};
  std::map<Key, Entry> queue_;
  std::unordered_map<contracts::hardware::EventId, contracts::hardware::MasterTime> timeById_;
};

}  // namespace ember::logic::hardware
