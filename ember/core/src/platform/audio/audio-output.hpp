/*
 * ------------------------------------------------------------------------
 * 名称：audio-output.hpp
 * 说明：声音输出的抽象：模拟线程把单声道 PCM 写进来，实现负责送到系统的声音设备；有系统设备与空两种实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：采样为 32 位浮点，范围 -1 到 1。写入在模拟线程，音量等设置可在任意线程，设备回调在系统的实时线程，
 *       三者之间不加锁：写入与回调经单生产者单消费者的环形缓冲，设置经原子量。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ember::platform {

/** 声音输出的统计。 */
struct AudioStats {
  /** 已写入的采样总数（含因缓冲写满而丢弃的）。 */
  std::uint64_t writtenSamples{0};
  /** 缓冲写满而丢弃的采样数（丢弃最新的）。 */
  std::uint64_t droppedSamples{0};
  /** 设备要采样而缓冲里没有、补了静音的采样数（只在播放时统计）。 */
  std::uint64_t underrunSamples{0};
};

/** 缓冲大小的档位：缓冲越小延迟越低，电脑负载高时越容易爆音。 */
enum class AudioBufferProfile : std::uint8_t { LowLatency, Standard, Stable };

/** 声音输出。 */
class AudioOutput {
 public:
  virtual ~AudioOutput() = default;

  /** 采样率（赫兹），写入的采样必须是这个采样率。 */
  [[nodiscard]] virtual unsigned sampleRate() const = 0;

  /** 是否真的送到系统的声音设备（空实现为 false）。 */
  [[nodiscard]] virtual bool isSystemOutput() const = 0;

  /**
   * 写入单声道采样。不阻塞、不抛出异常；缓冲写满时丢弃多出来的最新采样并计数。
   * @return 实际写入的采样数。
   */
  virtual std::size_t write(std::span<const float> samples) noexcept = 0;

  /** 缓冲里等待播放的采样数（水位）。 */
  [[nodiscard]] virtual std::size_t bufferedSamples() const noexcept = 0;

  /**
   * 设置音量与静音；变化平滑过渡，不会产生爆音。
   * @param percent 音量百分比，0 到 100，超出按边界处理。
   * @param muted 是否静音。
   */
  virtual void setVolume(unsigned percent, bool muted) = 0;

  /**
   * 设置是否在播放：false 时在几毫秒内淡出到静音（暂停、断电）；true 时先丢弃缓冲里过时的采样再淡入。
   */
  virtual void setPlaying(bool playing) = 0;

  /**
   * 设置快速模式：true 时写入的采样直接丢弃，设备播放静音（不等声卡）；false 时按 setPlaying 的状态正常播放。
   */
  virtual void setFast(bool fast) = 0;

  /** 清空缓冲（上电、复位时）。 */
  virtual void flush() = 0;

  /** 设置缓冲大小的档位（任意线程）：改变目标水位与预填充，立即生效，不重建设备。 */
  virtual void setBufferProfile(AudioBufferProfile profile) = 0;

  /** 统计。 */
  [[nodiscard]] virtual AudioStats stats() const noexcept = 0;
};

}  // namespace ember::platform
