/*
 * ------------------------------------------------------------------------
 * 名称：tape-deck.cpp
 * 说明：磁带机的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/tape-deck/tape-deck.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

/** 帧号换算的容差，使事件时间与帧号互相换算时不因浮点误差差 1 帧。 */
constexpr double FRAME_EPSILON = 1e-6;
/** 事件时间取整时留的余量（占一帧的比例），比 FRAME_EPSILON 小。 */
constexpr double TICK_EPSILON = 1e-9;
/** 声音电平的比例：满幅 16 位采样换成这个幅度。 */
constexpr double AUDIO_SCALE = 0.8;
/** 录音里机器声音的比例：满幅浮点采样换成 16 位。 */
constexpr double RECORD_AUDIO_SCALE = 32767.0;
/** 录音缓冲攒到这么多采样就交给去向。 */
constexpr std::size_t RECORD_FLUSH_SAMPLES = 16384;

}  // namespace

TapeDeck::TapeDeck(hw::Scheduler& scheduler, double masterHz) { bind(scheduler, masterHz); }

TapeDeck::~TapeDeck() {
  cancelEvent();
  try {
    if (recording()) stop();
  } catch (...) {
    // 析构时无法再报告，录音文件保留已写入的部分。
  }
}

void TapeDeck::bind(hw::Scheduler& scheduler, double masterHz) {
  if (masterHz <= 0) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "磁带机的主时钟频率必须大于 0");
  unbind();
  scheduler_ = &scheduler;
  masterHz_ = masterHz;
  ticksPerFrame_ = masterHz / hw::TapeSource::SAMPLE_RATE;
}

void TapeDeck::unbind() {
  // 先在原来的调度器上停止；录音写完时出错也要解除绑定，异常照常抛出。
  try {
    stop();
  } catch (...) {
    cancelEvent();
    scheduler_ = nullptr;
    throw;
  }
  cancelEvent();
  scheduler_ = nullptr;
}

void TapeDeck::unbind(const hw::Scheduler& scheduler) {
  if (scheduler_ == &scheduler) unbind();
}

// ---------------------------------------------------------------- 时间换算

std::uint64_t TapeDeck::frameAt(hw::MasterTime now) const {
  const double elapsed = static_cast<double>(now) - anchorTick_;
  if (elapsed <= 0) return anchorFrame_;
  return anchorFrame_ + static_cast<std::uint64_t>(std::floor(elapsed / ticksPerFrame_ + FRAME_EPSILON));
}

hw::MasterTime TapeDeck::tickOf(std::uint64_t frame) const {
  const double ticks = anchorTick_ + static_cast<double>(frame - anchorFrame_) * ticksPerFrame_ - TICK_EPSILON * ticksPerFrame_;
  return static_cast<hw::MasterTime>(std::ceil(ticks));
}

// ---------------------------------------------------------------- 播放处理

bool TapeDeck::schmitt(bool state, std::int16_t sample) const {
  if (sample >= comparator_.high) return true;
  if (sample <= comparator_.low) return false;
  return state;
}

bool TapeDeck::ensureBuffered(std::uint64_t frame) {
  while (frame >= bufferBase_ + buffer_.size()) {
    if (endOfSource_ || source_ == nullptr) return false;
    std::vector<std::int16_t> chunk(CHUNK_FRAMES * 2);
    const std::size_t count = source_->read(chunk.data(), CHUNK_FRAMES);
    if (count == 0) {
      endOfSource_ = true;
      endFrame_ = bufferBase_ + buffer_.size();
      return false;
    }
    for (std::size_t index = 0; index < count; ++index) buffer_.push_back(Frame{chunk[index * 2], chunk[index * 2 + 1]});
  }
  return true;
}

