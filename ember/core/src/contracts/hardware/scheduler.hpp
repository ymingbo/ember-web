/*
 * ------------------------------------------------------------------------
 * 名称：scheduler.hpp
 * 说明：硬件契约：调度器。需要定时的设备登记“某个时间点回调我”，不要求每个周期被调用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：同一时间点的事件按登记先后依次触发；PPU、声音等不逐周期运行的芯片，在被访问前与事件到期时追赶到当前时间。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/clock.hpp"

namespace ember::contracts::hardware {

/** 事件编号；0 表示没有事件。 */
using EventId = std::uint64_t;

/** 没有事件。 */
inline constexpr EventId NO_EVENT = 0;

/** 事件的接收方。 */
class EventListener {
 public:
  virtual ~EventListener() = default;

  /**
   * 事件到期。回调中可以登记或取消事件，但不能推进时钟。
   * @param when 事件登记的时间点（主时钟计数）；到期时 `Clock::now()` 也等于它，登记在过去的事件除外。
   * @param tag 登记时给出的标记，用来区分同一接收方的不同事件。
   */
  virtual void onEvent(MasterTime when, std::uint32_t tag) = 0;
};

/** 调度器：带事件调度的主时钟。 */
class Scheduler : public Clock {
 public:
  /**
   * 登记一个事件。时间点早于当前时间的事件在下一次推进时立即触发。
   * @param when 触发的时间点（主时钟计数）。
   * @param listener 接收方，必须活到事件触发或取消。
   * @param tag 回调时原样带回的标记。
   * @return 事件编号，用于取消。
   */
  virtual EventId scheduleAt(MasterTime when, EventListener& listener, std::uint32_t tag) = 0;

  /**
   * 取消还没有触发的事件。
   * @return 事件仍在等待并已取消返回 true；事件已触发或不存在返回 false。
   */
  virtual bool cancel(EventId id) = 0;
};

}  // namespace ember::contracts::hardware
