/*
 * ------------------------------------------------------------------------
 * 名称：apu-channels.cpp
 * 说明：2A03 APU 发声部件的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/apu-2a03/apu-channels.hpp"

#include "logic/hardware/apu-2a03/apu-tables.hpp"

namespace ember::logic::hardware::apu {

namespace {

/** 11 位定时器周期的上限；扫频的目标周期超过它时方波静音。 */
constexpr int MAX_PULSE_PERIOD = 0x7FF;
/** 方波周期小于它时静音。 */
constexpr unsigned MIN_PULSE_PERIOD = 8;
/** DMC 输出电平的最大值（7 位）。 */
constexpr std::uint8_t DMC_MAX_LEVEL = 125;

}  // namespace

// ---------------------------------------------------------------- 长度计数器

void LengthCounter::reset(bool power) {
  enabled_ = false;
  counter_ = 0;
  reloadValue_ = 0;
  previousValue_ = 0;
  if (power) {
    halt_ = false;
    pendingHalt_ = false;
  } else {
    pendingHalt_ = halt_;
  }
}

void LengthCounter::setEnabled(bool enabled) {
  enabled_ = enabled;
  if (!enabled) counter_ = 0;
}

void LengthCounter::load(std::uint8_t index) {
  if (!enabled_) return;
  reloadValue_ = LENGTH_TABLE[index & 0x1FU];
  previousValue_ = counter_;
}

void LengthCounter::clock() {
  if (counter_ > 0 && !halt_) --counter_;
}

void LengthCounter::endCycle() {
  if (reloadValue_ != 0) {
    // 这个周期里计数被时钟改变过（非零时减 1）则放弃载入。
    if (counter_ == previousValue_) counter_ = reloadValue_;
    reloadValue_ = 0;
  }
  halt_ = pendingHalt_;
}

// ---------------------------------------------------------------- 包络

void Envelope::reset() {
  start_ = false;
  constant_ = false;
  period_ = 0;
  divider_ = 0;
  decay_ = 0;
}

void Envelope::write(std::uint8_t value) {
  constant_ = (value & 0x10U) != 0;
  period_ = value & 0x0FU;
}

void Envelope::clock(bool loop) {
  if (start_) {
    start_ = false;
    decay_ = 15;
    divider_ = period_;
    return;
  }
  if (divider_ == 0) {
    divider_ = period_;
    if (decay_ > 0) {
      --decay_;
    } else if (loop) {
      decay_ = 15;
    }
  } else {
    --divider_;
  }
}

// ---------------------------------------------------------------- 方波

void PulseChannel::reset(bool power) {
  length_.reset(power);
  if (power) {
    envelope_.reset();
    duty_ = 0;
    sequence_ = 0;
    period_ = 0;
    timer_ = 2;
    sweepEnabled_ = false;
    sweepNegate_ = false;
    sweepReload_ = false;
    sweepPeriod_ = 0;
    sweepShift_ = 0;
    sweepDivider_ = 0;
  }
}

void PulseChannel::write(unsigned reg, std::uint8_t value) {
  switch (reg & 3U) {
    case 0:
      duty_ = value >> 6;
      length_.setHalt((value & 0x20U) != 0);
      envelope_.write(value);
      break;
    case 1:
      sweepEnabled_ = (value & 0x80U) != 0;
      sweepPeriod_ = (value >> 4) & 7U;
      sweepNegate_ = (value & 0x08U) != 0;
      sweepShift_ = value & 7U;
      sweepReload_ = true;
      break;
    case 2:
      period_ = (period_ & 0x700U) | value;
      break;
    default:
      period_ = (period_ & 0x0FFU) | ((value & 7U) << 8);
      length_.load(static_cast<std::uint8_t>(value >> 3));
      sequence_ = 0;
      envelope_.restart();
      break;
  }
}

void PulseChannel::clockTimer() {
  if (--timer_ <= 0) {
    timer_ = static_cast<int>((period_ + 1) * 2);
    sequence_ = (sequence_ + 1) & 7U;
  }
}

int PulseChannel::targetPeriod() const {
  const int delta = static_cast<int>(period_ >> sweepShift_);
  const int current = static_cast<int>(period_);
  return sweepNegate_ ? current - delta - (onesComplement_ ? 1 : 0) : current + delta;
}

