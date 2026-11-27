/*
 * ------------------------------------------------------------------------
 * 名称：apu-channels.hpp
 * 说明：2A03 APU 的发声部件：长度计数器、包络、两个方波（含扫频）、三角波、噪声、DMC。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：部件按 CPU 周期被 Apu2a03 逐周期调用；每个通道的 output() 给出当前的数字输出（方波、噪声 0-15，三角波 0-15，DMC 0-127）。
 *       长度计数器的“载入”与“暂停位”都带一个周期的延迟：载入在同一周期被帧计数器时钟过的情况下无效，暂停位在下一周期末才生效，
 *       这是 blargg 的 len_reload_timing、len_halt_timing 测试所要求的行为。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>

#include "logic/hardware/apu-2a03/apu-tables.hpp"

namespace ember::logic::hardware::apu {

/** 长度计数器。 */
class LengthCounter {
 public:
  /** 复位：计数清零、禁用；上电时暂停位也清零（软复位保持，三角波的暂停位即线性计数器的控制位）。 */
  void reset(bool power);

  /** $4015 的使能位：禁用时计数清零，禁用期间载入无效。 */
  void setEnabled(bool enabled);

  /** 载入长度（$4003 等的高 5 位是表索引）；在下一个周期末生效，若该周期里计数被时钟过则放弃。 */
  void load(std::uint8_t index);

  /** 设置暂停位；在下一个周期末生效。 */
  void setHalt(bool halt) { pendingHalt_ = halt; }

  /** 半帧时钟：未暂停且计数大于 0 时减 1。 */
  void clock();

  /** 每个周期的帧事件之后调用：完成延迟的载入与暂停位。 */
  void endCycle();

  [[nodiscard]] bool active() const { return counter_ > 0; }
  [[nodiscard]] bool halted() const { return halt_; }
  [[nodiscard]] bool enabled() const { return enabled_; }
  [[nodiscard]] std::uint8_t value() const { return counter_; }

 private:
  bool enabled_{false};
  bool halt_{false};
  bool pendingHalt_{false};
  std::uint8_t counter_{0};
  std::uint8_t reloadValue_{0};
  std::uint8_t previousValue_{0};
};

/** 包络：音量衰减或固定音量。循环标志与长度计数器的暂停位是同一个位，由调用方传入。 */
class Envelope {
 public:
  void reset();

  /** 写 $4000/$4004/$400C：位 4 为固定音量，低 4 位为音量或分频周期。 */
  void write(std::uint8_t value);

  /** 重新开始包络（写 $4003/$4007/$400F 时）。 */
  void restart() { start_ = true; }

  /** 四分之一帧时钟。 */
  void clock(bool loop);

  [[nodiscard]] std::uint8_t volume() const { return constant_ ? period_ : decay_; }

 private:
  bool start_{false};
  bool constant_{false};
  std::uint8_t period_{0};
  std::uint8_t divider_{0};
  std::uint8_t decay_{0};
};

/** 方波通道。 */
class PulseChannel {
 public:
  /**
   * @param onesComplement 扫频的负向用反码：方波 1 为 true，方波 2 为 false（目标周期差 1）。
   */
  explicit PulseChannel(bool onesComplement) : onesComplement_(onesComplement) {}

  void reset(bool power);

  /** 写通道的寄存器 0-3（$4000-$4003 或 $4004-$4007）。 */
  void write(unsigned reg, std::uint8_t value);

  /** 每个 CPU 周期调用一次：推进定时器。 */
  void clockTimer();
  void clockEnvelope() { envelope_.clock(length_.halted()); }
  void clockLength() { length_.clock(); }
  void clockSweep();
  void endCycle() { length_.endCycle(); }

  [[nodiscard]] std::uint8_t output() const;
  LengthCounter& length() { return length_; }
  [[nodiscard]] const LengthCounter& length() const { return length_; }
  [[nodiscard]] unsigned period() const { return period_; }

 private:
  [[nodiscard]] int targetPeriod() const;
  [[nodiscard]] bool sweepMuted() const;

  bool onesComplement_;
  LengthCounter length_;
  Envelope envelope_;
  unsigned duty_{0};
  unsigned sequence_{0};
  unsigned period_{0};
  int timer_{2};
  bool sweepEnabled_{false};
  bool sweepNegate_{false};
  bool sweepReload_{false};
  unsigned sweepPeriod_{0};
  unsigned sweepShift_{0};
  unsigned sweepDivider_{0};
};

/** 三角波通道。 */
class TriangleChannel {
 public:
  void reset(bool power);

