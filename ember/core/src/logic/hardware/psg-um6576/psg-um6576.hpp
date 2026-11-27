/*
 * ------------------------------------------------------------------------
 * 名称：psg-um6576.hpp
 * 说明：6576 的 4 通道 PSG（$4000-$400F、$4015）：两个方波、三角波、噪声，并把芯片组 DAC 的电平混进同一路输出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：寄存器与 NES 的 APU 同样，没有 DMC 与 $4010-$4013；方波不做扫频，$4001、$4005 写入无效；
 *       没有 $4017，内部固定走 4 步帧序列（四分之一帧时钟包络与三角波线性计数器，第 2、4 步再时钟长度计数器，不产生 IRQ）；
 *       $4015 写低 4 位使能通道（禁用时长度清零），读返回低 4 位各通道长度计数器是否大于 0（真机位 4 的读数不固定，属已知差异，这里读 0）；
 *       PAL 时序：噪声周期取 PAL 表，帧序列间隔约 8314 个 CPU 周期（50 Hz 的 4 步）；
 *       混音用 NES 的非线性公式（方波、三角波与噪声），电平每变化一次就经带限采样器（shared/audio，与 APU 共用）降采样，
 *       阶跃的位置精确到周期，三角波与噪声的阶梯不会混叠成嘶声，直流阻断也由它完成，输出有 16 个采样（约 0.33 毫秒）的固定延迟；
 *       DAC（程序约每 46 微秒写入一个采样的 PCM，开机语音就是这样播的）与语音合成器（10 kHz）是阶梯电平，阶梯在采样率一半以上留下镜像成分（沙沙的杂音），
 *       所以各自走独立的带限采样器，再经 48 kHz 下的低通重建滤波器（ReconstructionFilter）滤掉镜像，取走声音时才与其他通道相加；
 *       DAC 写入先经 DacRetimer：连续的采样流被中断延迟打乱的写入时刻在这里重新等间隔，否则每帧一次的停顿听起来是 50 Hz 的杂音；
 *       PSG 不逐周期被调用：寄存器读写与取走声音前追赶到当前时间（内部逐周期推进）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "contracts/hardware/clock.hpp"
#include "contracts/hardware/device.hpp"
#include "logic/hardware/psg-um6576/dac-retimer.hpp"
#include "logic/hardware/psg-um6576/reconstruction-filter.hpp"
#include "shared/audio/band-limited-sampler.hpp"

namespace ember::logic::hardware {

/** 6576 的 PSG。 */
class PsgUm6576 final : public contracts::hardware::Device {
 public:
  /** 输出采样率（赫兹）。 */
  static constexpr unsigned SAMPLE_RATE = 48000;

  /**
   * @param clock 主时钟，读写寄存器与取声音时追赶到它的当前时间。
   * @param cpuDomain CPU 的时钟域。
   * @param cpuHz CPU 的频率（赫兹），用来换算采样间隔。
   */
  PsgUm6576(const contracts::hardware::Clock& clock, contracts::hardware::ClockDomain cpuDomain, double cpuHz);

  contracts::hardware::BusRead read(std::uint16_t address) override;
  contracts::hardware::BusRead peek(std::uint16_t address) override;
  void write(std::uint16_t address, std::uint8_t value) override;
  void reset(bool power) override;
  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

  /** 追赶到指定的主时钟时间。 */
  void catchUp(contracts::hardware::MasterTime now);

  /** DAC 的电平变化（追赶到现在之后设置），经低通重建后与 PSG、语音相加。 */
  void setDacLevel(std::uint8_t level);

  /** 语音合成器的输出电平（-1 到 1，追赶到现在之后设置），经低通重建后与 PSG、DAC 相加。 */
  void setVoiceLevel(double level);

  /** 取走自上次以来生成的采样（单声道，-1 到 1），先清空 samples。 */
  void takeAudio(std::vector<float>& samples);

  /** 通道是否在发声（长度计数器大于 0），供测试。 */
  [[nodiscard]] bool channelActive(unsigned channel) const { return channel < 4 && lengths_[channel] > 0; }

 private:
  /** 内部推进一个 CPU 周期。 */
  void stepCycle();
  /** 让 DAC 的新电平在当前周期生效。 */
  void applyDacLevel(std::uint8_t level);
  /** 当前的混音电平。 */
  [[nodiscard]] double mix() const;
  /** 帧序列的一步。 */
  void frameStep();
  void clockQuarter();
  void clockHalf();

  const contracts::hardware::Clock& clock_;
  contracts::hardware::ClockDomain cpuDomain_;
  /** 已推进到的 CPU 周期。 */
  std::int64_t cycle_{0};

  // 方波 0、1
  std::array<std::uint8_t, 2> duty_{};
  std::array<std::uint16_t, 2> pulsePeriod_{};
  std::array<int, 2> pulseTimer_{};
  std::array<unsigned, 2> pulseSequence_{};
  // 三角波
  std::uint16_t trianglePeriod_{0};
  int triangleTimer_{0};
  unsigned triangleSequence_{0};
  bool linearControl_{false};
  bool linearReload_{false};
  std::uint8_t linearReloadValue_{0};
  std::uint8_t linear_{0};
  // 噪声
  bool noiseShort_{false};
  std::uint16_t noisePeriod_{4};
  int noiseTimer_{4};
  std::uint16_t noiseShift_{1};
  // 包络（0、1 方波，2 噪声）、长度（0-3）
  std::array<bool, 3> envelopeStart_{};
  std::array<bool, 3> envelopeConstant_{};
  std::array<std::uint8_t, 3> envelopePeriod_{};
  std::array<std::uint8_t, 3> envelopeDivider_{};
  std::array<std::uint8_t, 3> envelopeDecay_{};
  std::array<bool, 4> halt_{};
  std::array<std::uint8_t, 4> lengths_{};
  std::array<bool, 4> enabled_{};
  // 帧序列
  std::int64_t frameCounter_{0};
  unsigned frameStepIndex_{0};

  std::uint8_t dacLevel_{0};
  double voiceLevel_{0};
  // 降采样
  shared::BandLimitedSampler sampler_;
  /** 已交给采样器的混音电平。 */
  double level_{0};
  std::vector<float> pending_;
  // 语音与 DAC：各自独立的降采样与重建滤波（截止频率按声源的采样率取：语音 10 kHz、DAC 程序逐个写入的节拍约 22 kHz）
  shared::BandLimitedSampler voiceSampler_;
  ReconstructionFilter voiceFilter_{4700.0};
  std::vector<float> voicePending_;
  shared::BandLimitedSampler dacSampler_;
  ReconstructionFilter dacFilter_{9500.0};
  std::vector<float> dacPending_;
  DacRetimer dacRetimer_;
};

}  // namespace ember::logic::hardware
