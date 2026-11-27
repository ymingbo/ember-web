/*
 * ------------------------------------------------------------------------
 * 名称：band-limited-sampler.cpp
 * 说明：带限阶跃合成的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * ------------------------------------------------------------------------
 */

#include "shared/audio/band-limited-sampler.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "shared/errors/core-error.hpp"

namespace ember::shared {

namespace {

/** 带限脉冲响应的截止频率与奈奎斯特频率之比。 */
constexpr double CUTOFF_RATIO = 0.85;

using Kernel = std::array<std::array<float, 2 * BandLimitedSampler::HALF_WIDTH>, BandLimitedSampler::PHASES>;

/** 生成每个相位的脉冲响应：加窗 sinc，每个相位的各点之和归一化为 1。 */
Kernel buildKernel() {
  constexpr int halfWidth = BandLimitedSampler::HALF_WIDTH;
  constexpr double pi = std::numbers::pi;
  Kernel kernel{};
  for (int phase = 0; phase < BandLimitedSampler::PHASES; ++phase) {
    const double fraction = static_cast<double>(phase) / BandLimitedSampler::PHASES;
    std::array<double, 2 * halfWidth> taps{};
    double sum = 0;
    for (int tap = 0; tap < 2 * halfWidth; ++tap) {
      // 第 tap 个输出样本相对脉冲位置的距离（单位：输出样本）。
      const double x = static_cast<double>(1 - halfWidth + tap) - fraction;
      const double sinc = std::abs(x) < 1e-12 ? CUTOFF_RATIO : std::sin(pi * CUTOFF_RATIO * x) / (pi * x);
      const double t = (x + halfWidth) / (2.0 * halfWidth);
      const double window = 0.42 - 0.5 * std::cos(2.0 * pi * t) + 0.08 * std::cos(4.0 * pi * t);
      taps[static_cast<std::size_t>(tap)] = sinc * window;
      sum += taps[static_cast<std::size_t>(tap)];
    }
    for (int tap = 0; tap < 2 * halfWidth; ++tap) kernel[static_cast<std::size_t>(phase)][static_cast<std::size_t>(tap)] = static_cast<float>(taps[static_cast<std::size_t>(tap)] / sum);
  }
  return kernel;
}

const Kernel& kernelTable() {
  static const Kernel table = buildKernel();
  return table;
}

}  // namespace

BandLimitedSampler::BandLimitedSampler(double inputRate, unsigned outputRate) {
  if (!(inputRate > 0) || outputRate == 0 || static_cast<double>(outputRate) >= inputRate) {
    throw CoreError(ERROR_INVALID_PARAMS, "采样率必须大于 0，且输出采样率必须小于输入时钟频率");
  }
  phasesPerTick_ = static_cast<double>(outputRate) / inputRate * PHASES;
  dcBlockFactor_ = std::exp(-2.0 * std::numbers::pi * DC_BLOCK_HZ / static_cast<double>(outputRate));
}

void BandLimitedSampler::reset(std::int64_t startTick) {
  ring_.fill(0);
  emitted_ = startTick == 0 ? 0 : quantize(startTick) / PHASES - HALF_WIDTH + 1;
  integral_ = 0;
  previousInput_ = 0;
  previousOutput_ = 0;
}

std::int64_t BandLimitedSampler::quantize(std::int64_t tick) const { return static_cast<std::int64_t>(std::floor(static_cast<double>(tick) * phasesPerTick_ + 0.5)); }

void BandLimitedSampler::addDelta(std::int64_t tick, double delta) {
  const std::int64_t position = quantize(tick);
  const std::int64_t base = position / PHASES;
  const auto& taps = kernelTable()[static_cast<std::size_t>(position % PHASES)];
  const std::int64_t first = base - HALF_WIDTH + 1;
  for (int tap = 0; tap < TAPS; ++tap) ring_[static_cast<std::size_t>((first + tap) & (RING_SIZE - 1))] += delta * static_cast<double>(taps[static_cast<std::size_t>(tap)]);
}

void BandLimitedSampler::advance(std::int64_t tick, std::vector<float>& out) {
  // 此后的脉冲最早影响第 (position / PHASES - HALF_WIDTH + 1) 个样本，之前的样本已经定型。
  const std::int64_t finished = quantize(tick) / PHASES - HALF_WIDTH;
  while (emitted_ <= finished) {
    const std::size_t index = static_cast<std::size_t>(emitted_ & (RING_SIZE - 1));
    integral_ += ring_[index];
    ring_[index] = 0;
    const double filtered = integral_ - previousInput_ + dcBlockFactor_ * previousOutput_;
    previousInput_ = integral_;
    previousOutput_ = filtered;
    out.push_back(static_cast<float>(std::clamp(filtered, -1.0, 1.0)));
    ++emitted_;
  }
}

}  // namespace ember::shared
