/*
 * ------------------------------------------------------------------------
 * 名称：reconstruction-filter.cpp
 * 说明：重建滤波器的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/psg-um6576/reconstruction-filter.hpp"

#include <cmath>
#include <numbers>

namespace ember::logic::hardware {

namespace {

/** 滤波器工作的采样率（赫兹）。 */
constexpr double SAMPLE_RATE_HZ = 48000.0;

}  // namespace

ReconstructionFilter::ReconstructionFilter(double cutoffHz) {
  constexpr double pi = std::numbers::pi;
  const double normalized = 2.0 * cutoffHz / SAMPLE_RATE_HZ;
  constexpr int middle = static_cast<int>(TAPS / 2);
  double sum = 0;
  std::array<double, TAPS> values{};
  for (int index = 0; index < static_cast<int>(TAPS); ++index) {
    const double x = static_cast<double>(index - middle);
    const double sinc = index == middle ? 1.0 : std::sin(pi * normalized * x) / (pi * normalized * x);
    const double position = static_cast<double>(index) / static_cast<double>(TAPS - 1);
    const double window = 0.42 - 0.5 * std::cos(2.0 * pi * position) + 0.08 * std::cos(4.0 * pi * position);
    values[static_cast<std::size_t>(index)] = normalized * sinc * window;
    sum += values[static_cast<std::size_t>(index)];
  }
  // 直流增益归一为 1。
  for (std::size_t index = 0; index < TAPS; ++index) taps_[index] = static_cast<float>(values[index] / sum);
}

void ReconstructionFilter::reset() {
  history_.fill(0.0F);
  position_ = 0;
}

float ReconstructionFilter::process(float input) {
  history_[position_] = input;
  history_[position_ + TAPS] = input;
  // 窗口 [position_ + 1, position_ + TAPS] 从最旧到最新；系数对称，顺序不影响结果。
  const float* window = history_.data() + position_ + 1;
  float sum = 0.0F;
  for (std::size_t index = 0; index < TAPS; ++index) sum += taps_[index] * window[index];
  position_ = position_ + 1 == TAPS ? 0 : position_ + 1;
  return sum;
}

}  // namespace ember::logic::hardware
