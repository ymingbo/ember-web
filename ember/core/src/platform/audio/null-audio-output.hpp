/*
 * ------------------------------------------------------------------------
 * 名称：null-audio-output.hpp
 * 说明：空的声音输出：不送到任何设备，只统计写入的采样、记录音量与播放状态；用于测试与没有声卡的环境。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "platform/audio/audio-output.hpp"

namespace ember::platform {

/** 空声音输出：写入的采样全部丢弃（计入统计），水位恒为 0。 */
class NullAudioOutput final : public AudioOutput {
 public:
  explicit NullAudioOutput(unsigned sampleRate = 48000) : sampleRate_(sampleRate) {}

  [[nodiscard]] unsigned sampleRate() const override { return sampleRate_; }
  [[nodiscard]] bool isSystemOutput() const override { return false; }

  std::size_t write(std::span<const float> samples) noexcept override {
    written_.fetch_add(samples.size(), std::memory_order_relaxed);
    return samples.size();
  }

  [[nodiscard]] std::size_t bufferedSamples() const noexcept override { return 0; }

  void setVolume(unsigned percent, bool muted) override {
    volume_.store(percent > 100 ? 100 : percent, std::memory_order_relaxed);
    muted_.store(muted, std::memory_order_relaxed);
  }

  void setPlaying(bool playing) override {
    playing_.store(playing, std::memory_order_relaxed);
    if (playing) flushes_.fetch_add(1, std::memory_order_relaxed);
  }

  void flush() override { flushes_.fetch_add(1, std::memory_order_relaxed); }

  void setFast(bool fast) override { fast_.store(fast, std::memory_order_relaxed); }

  void setBufferProfile(AudioBufferProfile profile) override { profile_.store(profile, std::memory_order_relaxed); }

  [[nodiscard]] AudioStats stats() const noexcept override { return AudioStats{written_.load(std::memory_order_relaxed), 0, 0}; }

  // 测试用：最近设置的状态。
  [[nodiscard]] unsigned volume() const { return volume_.load(std::memory_order_relaxed); }
  [[nodiscard]] bool muted() const { return muted_.load(std::memory_order_relaxed); }
  [[nodiscard]] bool playing() const { return playing_.load(std::memory_order_relaxed); }
  /** 最近设置的快速模式。 */
  [[nodiscard]] bool fast() const { return fast_.load(std::memory_order_relaxed); }
  /** 最近设置的缓冲档位。 */
  [[nodiscard]] AudioBufferProfile bufferProfile() const { return profile_.load(std::memory_order_relaxed); }
  /** 清空缓冲的次数（含 setPlaying(true) 隐含的清空）。 */
  [[nodiscard]] std::uint64_t flushCount() const { return flushes_.load(std::memory_order_relaxed); }

 private:
  unsigned sampleRate_;
  std::atomic<std::uint64_t> written_{0};
  std::atomic<std::uint64_t> flushes_{0};
  std::atomic<unsigned> volume_{100};
  std::atomic<bool> muted_{false};
  std::atomic<bool> playing_{false};
  std::atomic<bool> fast_{false};
  std::atomic<AudioBufferProfile> profile_{AudioBufferProfile::Standard};
};

}  // namespace ember::platform
