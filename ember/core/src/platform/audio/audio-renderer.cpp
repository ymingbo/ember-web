/*
 * ------------------------------------------------------------------------
 * 名称：audio-renderer.cpp
 * 说明：声音回调一侧处理的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "platform/audio/audio-renderer.hpp"

#include <algorithm>
#include <span>

namespace ember::platform {

AudioRenderer::AudioRenderer(SampleRingBuffer& ring, AudioRendererOptions options) : ring_(ring), options_(options), targetFill_(options.targetFill), primeFill_(options.primeFill) {
  const double rampSamples = std::max(1.0, options_.rampSeconds * static_cast<double>(options_.sampleRate));
  rampStep_ = 1.0 / rampSamples;
  smoothFill_ = static_cast<double>(options_.targetFill);
}

double AudioRenderer::gainForVolume(unsigned percent) noexcept {
  const double linear = static_cast<double>(std::min(percent, 100U)) / 100.0;
  return linear * linear;
}

double AudioRenderer::ratioForFill(double fill, double target, double maxAdjust) noexcept {
  if (target <= 0) return 1.0;
  // 水位偏离目标 25% 时比例调到上限，之间线性。
  const double error = (fill - target) / target;
  return 1.0 + std::clamp(error * (maxAdjust * 4.0), -maxAdjust, maxAdjust);
}

void AudioRenderer::setVolume(unsigned percent, bool muted) noexcept {
  volume_.store(std::min(percent, 100U), std::memory_order_relaxed);
  muted_.store(muted, std::memory_order_relaxed);
}

void AudioRenderer::setPlaying(bool playing) noexcept { playing_.store(playing, std::memory_order_relaxed); }

void AudioRenderer::requestFlush() noexcept { flushRequest_.fetch_add(1, std::memory_order_release); }

void AudioRenderer::setBufferProfile(AudioBufferProfile profile) noexcept {
  // 目标水位与预填充（采样数，48000 赫兹下约 17 毫秒、标准 33 毫秒、稳定 67 毫秒）。
  std::size_t target = 1600;
  std::size_t prime = 1200;
  if (profile == AudioBufferProfile::LowLatency) {
    target = 800;
    prime = 600;
  } else if (profile == AudioBufferProfile::Stable) {
    target = 3200;
    prime = 2400;
  }
  targetFill_.store(target, std::memory_order_relaxed);
  primeFill_.store(prime, std::memory_order_relaxed);
}

void AudioRenderer::applyFlushRequest() noexcept {
  const std::uint32_t request = flushRequest_.load(std::memory_order_acquire);
  if (request == flushSeen_) return;
  flushSeen_ = request;
  ring_.discardAll();
  buffering_ = true;
  starving_ = false;
  smoothFill_ = static_cast<double>(targetFill_.load(std::memory_order_relaxed));
  // 正在静音时不留上一个采样的尾巴，否则继续播放时会有一小声；正在出声时让它衰减，不产生咔哒声。
  if (gain_ <= 0.0) held_ = 0.0F;
  previous_ = next_ = held_;
  fraction_ = 1.0;
}

float AudioRenderer::pull() noexcept {
  while (fraction_ >= 1.0) {
    float value = 0.0F;
    if (ring_.read(std::span<float>(&value, 1)) == 0) {
      // 缓冲空了：进入“攒数据”状态，这个采样让上一个采样继续衰减。
      buffering_ = true;
      starving_ = true;
      held_ *= UNDERRUN_DECAY;
      return held_;
    }
    previous_ = next_;
    next_ = value;
    fraction_ -= 1.0;
  }
  const float sample = previous_ + (next_ - previous_) * static_cast<float>(fraction_);
  fraction_ += ratioNow_;
  held_ = sample;
  return sample;
}

void AudioRenderer::render(float* out, std::size_t frames) noexcept {
  applyFlushRequest();
  const bool playing = playing_.load(std::memory_order_relaxed);
  const double wantedGain = playing && !muted_.load(std::memory_order_relaxed) ? gainForVolume(volume_.load(std::memory_order_relaxed)) : 0.0;

  if (!buffering_) {
    smoothFill_ += (static_cast<double>(ring_.size()) - smoothFill_) * FILL_SMOOTHING;
    ratioNow_ = ratioForFill(smoothFill_, static_cast<double>(targetFill_.load(std::memory_order_relaxed)), options_.maxRatioAdjust);
    ratio_.store(ratioNow_, std::memory_order_relaxed);
  }

  for (std::size_t i = 0; i < frames; ++i) {
    if (buffering_ && playing && ring_.size() >= primeFill_.load(std::memory_order_relaxed)) {
      // 攒够了：从衰减中的采样平滑地接上新数据，增益从静音淡入。
      buffering_ = false;
      starving_ = false;
      previous_ = next_ = held_;
      fraction_ = 1.0;
    }
    // 攒数据期间增益回到静音，攒够后再淡入。
    const double targetGain = buffering_ ? 0.0 : wantedGain;
    if (gain_ < targetGain) {
      gain_ = std::min(targetGain, gain_ + rampStep_);
    } else if (gain_ > targetGain) {
      gain_ = std::max(targetGain, gain_ - rampStep_);
    }
    float sample;
    if (buffering_) {
      held_ *= UNDERRUN_DECAY;
      sample = held_;
      if (starving_ && playing) underrun_.fetch_add(1, std::memory_order_relaxed);
    } else {
      sample = pull();
      if (buffering_ && playing) underrun_.fetch_add(1, std::memory_order_relaxed);
    }
    out[i] = static_cast<float>(static_cast<double>(sample) * gain_);
  }
  gainView_.store(gain_, std::memory_order_relaxed);
}

}  // namespace ember::platform
