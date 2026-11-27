/*
 * ------------------------------------------------------------------------
 * 名称：band-limited-sampler.hpp
 * 说明：带限阶跃合成：把按 CPU 周期变化的混音电平，经带限滤波降采样为固定采样率的单声道 PCM，并做 DC 阻断。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：做法（与 blip_buf 相同）：电平每变化一次，就把变化量乘以“加窗 sinc 脉冲响应”累加到输出样本的缓冲里，
 *       输出时对缓冲逐样本积分得到阶跃，所以方波的陡峭边沿不会产生混叠嘶声；
 *       脉冲响应取 32 点（半宽 16 个输出样本）的 Blackman 窗、截止频率为奈奎斯特的 85%（约 20 kHz），位置精度 1/256 个样本，
 *       通带到约 16 kHz，24.9 kHz 以上衰减 70 dB 以上；代价是 16 个样本（约 0.33 毫秒）的固定延迟，每次电平变化做 32 次乘加，
 *       比“累积平均”（箱式滤波）的混叠少得多，比更长的脉冲响应省，延迟也低。
 *       积分后接一阶高通（约 20 Hz）阻断直流，避免电平漂移与开关时的爆音。
 *       放在 shared 里，供各个声音芯片（APU、PSG）共用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ember::shared {

/** 带限降采样器。 */
class BandLimitedSampler {
 public:
  /** 脉冲响应的半宽（输出样本数）。 */
  static constexpr int HALF_WIDTH = 16;
  /** 小数位置的相位数。 */
  static constexpr int PHASES = 256;
  /** 直流阻断的截止频率（赫兹）。 */
  static constexpr double DC_BLOCK_HZ = 20.0;

  /**
   * @param inputRate 输入的时钟频率（赫兹），即 CPU 频率；时间用从 0 起的周期数表示。
   * @param outputRate 输出采样率（赫兹），必须小于 inputRate。
   * @throws CoreError core.invalidParams：频率不合法。
   */
  BandLimitedSampler(double inputRate, unsigned outputRate);

  /**
   * 清空状态。
   * @param startTick 时间的起点（输入时钟节拍数），默认 0；中途启动时传当前节拍，不会补出之前的样本。
   */
  void reset(std::int64_t startTick = 0);

  /**
   * 在某个周期上电平发生了变化。
   * @param tick 变化发生的周期（单调不减）。
   * @param delta 电平的变化量。
   */
  void addDelta(std::int64_t tick, double delta);

  /**
   * 把已经定型的输出样本追加到 out。每个周期都可以调用，没有新样本时什么也不做。
   * @param tick 当前周期；此后的 addDelta 不会早于它。
   */
  void advance(std::int64_t tick, std::vector<float>& out);

  /** 已输出的样本总数。 */
  [[nodiscard]] std::int64_t emittedSamples() const { return emitted_; }

 private:
  static constexpr int TAPS = HALF_WIDTH * 2;
  /** 循环缓冲的大小：必须是 2 的幂且大于脉冲响应的跨度。 */
  static constexpr int RING_SIZE = 64;

  [[nodiscard]] std::int64_t quantize(std::int64_t tick) const;

  double phasesPerTick_;
  double dcBlockFactor_;
  std::array<double, RING_SIZE> ring_{};
  std::int64_t emitted_{0};
  double integral_{0};
  double previousInput_{0};
  double previousOutput_{0};
};

}  // namespace ember::shared
