/*
 * ------------------------------------------------------------------------
 * 名称：psg-um6576.cpp
 * 说明：6576 PSG 的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/psg-um6576/psg-um6576.hpp"

#include <algorithm>
#include <cmath>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

constexpr std::uint8_t LENGTH_TABLE[32] = {10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14, 12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
constexpr std::uint8_t DUTY_TABLE[4] = {0b01000000, 0b01100000, 0b01111000, 0b10011111};
/** PAL 噪声周期表（CPU 周期）。 */
constexpr std::uint16_t NOISE_PERIODS[16] = {4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778};
/** 帧序列每一步的 CPU 周期数（PAL 4 步，约 50 Hz）。 */
constexpr std::int64_t FRAME_STEP_CYCLES = 8314;
/** DAC 满量程在混音里的幅度。 */
constexpr double DAC_SCALE = 0.5;
/** 最多保留的采样数（约 1 秒）。 */
constexpr std::size_t MAX_PENDING_SAMPLES = PsgUm6576::SAMPLE_RATE;

}  // namespace

PsgUm6576::PsgUm6576(const hw::Clock& clock, hw::ClockDomain cpuDomain, double cpuHz) : clock_(clock), cpuDomain_(cpuDomain), sampler_(cpuHz, SAMPLE_RATE), voiceSampler_(cpuHz, SAMPLE_RATE), dacSampler_(cpuHz, SAMPLE_RATE), dacRetimer_(cpuHz) {
  if (cpuDomain_.ticksPerCycle < 1 || cpuHz <= 0) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "PSG 的时钟参数必须大于 0");
  reset(true);
}

void PsgUm6576::reset(bool /*power*/) {
  cycle_ = cpuDomain_.toCycles(clock_.now());
  duty_.fill(0);
  pulsePeriod_.fill(0);
  pulseTimer_.fill(0);
  pulseSequence_.fill(0);
  trianglePeriod_ = 0;
  triangleTimer_ = 0;
  triangleSequence_ = 0;
  linearControl_ = false;
  linearReload_ = false;
  linearReloadValue_ = 0;
  linear_ = 0;
  noiseShort_ = false;
  noisePeriod_ = NOISE_PERIODS[0];
  noiseTimer_ = noisePeriod_;
  noiseShift_ = 1;
  envelopeStart_.fill(false);
  envelopeConstant_.fill(false);
  envelopePeriod_.fill(0);
  envelopeDivider_.fill(0);
  envelopeDecay_.fill(0);
  halt_.fill(false);
  lengths_.fill(0);
  enabled_.fill(false);
  frameCounter_ = 0;
  frameStepIndex_ = 0;
  dacLevel_ = 0;
  voiceLevel_ = 0;
  // 从当前时间重新开始，上电时的初始电平（三角波的静止电平）不产生阶跃。
  sampler_.reset(cycle_);
  level_ = mix();
  pending_.clear();
  voiceSampler_.reset(cycle_);
  voiceFilter_.reset();
  voicePending_.clear();
  dacSampler_.reset(cycle_);
  dacFilter_.reset();
  dacPending_.clear();
  dacRetimer_.reset();
}

hw::BusRead PsgUm6576::peek(std::uint16_t address) {
  if (address != 0x4015) return hw::releaseBus();
  std::uint8_t value = 0;
  for (unsigned channel = 0; channel < 4; ++channel) {
    if (lengths_[channel] > 0) value = static_cast<std::uint8_t>(value | (1U << channel));
  }
  return hw::driveBus(value, 0x0F);
}

hw::BusRead PsgUm6576::read(std::uint16_t address) {
  catchUp(clock_.now());
  return peek(address);
}

