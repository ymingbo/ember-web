/*
 * ------------------------------------------------------------------------
 * 名称：speech-lpc.cpp
 * 说明：学习机语音芯片的实现：接收缓冲、码流解析与 LPC-10 合成。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/speech-lpc/speech-lpc.hpp"

#include <cmath>

#include "contracts/hardware/hardware-error.hpp"
#include "logic/hardware/speech-lpc/speech-lpc-tables.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;
namespace tables = speech_tables;

/** 命令字节：选 D6 码表、选 PE 码表；步步高只有一种命令字节（D6 码表）。 */
constexpr std::uint8_t COMMAND_D6 = 0x0A;
constexpr std::uint8_t COMMAND_PE = 0x0B;
constexpr std::uint8_t COMMAND_BBG = 0xD6;
/** 播放结束后，收到这个字节表示确认，重新开始。 */
constexpr std::uint8_t ACKNOWLEDGE = 0x0F;
/** 状态位：缓冲未满。 */
constexpr std::uint8_t STATUS_READY = 0x80;
/** 状态低 4 位：播放结束。 */
constexpr std::uint8_t STATUS_ENDED = 0x0F;
/** 能量索引：0 是静音帧，15 是结束帧。 */
constexpr unsigned ENERGY_SILENT = 0;
constexpr unsigned ENERGY_END = 15;
/** 定点数的小数位数（Q15）。 */
constexpr unsigned FRACTION_BITS = 15;
/** 滤波器输出的限幅。 */
constexpr int FILTER_CLAMP_POSITIVE = 27500;
constexpr int FILTER_CLAMP_NEGATIVE = -(FILTER_CLAMP_POSITIVE + 1);
/** 清音激励的幅度。 */
constexpr int UNVOICED_AMPLITUDE = 1408;
/** 激励的放大倍数。 */
constexpr int EXCITATION_GAIN = 8;
/** 音高每个采样减去的步长；浊音激励表按它的 1/16 取点。 */
constexpr std::int16_t PITCH_STEP = 16;
/** 清音与静音帧的音高周期。 */
constexpr std::int16_t IDLE_PITCH_PERIOD = 0x80;
/** 输出电平的比例：满幅 16 位采样换成这个幅度（没有真机数据，按与 PSG 的相对响度估定）。 */
constexpr double VOICE_SCALE = 0.5;

std::uint8_t reverseBits(std::uint8_t value) {
  std::uint8_t result = 0;
  for (unsigned bit = 0; bit < 8; ++bit) result = static_cast<std::uint8_t>((result << 1) | ((value >> bit) & 1U));
  return result;
}

std::int16_t signed16(std::uint16_t value) { return static_cast<std::int16_t>(value); }

/** 帧里第 index 个参数：0 能量，1 音高，2 起是 K1-K10。 */
template <typename FrameType>
auto& component(FrameType& frame, std::size_t index) {
  if (index == 0) return frame.energy;
  if (index == 1) return frame.pitch;
  return frame.k[index - 2];
}

}  // namespace

SpeechLpc::SpeechLpc(hw::Scheduler& scheduler, double masterHz, Variant variant) : scheduler_(scheduler), variant_(variant), ticksPerSample_(masterHz / SAMPLE_RATE) {
  if (masterHz <= 0) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "语音芯片的主时钟频率必须大于 0");
  reset(true);
}

hw::BusRead SpeechLpc::read(std::uint16_t /*address*/) {
  if (variant_ == Variant::Yuxing) return hw::driveBus(count_ < YUXING_BUSY_BYTES ? std::uint8_t{0x00} : STATUS_READY);
  std::uint8_t status = 0;
  if (count_ < FIFO_CAPACITY) status = static_cast<std::uint8_t>(status | STATUS_READY);
  if (ended_ || variant_ == Variant::Bbg) status = static_cast<std::uint8_t>(status | STATUS_ENDED);
  return hw::driveBus(status);
}