void TapeDeck::processFrame(const Frame& frame) {
  schmittState_ = schmitt(schmittState_, frame[1]);
  if (audioRate_ == 0) return;
  audioSum_ += frame[0];
  audioCount_ += 1.0;
  audioPhase_ += 1.0;
  if (audioPhase_ >= framesPerAudioSample_) {
    audioPending_.push_back(static_cast<float>(audioSum_ / audioCount_ / 32768.0 * AUDIO_SCALE));
    audioPhase_ -= framesPerAudioSample_;
    audioSum_ = 0;
    audioCount_ = 0;
    if (audioPending_.size() > audioRate_) audioPending_.erase(audioPending_.begin(), audioPending_.begin() + static_cast<std::ptrdiff_t>(audioRate_ / 2));
  }
}

void TapeDeck::advanceTo(hw::MasterTime now) {
  if (mode_ != hw::TapeMode::Playing) return;
  const std::uint64_t target = frameAt(now);
  while (processed_ <= target) {
    if (!ensureBuffered(processed_)) {
      finishPlayback();
      return;
    }
    processFrame(buffer_[static_cast<std::size_t>(processed_ - bufferBase_)]);
    ++processed_;
  }
  while (bufferBase_ < processed_ && !buffer_.empty()) {
    buffer_.pop_front();
    ++bufferBase_;
  }
}

void TapeDeck::finishPlayback() {
  mode_ = hw::TapeMode::Stopped;
  processed_ = endFrame_;
  cancelEvent();
}

void TapeDeck::rewindSource() {
  if (source_ != nullptr) source_->rewind();
  buffer_.clear();
  bufferBase_ = 0;
  processed_ = 0;
  endOfSource_ = false;
  endFrame_ = 0;
  schmittState_ = schmitt(false, 0);
}

// ---------------------------------------------------------------- 边沿事件

void TapeDeck::cancelEvent() {
  if (eventId_ != hw::NO_EVENT && scheduler_ != nullptr) scheduler_->cancel(eventId_);
  eventId_ = hw::NO_EVENT;
}

void TapeDeck::scheduleNextEdge() {
  cancelEvent();
  if (!listener_ || mode_ != hw::TapeMode::Playing) return;
  if (scanFrame_ < processed_) {
    scanFrame_ = processed_;
    scanState_ = schmittState_;
  }
  const std::uint64_t limit = scanFrame_ + SCAN_FRAMES;
  const bool previousLevel = scanState_ != comparator_.invert;
  while (scanFrame_ < limit) {
    if (!ensureBuffered(scanFrame_)) {
      eventId_ = scheduler_->scheduleAt(tickOf(endFrame_), *this, TagEnd);
      return;
    }
    const bool state = schmitt(scanState_, buffer_[static_cast<std::size_t>(scanFrame_ - bufferBase_)][1]);
    const bool levelNow = state != comparator_.invert;
    scanState_ = state;
    if (levelNow != previousLevel) {
      pendingEdgeFrame_ = scanFrame_;
      pendingEdgeLevel_ = levelNow;
      ++scanFrame_;
      eventId_ = scheduler_->scheduleAt(tickOf(pendingEdgeFrame_), *this, TagEdge);
      return;
    }
    ++scanFrame_;
  }
  eventId_ = scheduler_->scheduleAt(tickOf(scanFrame_), *this, TagRescan);
}

void TapeDeck::onEvent(hw::MasterTime when, std::uint32_t tag) {
  eventId_ = hw::NO_EVENT;
  if (mode_ != hw::TapeMode::Playing) return;
  advanceTo(when);
  if (mode_ != hw::TapeMode::Playing) return;
  if (tag == TagEdge) {
    const std::uint64_t delta = pendingEdgeFrame_ - lastEdgeFrame_;
    lastEdgeFrame_ = pendingEdgeFrame_;
    if (listener_) listener_(pendingEdgeLevel_, static_cast<std::uint32_t>(delta));
  }
  scheduleNextEdge();
}

// ---------------------------------------------------------------- TapeControl

hw::TapeMode TapeDeck::mode() {
  advanceTo(now());
  return mode_;
}

double TapeDeck::positionSeconds() {
  const hw::MasterTime current = now();
  if (mode_ == hw::TapeMode::Recording) return std::max(0.0, recordStartSeconds_ + (static_cast<double>(current) - recordStartTick_) / masterHz_);
  advanceTo(current);
  const std::uint64_t frame = mode_ == hw::TapeMode::Playing ? frameAt(current) : processed_;
  return static_cast<double>(frame) / hw::TapeSource::SAMPLE_RATE;
}

