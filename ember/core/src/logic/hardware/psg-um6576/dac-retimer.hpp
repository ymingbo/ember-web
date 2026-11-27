/*
 * ------------------------------------------------------------------------
 * 名称：dac-retimer.hpp
 * 说明：DAC 写入的重新定时：程序逐个写入 PCM 采样（开机语音约每 47 微秒一个）时，写入时刻会被中断延迟打乱
 *       （例如每帧垂直消隐的中断处理占去约 240 微秒，之后连着补写几个），直接按写入时刻输出，每帧都有一次短暂的停顿，听起来是 50 Hz 的杂音；
 *       检测到连续的采样流后，先攒几个采样再按估计出的间隔等间隔输出，停顿被缓冲吸收。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：零星的写入（间隔大于约 600 微秒，或还没认出采样流）直接输出，不引入延迟；采样流断开时回到直接输出；
 *       缓冲的目标水位 12 个采样，输出间隔按水位在 ±5% 内微调，吸收写入速率与估计间隔的微小差异；时间用 CPU 周期表示。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace ember::logic::hardware {

/** DAC 采样流的重新定时器。 */
class DacRetimer {
 public:
  /** @param cpuHz CPU 的频率（赫兹），用来换算时间门限。 */
  explicit DacRetimer(double cpuHz);

  /** 清空状态。 */
  void reset();

  /**
   * 收到一次 DAC 写入。
   * @param cycle 写入发生的 CPU 周期（单调不减）。
   * @param level DAC 的新电平。
   */
  void push(std::int64_t cycle, std::uint8_t level);

  /**
   * 每个 CPU 周期调用一次：此刻有电平要生效时返回 true 并给出电平。
   * @param cycle 当前周期（单调不减）。
   */
  bool poll(std::int64_t cycle, std::uint8_t& level);

 private:
  /** 攒够这么多采样才开始输出，也是缓冲的目标水位。 */
  static constexpr std::size_t PREFILL = 12;
  /** 缓冲的容量上限，超出时丢弃最旧的。 */
  static constexpr std::size_t CAPACITY = 64;
  /** 认出采样流所需的、间隔接近估计值的连续写入个数。 */
  static constexpr unsigned STABLE_WRITES = 64;
  /** 两次写入间隔超过这个时间（秒）就不再算同一个采样流。 */
  static constexpr double BREAK_SECONDS = 0.0006;

  /** 回到直接输出：丢弃缓冲与估计。 */
  void resetStream();
  void direct(std::uint8_t level);

  double breakCycles_;
  bool seen_{false};
  std::int64_t lastPush_{0};
  /** 采样流的平均间隔（周期），0 表示还没有估计。 */
  double interval_{0};
  unsigned stable_{0};
  /** 采样流里已计入估计的间隔个数。 */
  unsigned writes_{0};
  bool buffered_{false};
  bool playing_{false};
  double nextPlay_{0};
  std::deque<std::uint8_t> queue_;
  bool hasDirect_{false};
  std::uint8_t directLevel_{0};
};

}  // namespace ember::logic::hardware
