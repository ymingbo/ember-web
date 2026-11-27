/*
 * ------------------------------------------------------------------------
 * 名称：clock.hpp
 * 说明：硬件契约：主时钟与时钟域。主时钟是整台机器唯一的时间基准，各芯片按自己的分频换算周期数。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：时间用主时钟计数表示，不用墙上时间；NTSC 经典 FC 的主时钟为 21.477272 MHz，CPU 12 分频，PPU 4 分频。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::contracts::hardware {

/** 主时钟计数：从上电起经过的主时钟周期数。 */
using MasterTime = std::int64_t;

/** 芯片的时钟域：芯片的一个周期占多少个主时钟周期（分频比）。 */
struct ClockDomain {
  /** 每个芯片周期的主时钟周期数，必须大于 0。 */
  MasterTime ticksPerCycle{1};

  /** 把芯片周期数换算成主时钟周期数。 */
  [[nodiscard]] constexpr MasterTime toMaster(std::int64_t cycles) const noexcept { return cycles * ticksPerCycle; }

  /** 把主时钟周期数换算成芯片周期数（向下取整）。 */
  [[nodiscard]] constexpr std::int64_t toCycles(MasterTime ticks) const noexcept { return ticks / ticksPerCycle; }
};

/** 主时钟。 */
class Clock {
 public:
  virtual ~Clock() = default;

  /** 当前主时钟计数。事件回调执行期间，它等于该事件的时间点。 */
  [[nodiscard]] virtual MasterTime now() const = 0;

  /**
   * 把主时钟向前推进。推进过程中到期的事件按时间顺序先于时钟到达终点被触发。
   * 只有驱动整机运行的 CPU 与主机调用它；事件回调里不能调用。
   * @param ticks 推进的主时钟周期数，不能为负。
   */
  virtual void advance(MasterTime ticks) = 0;
};

}  // namespace ember::contracts::hardware