void TapeDeck::load(std::unique_ptr<hw::TapeSource> source, std::string name) {
  eject();
  source_ = std::move(source);
  name_ = std::move(name);
}

void TapeDeck::play() {
  if (scheduler_ == nullptr || source_ == nullptr || mode_ != hw::TapeMode::Stopped) return;
  if (endOfSource_ && processed_ >= endFrame_) rewindSource();
  mode_ = hw::TapeMode::Playing;
  anchorTick_ = static_cast<double>(now());
  anchorFrame_ = processed_;
  scanFrame_ = processed_;
  scanState_ = schmittState_;
  lastEdgeFrame_ = processed_;
  scheduleNextEdge();
}

void TapeDeck::seek(double seconds) {
  if (source_ == nullptr || mode_ == hw::TapeMode::Recording) return;
  // 先把播放处理到现在：此时磁带可能刚好放完而停止
  advanceTo(now());
  const double total = source_->durationSeconds();
  double clamped = std::max(0.0, seconds);
  if (total > 0) clamped = std::min(clamped, total);
  const auto frame = static_cast<std::uint64_t>(std::llround(clamped * hw::TapeSource::SAMPLE_RATE));
  const bool playing = mode_ == hw::TapeMode::Playing;
  if (playing) cancelEvent();
  source_->seek(frame);
  buffer_.clear();
  bufferBase_ = frame;
  processed_ = frame;
  endOfSource_ = false;
  endFrame_ = 0;
  schmittState_ = schmitt(false, 0);
  audioPending_.clear();
  audioPhase_ = 0;
  audioSum_ = 0;
  audioCount_ = 0;
  if (!playing) return;
  anchorTick_ = static_cast<double>(now());
  anchorFrame_ = frame;
  scanFrame_ = frame;
  scanState_ = schmittState_;
  lastEdgeFrame_ = frame;
  scheduleNextEdge();
}

void TapeDeck::record(std::unique_ptr<hw::TapeSink> sink, std::string name, double startSeconds) {
  if (scheduler_ == nullptr) return;
  eject();
  sink_ = std::move(sink);
  name_ = std::move(name);
  mode_ = hw::TapeMode::Recording;
  recordStartTick_ = static_cast<double>(now());
  recordStartSeconds_ = std::max(0.0, startSeconds);
  recordedSamples_ = 0;
  recordLevel_ = driven_;
  recordLineUsed_ = driven_;
  recordBuffer_.clear();
  recordMixed_ = 0;
}

void TapeDeck::stop() {
  if (mode_ == hw::TapeMode::Playing) {
    advanceTo(now());
    mode_ = hw::TapeMode::Stopped;
    cancelEvent();
  } else if (mode_ == hw::TapeMode::Recording) {
    renderRecording(now());
    flushRecording(true);
    // 先让磁带机回到空闲，再写完文件：写完时出错抛出异常，状态也是一致的。
    const std::unique_ptr<hw::TapeSink> sink = std::move(sink_);
    name_.clear();
    mode_ = hw::TapeMode::Stopped;
    if (sink != nullptr) sink->finish();
  }
}

void TapeDeck::eject() {
  stop();
  source_.reset();
  sink_.reset();
  name_.clear();
  buffer_.clear();
  bufferBase_ = 0;
  processed_ = 0;
  endOfSource_ = false;
  endFrame_ = 0;
  schmittState_ = schmitt(false, 0);
  audioPending_.clear();
  audioPhase_ = 0;
  audioSum_ = 0;
  audioCount_ = 0;
}

// ---------------------------------------------------------------- TapeLine

bool TapeDeck::level() {
  advanceTo(now());
  return schmittState_ != comparator_.invert;
}

void TapeDeck::setComparator(const hw::TapeComparator& comparator) {
  comparator_ = comparator;
  // 没有在播放时，数据线停在“静音”对应的状态（取反的比较器静音时读 0），不留着上一个比较器的状态。
  if (mode_ != hw::TapeMode::Playing) schmittState_ = schmitt(schmittState_, 0);
  if (mode_ == hw::TapeMode::Playing) {
    advanceTo(now());
    scanFrame_ = processed_;
    scanState_ = schmittState_;
    scheduleNextEdge();
  }
}