  /** 写通道的寄存器 0、2、3（$4008、$400A、$400B）。 */
  void write(unsigned reg, std::uint8_t value);

  void clockTimer();
  void clockLinear();
  void clockLength() { length_.clock(); }
  void endCycle() { length_.endCycle(); }

  [[nodiscard]] std::uint8_t output() const;
  LengthCounter& length() { return length_; }
  [[nodiscard]] const LengthCounter& length() const { return length_; }

 private:
  LengthCounter length_;
  bool control_{false};
  bool linearReload_{false};
  std::uint8_t linearReloadValue_{0};
  std::uint8_t linear_{0};
  unsigned period_{0};
  int timer_{1};
  unsigned sequence_{0};
};

/** 噪声通道。 */
class NoiseChannel {
 public:
  void reset(bool power);

  /** 选择周期表：PAL 用 PAL 表，默认 NTSC。 */
  void setPal(bool pal) { table_ = pal ? &NOISE_PERIOD_TABLE_PAL : &NOISE_PERIOD_TABLE; }

  /** 写通道的寄存器 0、2、3（$400C、$400E、$400F）。 */
  void write(unsigned reg, std::uint8_t value);

  void clockTimer();
  void clockEnvelope() { envelope_.clock(length_.halted()); }
  void clockLength() { length_.clock(); }
  void endCycle() { length_.endCycle(); }

  [[nodiscard]] std::uint8_t output() const;
  LengthCounter& length() { return length_; }
  [[nodiscard]] const LengthCounter& length() const { return length_; }

 private:
  LengthCounter length_;
  Envelope envelope_;
  bool shortMode_{false};
  const std::array<std::uint16_t, 16>* table_{&NOISE_PERIOD_TABLE};
  unsigned period_{4};
  int timer_{4};
  std::uint16_t shift_{1};
};

/** DMC（差分调制）通道：采样由 Apu2a03 经 DMA 取来，本类只管状态与输出。 */
class DmcChannel {
 public:
  void reset(bool power);

  /** 选择速率表：PAL 用 PAL 表，默认 NTSC。 */
  void setPal(bool pal) { table_ = pal ? &DMC_RATE_TABLE_PAL : &DMC_RATE_TABLE; }

  /** 写通道的寄存器 0-3（$4010-$4013）。 */
  void write(unsigned reg, std::uint8_t value);

  /**
   * $4015 的使能位：禁用时剩余字节清零；启用且没有剩余字节时按 $4012/$4013 重新开始，并在 startDelay 个周期后请求取样。
   */
  void setEnabled(bool enabled, unsigned startDelay);

  /** 每个 CPU 周期调用一次：推进启动延迟与输出单元的定时器。 */
  void clock();

  /** 取走“需要一次取样 DMA”的请求；返回 true 后调用方负责在读到字节后调用 deliverSample。 */
  bool takeDmaRequest();

  /** 要读的采样地址。 */
  [[nodiscard]] std::uint16_t dmaAddress() const { return address_; }

  /** 取样 DMA 读到一个字节：装入缓冲、推进地址与剩余字节，结束时按循环与 IRQ 标志处理。 */
  void deliverSample(std::uint8_t byte);

  /** 取消正在进行的 DMA（复位时）。 */
  void cancelDma();

  [[nodiscard]] bool irqFlag() const { return irqFlag_; }
  void clearIrq() { irqFlag_ = false; }
  /** 有剩余字节（$4015 的位 4）。 */
  [[nodiscard]] bool active() const { return bytesRemaining_ > 0; }
  [[nodiscard]] std::uint8_t output() const { return level_; }

  /** 距离下一次取样请求还有多少个周期；没有预计的请求返回 -1。 */
  [[nodiscard]] std::int64_t cyclesUntilRequest() const;

 private:
  void restartSample();
  void requestIfNeeded();
  void clockOutput();

  bool irqEnabled_{false};
  bool loop_{false};
  bool irqFlag_{false};
  const std::array<std::uint16_t, 16>* table_{&DMC_RATE_TABLE};
  unsigned period_{428};
  int timer_{428};
  std::uint16_t sampleAddress_{0xC000};
  std::uint16_t sampleLength_{1};
  std::uint16_t address_{0xC000};
  std::uint16_t bytesRemaining_{0};
  std::uint8_t buffer_{0};
  bool bufferEmpty_{true};
  std::uint8_t shift_{0};
  unsigned bitsLeft_{8};
  bool silence_{true};
  std::uint8_t level_{0};
  unsigned startDelay_{0};
  bool wantDma_{false};
  bool dmaInFlight_{false};
};

}  // namespace ember::logic::hardware::apu