bool PulseChannel::sweepMuted() const { return period_ < MIN_PULSE_PERIOD || (!sweepNegate_ && targetPeriod() > MAX_PULSE_PERIOD); }

void PulseChannel::clockSweep() {
  const bool dividerZero = sweepDivider_ == 0;
  if (dividerZero && sweepEnabled_ && sweepShift_ > 0 && !sweepMuted()) period_ = static_cast<unsigned>(targetPeriod());
  if (dividerZero || sweepReload_) {
    sweepDivider_ = sweepPeriod_;
    sweepReload_ = false;
  } else {
    --sweepDivider_;
  }
}

std::uint8_t PulseChannel::output() const {
  if (!length_.active() || sweepMuted() || PULSE_DUTY[duty_][sequence_] == 0) return 0;
  return envelope_.volume();
}

// ---------------------------------------------------------------- 三角波

void TriangleChannel::reset(bool power) {
  length_.reset(power);
  if (power) {
    control_ = false;
    linearReload_ = false;
    linearReloadValue_ = 0;
    linear_ = 0;
    period_ = 0;
    timer_ = 1;
    sequence_ = 0;
  }
}

void TriangleChannel::write(unsigned reg, std::uint8_t value) {
  switch (reg & 3U) {
    case 0:
      control_ = (value & 0x80U) != 0;
      linearReloadValue_ = value & 0x7FU;
      length_.setHalt(control_);
      break;
    case 2:
      period_ = (period_ & 0x700U) | value;
      break;
    case 3:
      period_ = (period_ & 0x0FFU) | ((value & 7U) << 8);
      length_.load(static_cast<std::uint8_t>(value >> 3));
      linearReload_ = true;
      break;
    default:
      break;
  }
}

void TriangleChannel::clockTimer() {
  if (--timer_ <= 0) {
    timer_ = static_cast<int>(period_ + 1);
    // 周期小于 2 是超声波，不推进序列，避免输出 1.79 MHz 级的方波。
    if (length_.active() && linear_ > 0 && period_ >= 2) sequence_ = (sequence_ + 1) & 31U;
  }
}

void TriangleChannel::clockLinear() {
  if (linearReload_) {
    linear_ = linearReloadValue_;
  } else if (linear_ > 0) {
    --linear_;
  }
  if (!control_) linearReload_ = false;
}

std::uint8_t TriangleChannel::output() const { return TRIANGLE_SEQUENCE[sequence_]; }

// ---------------------------------------------------------------- 噪声

void NoiseChannel::reset(bool power) {
  length_.reset(power);
  if (power) {
    envelope_.reset();
    shortMode_ = false;
    period_ = (*table_)[0];
    timer_ = static_cast<int>(period_);
    shift_ = 1;
  }
}

void NoiseChannel::write(unsigned reg, std::uint8_t value) {
  switch (reg & 3U) {
    case 0:
      length_.setHalt((value & 0x20U) != 0);
      envelope_.write(value);
      break;
    case 2:
      shortMode_ = (value & 0x80U) != 0;
      period_ = (*table_)[value & 0x0FU];
      break;
    case 3:
      length_.load(static_cast<std::uint8_t>(value >> 3));
      envelope_.restart();
      break;
    default:
      break;
  }
}

void NoiseChannel::clockTimer() {
  if (--timer_ <= 0) {
    timer_ = static_cast<int>(period_);
    const unsigned feedback = (shift_ & 1U) ^ ((shift_ >> (shortMode_ ? 6U : 1U)) & 1U);
    shift_ = static_cast<std::uint16_t>((shift_ >> 1) | (feedback << 14));
  }
}

std::uint8_t NoiseChannel::output() const {
  if ((shift_ & 1U) != 0 || !length_.active()) return 0;
  return envelope_.volume();
}

// ---------------------------------------------------------------- DMC

