/*
 * ------------------------------------------------------------------------
 * 名称：audio-renderer.hpp
 * 说明：声音回调一侧的处理：从环形缓冲取采样，按水位微调重采样比，应用音量、静音与淡入淡出，缓冲不足时补静音。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：同步策略：模拟线程按帧节拍休眠运行，产生采样的速度与声卡消耗的速度总有微小漂移，用缓冲水位校正——
 *       水位高于目标就略微加快读取（重采样比大于 1，每个输出采样消耗略多于一个输入采样），低于目标就略微放慢，
 *       比例的偏离限制在 ±0.5% 以内（约 ±8.6 音分，听不出音调变化），用线性插值重采样，水位先做指数平滑再算比例，
 *       避免每帧一次的锯齿波动让音调抖动；缓冲不足时先让最后一个采样衰减到 0，再静音，攒够一小段（预填充）后重新开始，避免断断续续。
 *       渲染函数只用原子量与自己的状态，不加锁、不分配内存，可在系统的实时线程调用；设置函数可在任意线程调用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "platform/audio/audio-output.hpp"
#include "platform/audio/sample-ring-buffer.hpp"

namespace ember::platform {

/** 渲染器的参数。 */
struct AudioRendererOptions {
  /** 采样率（赫兹），用来换算淡入淡出的时长。 */
  unsigned sampleRate{48000};
  /** 目标水位（采样数）：水位在它附近时重采样比为 1。 */
  std::size_t targetFill{1600};
  /** 开始播放前缓冲里要攒够的采样数。 */
  std::size_t primeFill{1200};
  /** 重采样比偏离 1 的最大值。 */
  double maxRatioAdjust{0.005};
  /** 音量与播放状态变化时淡入淡出的时长（秒）。 */
  double rampSeconds{0.010};
};

/** 环形缓冲的消费者：产生设备需要的输出采样。 */
class AudioRenderer {
 public:
  AudioRenderer(SampleRingBuffer& ring, AudioRendererOptions options);

  AudioRenderer(const AudioRenderer&) = delete;
  AudioRenderer& operator=(const AudioRenderer&) = delete;

  /** 在设备的实时线程里产生 frames 个单声道输出采样。 */
  void render(float* out, std::size_t frames) noexcept;

  /** 设置音量与静音（任意线程）；变化经淡入淡出过渡。 */
  void setVolume(unsigned percent, bool muted) noexcept;

  /** 设置是否在播放（任意线程）：false 时淡出到静音，true 时淡入。 */
  void setPlaying(bool playing) noexcept;

  /** 请求清空缓冲（任意线程）：由下一次 render 在消费者一侧执行。 */
  void requestFlush() noexcept;

  /** 按档位设置目标水位与预填充（任意线程）：低延迟 800 与 600、标准 1600 与 1200、稳定 3200 与 2400 个采样；下一次 render 起采用。 */
  void setBufferProfile(AudioBufferProfile profile) noexcept;

  /** 当前的目标水位与预填充（采样数），测试用。 */
  [[nodiscard]] std::size_t targetFill() const noexcept { return targetFill_.load(std::memory_order_relaxed); }
  [[nodiscard]] std::size_t primeFill() const noexcept { return primeFill_.load(std::memory_order_relaxed); }

  /** 缓冲不足、补了静音的采样数（只在播放时统计）。 */
  [[nodiscard]] std::uint64_t underrunSamples() const noexcept { return underrun_.load(std::memory_order_relaxed); }

  /** 最近一次 render 用的重采样比。 */
  [[nodiscard]] double resampleRatio() const noexcept { return ratio_.load(std::memory_order_relaxed); }

  /** 当前的淡入淡出增益（0 到 1），测试用。 */
  [[nodiscard]] double currentGain() const noexcept { return gainView_.load(std::memory_order_relaxed); }

  /** 音量百分比对应的增益：按平方曲线，100 为 1，50 约为 0.25（-12 dB）。 */
  static double gainForVolume(unsigned percent) noexcept;

  /** 由水位算重采样比：水位等于目标为 1，偏高大于 1，偏低小于 1，偏离不超过 maxAdjust。 */
  static double ratioForFill(double fill, double target, double maxAdjust) noexcept;

 private:
  /** 衰减因子：缓冲不足时，最后一个采样每个输出采样乘一次，约 200 个采样（4 毫秒）衰减到 1/3。 */
  static constexpr float UNDERRUN_DECAY = 0.995F;
  /** 水位平滑系数：每次 render 向当前水位靠近的比例。 */
  static constexpr double FILL_SMOOTHING = 0.1;

  void applyFlushRequest() noexcept;
  float pull() noexcept;

  SampleRingBuffer& ring_;
  AudioRendererOptions options_;
  double rampStep_;

  // 设置：任意线程写，回调线程读。
  std::atomic<unsigned> volume_{100};
  std::atomic<bool> muted_{false};
  std::atomic<bool> playing_{false};
  std::atomic<std::uint32_t> flushRequest_{0};
  std::atomic<std::size_t> targetFill_{0};
  std::atomic<std::size_t> primeFill_{0};

  // 统计与观察用。
  std::atomic<std::uint64_t> underrun_{0};
  std::atomic<double> ratio_{1.0};
  std::atomic<double> gainView_{0.0};

  // 以下只由回调线程访问。
  std::uint32_t flushSeen_{0};
  bool buffering_{true};
  /** 正处在“缓冲不足”造成的攒数据状态（区别于开始播放或清空后的预填充）。 */
  bool starving_{false};
  double gain_{0.0};
  double smoothFill_{0.0};
  double ratioNow_{1.0};
  float previous_{0.0F};
  float next_{0.0F};
  double fraction_{1.0};
  float held_{0.0F};
};

}  // namespace ember::platform