void SpeechLpc::write(std::uint16_t /*address*/, std::uint8_t value) {
  if (count_ >= FIFO_CAPACITY) return;
  fifo_[(head_ + count_) % fifo_.size()] = value;
  ++count_;
  settle();
  if (state_ == State::Run) scheduleTick();
}

void SpeechLpc::reset(bool /*power*/) {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  head_ = 0;
  count_ = 0;
  state_ = State::Startup;
  ended_ = false;
  useD6Tables_ = true;
  framesSynthesized_ = 0;
  restartSynth();
  emitLevel(0);
}

void SpeechLpc::restartSynth() {
  bitCache_ = 0;
  bitsLeft_ = 0;
  preloaded_ = 0;
  waitingNextFrame_ = false;
  previous_ = Frame{};
  current_ = Frame{};
  next_ = Frame{};
  needInterpolation_ = false;
  randomSeed_ = -1;
  currentPitch_ = 0;
  sampleIndex_ = 0;
  filterState_.fill(0);
  output_ = 0;
}

void SpeechLpc::fifoPop() {
  head_ = (head_ + 1) % fifo_.size();
  --count_;
}

void SpeechLpc::emitLevel(double level) {
  if (level == lastLevel_) return;
  lastLevel_ = level;
  if (onLevel) onLevel(level);
}

void SpeechLpc::scheduleTick() {
  if (eventId_ != hw::NO_EVENT) return;
  nextTickAt_ = static_cast<double>(scheduler_.now()) + ticksPerSample_;
  eventId_ = scheduler_.scheduleAt(static_cast<hw::MasterTime>(std::llround(nextTickAt_)), *this, 0);
}

void SpeechLpc::onEvent(hw::MasterTime /*when*/, std::uint32_t /*tag*/) {
  eventId_ = hw::NO_EVENT;
  if (state_ != State::Run) return;
  if (!waitingNextFrame_) {
    synthesizeSample();
    emitLevel(output_ / 32768.0 * VOICE_SCALE);
    if (++sampleIndex_ >= SAMPLES_PER_FRAME) {
      ++framesSynthesized_;
      previous_ = next_;
      current_ = next_;
      waitingNextFrame_ = true;
    }
  } else {
    emitLevel(0);
  }
  settle();
  if (state_ != State::Run) return;
  nextTickAt_ += ticksPerSample_;
  eventId_ = scheduler_.scheduleAt(static_cast<hw::MasterTime>(std::llround(nextTickAt_)), *this, 0);
}

void SpeechLpc::finishStream() {
  bitCache_ = 0;
  bitsLeft_ = 0;
  waitingNextFrame_ = false;
  emitLevel(0);
  if (variant_ == Variant::Yuxing) {
    // 没有确认字节：丢掉当前字节的剩余位，下一段紧接着开始。
    preloaded_ = 0;
    state_ = State::Preload;
    return;
  }
  state_ = State::Finished;
  ended_ = true;
}

void SpeechLpc::settle() {
  for (;;) {
    switch (state_) {
      case State::Startup: {
        if (variant_ == Variant::Yuxing) {
          // 没有命令字节：直接按 PE 码表读帧。
          useD6Tables_ = false;
          preloaded_ = 0;
          state_ = State::Preload;
          break;
        }
        if (count_ == 0) return;
        const std::uint8_t command = fifoAt(0);
        fifoPop();
        if (variant_ == Variant::Bbg) {
          if (command == COMMAND_BBG) {
            useD6Tables_ = true;
            preloaded_ = 0;
            state_ = State::Preload;
          }
        } else if (command == COMMAND_D6 || command == COMMAND_PE) {
          useD6Tables_ = command == COMMAND_D6;
          preloaded_ = 0;
          state_ = State::Preload;
        }
        break;
      }
      case State::Preload: {
        const FrameResult result = tryReadFrame(preloaded_ == 0 ? previous_ : next_, previous_);
        if (result == FrameResult::NeedMoreData) return;
        if (result == FrameResult::EndOfStream) {
          finishStream();
          break;
        }
        if (++preloaded_ == 2) {
          current_ = previous_;
          updateInterpolation();
          sampleIndex_ = 0;
          currentPitch_ = 0;
          waitingNextFrame_ = false;
          state_ = State::Run;
        }
        break;
      }
      case State::Run: {
        if (!waitingNextFrame_) return;
        const FrameResult result = tryReadFrame(next_, previous_);
        if (result == FrameResult::NeedMoreData) return;
        if (result == FrameResult::EndOfStream) {
          finishStream();
          break;
        }
        updateInterpolation();
        waitingNextFrame_ = false;
        sampleIndex_ = 0;
        return;
      }
      case State::Finished: {
        if (count_ == 0) return;
        const std::uint8_t byte = fifoAt(0);
        fifoPop();
        if (byte == ACKNOWLEDGE && variant_ == Variant::Subor) {
          head_ = 0;
          count_ = 0;
          restartSynth();
          ended_ = false;
          state_ = State::Startup;
        }
        break;
      }
    }
  }
}