void DmcChannel::reset(bool power) {
  if (power) {
    irqEnabled_ = false;
    loop_ = false;
    period_ = (*table_)[0];
    timer_ = static_cast<int>(period_);
    sampleAddress_ = 0xC000;
    sampleLength_ = 1;
    address_ = 0xC000;
    buffer_ = 0;
    bufferEmpty_ = true;
    shift_ = 0;
    bitsLeft_ = 8;
    silence_ = true;
    level_ = 0;
  }
  irqFlag_ = false;
  bytesRemaining_ = 0;
  cancelDma();
}

void DmcChannel::cancelDma() {
  startDelay_ = 0;
  wantDma_ = false;
  dmaInFlight_ = false;
}

void DmcChannel::write(unsigned reg, std::uint8_t value) {
  switch (reg & 3U) {
    case 0:
      irqEnabled_ = (value & 0x80U) != 0;
      loop_ = (value & 0x40U) != 0;
      period_ = (*table_)[value & 0x0FU];
      if (!irqEnabled_) irqFlag_ = false;
      break;
    case 1:
      level_ = value & 0x7FU;
      break;
    case 2:
      sampleAddress_ = static_cast<std::uint16_t>(0xC000U | (static_cast<unsigned>(value) << 6));
      break;
    default:
      sampleLength_ = static_cast<std::uint16_t>((static_cast<unsigned>(value) << 4) | 1U);
      break;
  }
}

void DmcChannel::setEnabled(bool enabled, unsigned startDelay) {
  if (!enabled) {
    bytesRemaining_ = 0;
    startDelay_ = 0;
    return;
  }
  if (bytesRemaining_ == 0) {
    restartSample();
    startDelay_ = startDelay;
    if (startDelay_ == 0) requestIfNeeded();
  }
}

void DmcChannel::restartSample() {
  address_ = sampleAddress_;
  bytesRemaining_ = sampleLength_;
}

void DmcChannel::requestIfNeeded() {
  if (bytesRemaining_ > 0 && bufferEmpty_ && !dmaInFlight_) wantDma_ = true;
}

void DmcChannel::clock() {
  if (startDelay_ > 0 && --startDelay_ == 0) requestIfNeeded();
  if (--timer_ <= 0) {
    timer_ = static_cast<int>(period_);
    clockOutput();
  }
}

void DmcChannel::clockOutput() {
  if (!silence_) {
    if ((shift_ & 1U) != 0) {
      if (level_ <= DMC_MAX_LEVEL) level_ = static_cast<std::uint8_t>(level_ + 2);
    } else if (level_ >= 2) {
      level_ = static_cast<std::uint8_t>(level_ - 2);
    }
  }
  shift_ = static_cast<std::uint8_t>(shift_ >> 1);
  if (--bitsLeft_ == 0) {
    bitsLeft_ = 8;
    if (bufferEmpty_) {
      silence_ = true;
    } else {
      silence_ = false;
      shift_ = buffer_;
      bufferEmpty_ = true;
      requestIfNeeded();
    }
  }
}

bool DmcChannel::takeDmaRequest() {
  if (!wantDma_) return false;
  wantDma_ = false;
  dmaInFlight_ = true;
  return true;
}

void DmcChannel::deliverSample(std::uint8_t byte) {
  dmaInFlight_ = false;
  buffer_ = byte;
  bufferEmpty_ = false;
  if (bytesRemaining_ == 0) return;
  address_ = static_cast<std::uint16_t>((address_ + 1U) | 0x8000U);
  if (--bytesRemaining_ == 0) {
    if (loop_) {
      restartSample();
    } else if (irqEnabled_) {
      irqFlag_ = true;
    }
  }
}

std::int64_t DmcChannel::cyclesUntilRequest() const {
  if (dmaInFlight_ || bytesRemaining_ == 0) return -1;
  std::int64_t best = -1;
  if (startDelay_ > 0 && bufferEmpty_) best = startDelay_;
  if (!bufferEmpty_) {
    // 当前字节还剩 bitsLeft_ 个输出时钟：第一个在 timer_ 个周期后，之后每个隔 period_ 个周期。
    const std::int64_t endOfByte = timer_ + static_cast<std::int64_t>(bitsLeft_ - 1) * period_;
    if (best < 0 || endOfByte < best) best = endOfByte;
  }
  return best;
}

}  // namespace ember::logic::hardware::apu