void PsgUm6576::write(std::uint16_t address, std::uint8_t value) {
  catchUp(clock_.now());
  if (address == 0x4015) {
    for (unsigned channel = 0; channel < 4; ++channel) {
      enabled_[channel] = (value >> channel & 1U) != 0;
      if (!enabled_[channel]) lengths_[channel] = 0;
    }
    return;
  }
  if (address < 0x4000 || address > 0x400F) return;
  const unsigned reg = address & 3;
  if (address < 0x4008) {
    const unsigned index = (address >> 2) & 1;
    switch (reg) {
      case 0:
        duty_[index] = static_cast<std::uint8_t>(value >> 6);
        halt_[index] = (value & 0x20) != 0;
        envelopeConstant_[index] = (value & 0x10) != 0;
        envelopePeriod_[index] = value & 0x0F;
        break;
      case 2:
        pulsePeriod_[index] = static_cast<std::uint16_t>((pulsePeriod_[index] & 0x700) | value);
        break;
      case 3:
        pulsePeriod_[index] = static_cast<std::uint16_t>((pulsePeriod_[index] & 0xFF) | ((value & 7) << 8));
        if (enabled_[index]) lengths_[index] = LENGTH_TABLE[value >> 3];
        pulseSequence_[index] = 0;
        envelopeStart_[index] = true;
        break;
      default:
        break;
    }
    return;
  }
  if (address < 0x400C) {
    switch (reg) {
      case 0:
        linearControl_ = (value & 0x80) != 0;
        halt_[2] = linearControl_;
        linearReloadValue_ = value & 0x7F;
        break;
      case 2:
        trianglePeriod_ = static_cast<std::uint16_t>((trianglePeriod_ & 0x700) | value);
        break;
      case 3:
        trianglePeriod_ = static_cast<std::uint16_t>((trianglePeriod_ & 0xFF) | ((value & 7) << 8));
        if (enabled_[2]) lengths_[2] = LENGTH_TABLE[value >> 3];
        linearReload_ = true;
        break;
      default:
        break;
    }
    return;
  }
  switch (reg) {
    case 0:
      halt_[3] = (value & 0x20) != 0;
      envelopeConstant_[2] = (value & 0x10) != 0;
      envelopePeriod_[2] = value & 0x0F;
      break;
    case 2:
      noiseShort_ = (value & 0x80) != 0;
      noisePeriod_ = NOISE_PERIODS[value & 0x0F];
      break;
    case 3:
      if (enabled_[3]) lengths_[3] = LENGTH_TABLE[value >> 3];
      envelopeStart_[2] = true;
      break;
    default:
      break;
  }
}

void PsgUm6576::catchUp(hw::MasterTime now) {
  const std::int64_t target = cpuDomain_.toCycles(now);
  while (cycle_ < target) stepCycle();
}

void PsgUm6576::setDacLevel(std::uint8_t level) {
  catchUp(clock_.now());
  dacRetimer_.push(cycle_, level);
}

void PsgUm6576::applyDacLevel(std::uint8_t level) {
  dacSampler_.addDelta(cycle_, (static_cast<double>(level) - static_cast<double>(dacLevel_)) / 255.0 * DAC_SCALE);
  dacLevel_ = level;
}

void PsgUm6576::setVoiceLevel(double level) {
  catchUp(clock_.now());
  voiceSampler_.addDelta(cycle_, level - voiceLevel_);
  voiceLevel_ = level;
}

void PsgUm6576::clockQuarter() {
  for (unsigned index = 0; index < 3; ++index) {
    const bool loop = halt_[index == 2 ? 3 : index];
    if (envelopeStart_[index]) {
      envelopeStart_[index] = false;
      envelopeDecay_[index] = 15;
      envelopeDivider_[index] = envelopePeriod_[index];
    } else if (envelopeDivider_[index] == 0) {
      envelopeDivider_[index] = envelopePeriod_[index];
      if (envelopeDecay_[index] > 0) {
        --envelopeDecay_[index];
      } else if (loop) {
        envelopeDecay_[index] = 15;
      }
    } else {
      --envelopeDivider_[index];
    }
  }
  if (linearReload_) {
    linear_ = linearReloadValue_;
  } else if (linear_ > 0) {
    --linear_;
  }
  if (!linearControl_) linearReload_ = false;
}

void PsgUm6576::clockHalf() {
  for (unsigned channel = 0; channel < 4; ++channel) {
    if (!halt_[channel] && lengths_[channel] > 0) --lengths_[channel];
  }
}

