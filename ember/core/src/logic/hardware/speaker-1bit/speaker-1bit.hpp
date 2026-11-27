/*
 * ------------------------------------------------------------------------
 * 名称：speaker-1bit.hpp
 * 说明：一位扬声器：程序每访问一次扬声器地址，喇叭的电平翻转一次；按主时钟时间记下翻转，
 *       取声音时按采样率生成采样。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：每个采样取它那段时间里电平的平均值（按时间加权），
 *       所以翻转得很快时声音会自然变弱而不是混叠成噪声；
 *       采样的位置按“采样率 / 主时钟”连续推进，不随取声音的间隔漂移；低电平输出 0，
 *       高电平输出幅度，没有翻转时无声。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <vector>

#include "contracts/hardware/clock.hpp"

namespace ember::logic::hardware {

/** 一位扬声器。 */
class Speaker1Bit {
 public:
  /** 喇叭高电平的幅度（采样值的最大值）。 */
  static constexpr float AMPLITUDE = 0.3F;

  /**
   * @param masterHz 主时钟频率（赫兹）。
   * @param sampleRate 输出的采样率（赫兹）。
   */
  Speaker1Bit(double masterHz, unsigned sampleRate);

  /** 清空（上电时）：电平回到低，时间从 0 起。 */
  void reset();

  /** 记一次翻转，when 不早于上一次翻转的时间。 */
  void toggle(contracts::hardware::MasterTime when);

  /**
   * 取走到 now 为止生成的采样，先清空 samples 再放入。
   * @param now 主机当前的主时钟时间。
   */
  void take(std::vector<float>& samples, contracts::hardware::MasterTime now);

 private:
  double ticksPerSample_;
  /** 还没有被采样消费的翻转时间。 */
  std::vector<contracts::hardware::MasterTime> toggles_;
  /** 采样已经算到的时间（主时钟周期，带小数）。 */
  double position_{0};
  /** position_ 处的电平。 */
  bool high_{false};
};

}  // namespace ember::logic::hardware
