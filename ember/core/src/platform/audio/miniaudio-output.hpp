/*
 * ------------------------------------------------------------------------
 * 名称：miniaudio-output.hpp
 * 说明：系统声音输出：用 miniaudio 打开系统默认的播放设备（Windows 为 WASAPI），回调线程从环形缓冲取采样。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：设备初始化失败（没有声卡、设备被独占等）时 open 返回空指针并给出原因，由调用方降级为空输出；
 *       miniaudio 的头文件只在本目录的实现文件里包含，对外隐藏。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <string>

#include "platform/audio/audio-output.hpp"

namespace ember::platform {

/** 经 miniaudio 输出到系统声音设备。 */
class MiniaudioOutput final : public AudioOutput {
 public:
  /**
   * 打开系统默认的播放设备（单声道、32 位浮点，系统需要别的格式时由 miniaudio 转换）。
   * @param sampleRate 采样率（赫兹）。
   * @param failure 失败时写入原因。
   * @return 成功返回输出对象，失败返回空指针。
   */
  static std::unique_ptr<MiniaudioOutput> open(unsigned sampleRate, std::string& failure);

  ~MiniaudioOutput() override;

  MiniaudioOutput(const MiniaudioOutput&) = delete;
  MiniaudioOutput& operator=(const MiniaudioOutput&) = delete;

  [[nodiscard]] unsigned sampleRate() const override;
  [[nodiscard]] bool isSystemOutput() const override { return true; }
  std::size_t write(std::span<const float> samples) noexcept override;
  [[nodiscard]] std::size_t bufferedSamples() const noexcept override;
  void setVolume(unsigned percent, bool muted) override;
  void setPlaying(bool playing) override;
  void setFast(bool fast) override;
  void flush() override;
  void setBufferProfile(AudioBufferProfile profile) override;
  [[nodiscard]] AudioStats stats() const noexcept override;

 private:
  struct Device;
  explicit MiniaudioOutput(std::unique_ptr<Device> device);

  std::unique_ptr<Device> device_;
};

}  // namespace ember::platform