SpeechLpc::FrameResult SpeechLpc::tryReadFrame(Frame& destination, const Frame& reference) {
  std::uint32_t cache = bitCache_;
  unsigned bitsLeft = bitsLeft_;
  std::size_t used = 0;
  bool starved = false;
  // 取 count 位（高位在前），字节按位反序使用；字节不够时记下 starved，返回 0。
  const auto take = [&](unsigned count) -> unsigned {
    while (bitsLeft < count) {
      if (used >= count_) {
        starved = true;
        return 0;
      }
      cache = (cache << 8) | reverseBits(fifoAt(used++));
      bitsLeft += 8;
    }
    const unsigned value = (cache >> (bitsLeft - count)) & ((1U << count) - 1U);
    bitsLeft -= count;
    cache &= (1U << bitsLeft) - 1U;
    return value;
  };

  const bool d6 = useD6Tables_;
  Frame frame{};
  FrameResult result = FrameResult::Decoded;
  const unsigned energyIndex = take(4);
  if (energyIndex == ENERGY_SILENT) {
    // 静音帧：全部参数为 0，不再读后面的位。
  } else if (energyIndex == ENERGY_END) {
    result = FrameResult::EndOfStream;
  } else {
    const unsigned repeat = take(1);
    const unsigned pitchIndex = take(d6 ? 7 : 6);
    frame.energy = d6 ? signed16(tables::D6_ENERGY[energyIndex]) : tables::PE_ENERGY[energyIndex];
    frame.pitch = d6 ? signed16(tables::D6_PITCH[pitchIndex]) : tables::PE_PITCH[pitchIndex];
    if (repeat != 0) {
      for (std::size_t index = 0; index < 4; ++index) frame.k[index] = reference.k[index];
    } else {
      const unsigned k1 = take(d6 ? 6 : 5);
      const unsigned k2 = take(d6 ? 6 : 5);
      const unsigned k3 = take(d6 ? 5 : 4);
      const unsigned k4 = take(d6 ? 5 : 4);
      frame.k[0] = d6 ? signed16(tables::D6_K1[k1]) : tables::PE_K1[k1];
      frame.k[1] = d6 ? signed16(tables::D6_K2[k2]) : tables::PE_K2[k2];
      frame.k[2] = d6 ? signed16(tables::D6_K3[k3]) : tables::PE_K3[k3];
      frame.k[3] = d6 ? signed16(tables::D6_K4[k4]) : tables::PE_K4[k4];
    }
    if (pitchIndex == 0) {
      // 清音帧：K5-K10 为 0。
    } else if (repeat != 0) {
      frame.k = reference.k;
    } else {
      const unsigned k5 = take(4);
      const unsigned k6 = take(4);
      const unsigned k7 = take(4);
      const unsigned k8 = take(3);
      const unsigned k9 = take(3);
      const unsigned k10 = take(3);
      frame.k[4] = d6 ? signed16(tables::D6_K5[k5]) : tables::PE_K5[k5];
      frame.k[5] = d6 ? signed16(tables::D6_K6[k6]) : tables::PE_K6[k6];
      frame.k[6] = d6 ? signed16(tables::D6_K7[k7]) : tables::PE_K7[k7];
      frame.k[7] = d6 ? signed16(tables::D6_K8[k8]) : tables::PE_K8[k8];
      frame.k[8] = d6 ? signed16(tables::D6_K9[k9]) : tables::PE_K9[k9];
      frame.k[9] = d6 ? signed16(tables::D6_K10[k10]) : tables::PE_K10[k10];
    }
  }
  if (starved) return FrameResult::NeedMoreData;

  for (std::size_t index = 0; index < used; ++index) fifoPop();
  bitCache_ = cache;
  bitsLeft_ = bitsLeft;
  if (result == FrameResult::EndOfStream) return result;
  destination = frame;
  return FrameResult::Decoded;
}