void TapeDeck::setEdgeListener(std::function<void(bool, std::uint32_t)> listener) {
  listener_ = std::move(listener);
  if (mode_ == hw::TapeMode::Playing) {
    advanceTo(now());
    scanFrame_ = processed_;
    scanState_ = schmittState_;
    lastEdgeFrame_ = processed_;
    scheduleNextEdge();
  }
}

void TapeDeck::drive(bool high) {
  if (mode_ == hw::TapeMode::Recording) {
    renderRecording(now());
    recordLevel_ = high;
    if (high) recordLineUsed_ = true;
  }
  driven_ = high;
}

// ---------------------------------------------------------------- 录音

void TapeDeck::renderRecording(hw::MasterTime now) {
  const double elapsed = static_cast<double>(now) - recordStartTick_;
  if (elapsed <= 0) return;
  const auto due = static_cast<std::uint64_t>(std::floor(elapsed * hw::TapeSink::SAMPLE_RATE / masterHz_));
  if (due <= recordedSamples_) return;
  const std::int16_t value = !recordLineUsed_ ? std::int16_t{0} : (recordLevel_ ? RECORD_AMPLITUDE : static_cast<std::int16_t>(-RECORD_AMPLITUDE));
  recordBuffer_.insert(recordBuffer_.end(), static_cast<std::size_t>(due - recordedSamples_), value);
  recordedSamples_ = due;
}

void TapeDeck::mixRecordedAudio(const std::vector<float>& samples) {
  renderRecording(now());
  const std::size_t pending = recordBuffer_.size() - recordMixed_;
  if (pending > 0 && !samples.empty()) {
    // 机器声音的采样率高于录音：这一段时间里的机器采样平均后叠到对应的录音采样上
    const std::size_t total = samples.size();
    for (std::size_t index = 0; index < pending; ++index) {
      const std::size_t first = index * total / pending;
      const std::size_t last = std::max(first + 1, (index + 1) * total / pending);
      double sum = 0;
      for (std::size_t position = first; position < last; ++position) sum += samples[position];
      const double mixed = recordBuffer_[recordMixed_ + index] + sum / static_cast<double>(last - first) * RECORD_AUDIO_SCALE;
      recordBuffer_[recordMixed_ + index] = static_cast<std::int16_t>(std::clamp(std::lround(mixed), -32768L, 32767L));
    }
  }
  recordMixed_ = recordBuffer_.size();
  if (recordMixed_ >= RECORD_FLUSH_SAMPLES) flushRecording(false);
}

void TapeDeck::flushRecording(bool all) {
  const std::size_t count = all ? recordBuffer_.size() : recordMixed_;
  if (sink_ != nullptr && count > 0) sink_->write(recordBuffer_.data(), count);
  recordBuffer_.erase(recordBuffer_.begin(), recordBuffer_.begin() + static_cast<std::ptrdiff_t>(count));
  recordMixed_ = all ? 0 : recordMixed_ - count;
}

// ---------------------------------------------------------------- 声音

void TapeDeck::setAudioRate(unsigned rate) {
  audioRate_ = rate;
  framesPerAudioSample_ = rate == 0 ? 0.0 : static_cast<double>(hw::TapeSource::SAMPLE_RATE) / rate;
  audioPhase_ = 0;
  audioSum_ = 0;
  audioCount_ = 0;
  audioPending_.clear();
}

void TapeDeck::mixAudio(std::vector<float>& samples) {
  advanceTo(now());
  const std::size_t count = std::min(samples.size(), audioPending_.size());
  for (std::size_t index = 0; index < count; ++index) samples[index] += audioPending_[index];
  audioPending_.erase(audioPending_.begin(), audioPending_.begin() + static_cast<std::ptrdiff_t>(count));
  if (recording()) mixRecordedAudio(samples);
}

}  // namespace ember::logic::hardware
