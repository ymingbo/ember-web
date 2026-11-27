/*
 * ------------------------------------------------------------------------
 * 名称：miniaudio-output.cpp
 * 说明：系统声音输出的实现：miniaudio 的播放设备、数据回调、环形缓冲与回调一侧的渲染器。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：环形缓冲 8192 个采样（约 170 毫秒），目标水位与预填充见 AudioRendererOptions；设备回调只调用渲染器，不加锁、不分配内存；
 *       miniaudio 的“空后端”（没有任何真实设备时它自动选用的静音后端）视为设备不可用，让调用方降级并记录。
 * ------------------------------------------------------------------------
 */

#include "platform/audio/miniaudio-output.hpp"

#include <atomic>

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <miniaudio.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "platform/audio/audio-renderer.hpp"
#include "platform/audio/sample-ring-buffer.hpp"

namespace ember::platform {

namespace {

/** 环形缓冲的容量（采样数）。 */
constexpr std::size_t RING_CAPACITY = 8192;
/** 设备的周期（毫秒）与周期数：周期越小延迟越低、回调越频繁。 */
constexpr ma_uint32 PERIOD_MS = 10;
constexpr ma_uint32 PERIOD_COUNT = 3;

}  // namespace

struct MiniaudioOutput::Device {
  explicit Device(unsigned rate) : sampleRate(rate), ring(RING_CAPACITY), renderer(ring, AudioRendererOptions{.sampleRate = rate}) {}

  ~Device() {
    if (initialized) ma_device_uninit(&device);
  }

  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;

  unsigned sampleRate;
  SampleRingBuffer ring;
  AudioRenderer renderer;
  std::atomic<std::uint64_t> written{0};
  std::atomic<std::uint64_t> dropped{0};
  /** 快速模式：采样丢弃，设备播放静音。 */
  std::atomic<bool> fast{false};
  /** 最近一次 setPlaying 的值；快速模式结束时据此恢复。 */
  std::atomic<bool> wantPlaying{false};
  ma_device device{};
  bool initialized{false};
};

namespace {

/** 设备的数据回调：运行在系统的实时线程。 */
void dataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames) {
  auto* owner = static_cast<AudioRenderer*>(device->pUserData);
  owner->render(static_cast<float*>(output), frames);
}

}  // namespace

MiniaudioOutput::MiniaudioOutput(std::unique_ptr<Device> device) : device_(std::move(device)) {}

MiniaudioOutput::~MiniaudioOutput() = default;

std::unique_ptr<MiniaudioOutput> MiniaudioOutput::open(unsigned sampleRate, std::string& failure) {
  auto device = std::make_unique<Device>(sampleRate);

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 1;
  config.sampleRate = sampleRate;
  config.periodSizeInMilliseconds = PERIOD_MS;
  config.periods = PERIOD_COUNT;
  config.performanceProfile = ma_performance_profile_low_latency;
  config.dataCallback = dataCallback;
  config.pUserData = &device->renderer;

  ma_result result = ma_device_init(nullptr, &config, &device->device);
  if (result != MA_SUCCESS) {
    failure = ma_result_description(result);
    return nullptr;
  }
  device->initialized = true;
  if (ma_device_get_context(&device->device)->backend == ma_backend_null) {
    failure = "没有可用的系统声音设备";
    return nullptr;
  }
  result = ma_device_start(&device->device);
  if (result != MA_SUCCESS) {
    failure = ma_result_description(result);
    return nullptr;
  }
  return std::unique_ptr<MiniaudioOutput>(new MiniaudioOutput(std::move(device)));
}

unsigned MiniaudioOutput::sampleRate() const { return device_->sampleRate; }

std::size_t MiniaudioOutput::write(std::span<const float> samples) noexcept {
  if (device_->fast.load(std::memory_order_relaxed)) return 0;
  const std::size_t accepted = device_->ring.write(samples);
  device_->written.fetch_add(samples.size(), std::memory_order_relaxed);
  if (accepted < samples.size()) device_->dropped.fetch_add(samples.size() - accepted, std::memory_order_relaxed);
  return accepted;
}

std::size_t MiniaudioOutput::bufferedSamples() const noexcept { return device_->ring.size(); }

void MiniaudioOutput::setVolume(unsigned percent, bool muted) { device_->renderer.setVolume(percent, muted); }

void MiniaudioOutput::setPlaying(bool playing) {
  device_->wantPlaying.store(playing, std::memory_order_relaxed);
  if (device_->fast.load(std::memory_order_relaxed)) return;
  // 继续播放前先丢弃过时的采样，渲染器会从静音淡入。
  if (playing) device_->renderer.requestFlush();
  device_->renderer.setPlaying(playing);
}

void MiniaudioOutput::setFast(bool fast) {
  device_->fast.store(fast, std::memory_order_relaxed);
  if (fast) {
    device_->renderer.setPlaying(false);
    return;
  }
  // 回到真实时间：按 setPlaying 的最近一次取值恢复，先丢弃过时的采样。
  const bool playing = device_->wantPlaying.load(std::memory_order_relaxed);
  if (playing) device_->renderer.requestFlush();
  device_->renderer.setPlaying(playing);
}

void MiniaudioOutput::flush() { device_->renderer.requestFlush(); }

void MiniaudioOutput::setBufferProfile(AudioBufferProfile profile) { device_->renderer.setBufferProfile(profile); }

AudioStats MiniaudioOutput::stats() const noexcept {
  return AudioStats{device_->written.load(std::memory_order_relaxed), device_->dropped.load(std::memory_order_relaxed), device_->renderer.underrunSamples()};
}

}  // namespace ember::platform