void SpeechLpc::updateInterpolation() {
  needInterpolation_ = true;
  if (previous_.energy == 0) {
    if (next_.pitch == 0 && next_.energy != 0) needInterpolation_ = false;
  } else if (previous_.pitch != 0) {
    if (next_.pitch == 0 && next_.energy != 0) needInterpolation_ = false;
  } else if (next_.pitch != 0) {
    needInterpolation_ = false;
  }
}

std::int16_t SpeechLpc::reloadPitch() {
  std::int16_t pitch = currentPitch_;
  if (needInterpolation_) {
    const int ratio = static_cast<int>((sampleIndex_ << FRACTION_BITS) / SAMPLES_PER_FRAME);
    for (std::size_t index = 0; index < ORDER + 2; ++index) {
      const std::int16_t from = component(previous_, index);
      auto difference = static_cast<std::int16_t>(component(next_, index) - from);
      difference = static_cast<std::int16_t>((difference * ratio) >> FRACTION_BITS);
      component(current_, index) = static_cast<std::int16_t>(difference + from);
    }
    pitch = static_cast<std::int16_t>(pitch + current_.pitch);
    if (pitch > 0) return pitch;
  }
  if (current_.pitch != 0) return static_cast<std::int16_t>(pitch + current_.pitch);
  return IDLE_PITCH_PERIOD;
}

bool SpeechLpc::nextRandomBit() {
  const auto seed = static_cast<std::int16_t>(randomSeed_ << 1);
  const int bit = ((seed >> 12) ^ (seed >> 13)) & 1;
  randomSeed_ = static_cast<std::int16_t>(seed | bit);
  return bit != 0;
}

void SpeechLpc::runFilter() {
  int sample = output_;
  for (std::size_t step = 0; step < ORDER; ++step) {
    const std::size_t index = ORDER - 1 - step;
    sample -= (current_.k[index] * filterState_[index]) >> FRACTION_BITS;
    if (sample > FILTER_CLAMP_POSITIVE) sample = FILTER_CLAMP_POSITIVE;
    else if (sample < FILTER_CLAMP_NEGATIVE) sample = FILTER_CLAMP_NEGATIVE;
    filterState_[index + 1] = static_cast<std::int16_t>(filterState_[index] + ((sample * current_.k[index]) >> FRACTION_BITS));
  }
  output_ = static_cast<std::int16_t>(sample);
  filterState_[0] = output_;
}

void SpeechLpc::synthesizeSample() {
  currentPitch_ = static_cast<std::int16_t>(currentPitch_ - PITCH_STEP);
  if (currentPitch_ < 0) currentPitch_ = reloadPitch();

  int excitation = 0;
  if (current_.pitch == 0) {
    if (current_.energy != 0) excitation = ((nextRandomBit() ? UNVOICED_AMPLITUDE : -UNVOICED_AMPLITUDE) * current_.energy) >> FRACTION_BITS;
  } else if (currentPitch_ >= 0 && static_cast<std::size_t>(currentPitch_) < tables::VOICED_EXCITATION.size()) {
    excitation = (tables::VOICED_EXCITATION[static_cast<std::size_t>(currentPitch_)] * current_.energy) >> FRACTION_BITS;
  }
  output_ = static_cast<std::int16_t>(excitation * EXCITATION_GAIN);
  runFilter();
}

}  // namespace ember::logic::hardware