void PsgUm6576::frameStep() {
  clockQuarter();
  if ((frameStepIndex_ & 1U) != 0) clockHalf();
  frameStepIndex_ = (frameStepIndex_ + 1) & 3U;
}

double PsgUm6576::mix() const {
  double pulse = 0;
  for (unsigned index = 0; index < 2; ++index) {
    if (lengths_[index] == 0 || pulsePeriod_[index] < 8) continue;
    if (((DUTY_TABLE[duty_[index]] >> (7 - pulseSequence_[index])) & 1U) == 0) continue;
    pulse += envelopeConstant_[index] ? envelopePeriod_[index] : envelopeDecay_[index];
  }
  const double pulseOut = pulse == 0 ? 0.0 : 95.88 / (8128.0 / pulse + 100.0);
  const double triangle = triangleSequence_ < 16 ? 15.0 - triangleSequence_ : static_cast<double>(triangleSequence_) - 16.0;
  double noise = 0;
  if (lengths_[3] > 0 && (noiseShift_ & 1U) == 0) noise = envelopeConstant_[2] ? envelopePeriod_[2] : envelopeDecay_[2];
  const double tnd = triangle / 8227.0 + noise / 12241.0;
  const double tndOut = tnd == 0 ? 0.0 : 159.79 / (1.0 / tnd + 100.0);
  return pulseOut + tndOut;
}

void PsgUm6576::stepCycle() {
  std::uint8_t dacValue = 0;
  if (dacRetimer_.poll(cycle_, dacValue)) applyDacLevel(dacValue);
  // 方波的定时器每两个 CPU 周期走一步。
  if ((cycle_ & 1) == 0) {
    for (unsigned index = 0; index < 2; ++index) {
      if (pulseTimer_[index] == 0) {
        pulseTimer_[index] = pulsePeriod_[index];
        pulseSequence_[index] = (pulseSequence_[index] + 1) & 7U;
      } else {
        --pulseTimer_[index];
      }
    }
  }
  if (triangleTimer_ == 0) {
    triangleTimer_ = trianglePeriod_;
    if (lengths_[2] > 0 && linear_ > 0 && trianglePeriod_ >= 2) triangleSequence_ = (triangleSequence_ + 1) & 31U;
  } else {
    --triangleTimer_;
  }
  if (noiseTimer_ == 0) {
    noiseTimer_ = noisePeriod_;
    const unsigned feedback = (noiseShift_ & 1U) ^ ((noiseShift_ >> (noiseShort_ ? 6 : 1)) & 1U);
    noiseShift_ = static_cast<std::uint16_t>((noiseShift_ >> 1) | (feedback << 14));
  } else {
    --noiseTimer_;
  }
  if (++frameCounter_ >= FRAME_STEP_CYCLES) {
    frameCounter_ = 0;
    frameStep();
  }

  const double level = mix();
  if (level != level_) {
    sampler_.addDelta(cycle_, level - level_);
    level_ = level;
  }
  ++cycle_;
  sampler_.advance(cycle_, pending_);
  voiceSampler_.advance(cycle_, voicePending_);
  dacSampler_.advance(cycle_, dacPending_);
  if (pending_.size() > MAX_PENDING_SAMPLES) {
    const auto dropped = static_cast<std::ptrdiff_t>(MAX_PENDING_SAMPLES / 2);
    pending_.erase(pending_.begin(), pending_.begin() + dropped);
    voicePending_.erase(voicePending_.begin(), voicePending_.begin() + dropped);
    dacPending_.erase(dacPending_.begin(), dacPending_.begin() + dropped);
  }
}

void PsgUm6576::takeAudio(std::vector<float>& samples) {
  catchUp(clock_.now());
  samples.swap(pending_);
  pending_.clear();
  // 两个采样器按同样的节拍输出，个数相同。
  for (std::size_t index = 0; index < samples.size() && index < voicePending_.size() && index < dacPending_.size(); ++index) {
    samples[index] = std::clamp(samples[index] + voiceFilter_.process(voicePending_[index]) + dacFilter_.process(dacPending_[index]), -1.0F, 1.0F);
  }
  voicePending_.clear();
  dacPending_.clear();
}

}  // namespace ember::logic::hardware
