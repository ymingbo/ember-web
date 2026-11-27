/*
 * ------------------------------------------------------------------------
 * 名称：tape-analysis.cpp
 * 说明：磁带的时间轴分析的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "logic/media/tape-analysis.hpp"

#include <algorithm>
#include <cstdlib>

namespace ember::logic::media {

namespace {

namespace hw = contracts::hardware;

/** 一次从来源读的帧数。 */
constexpr std::size_t CHUNK_FRAMES = 16384;
/** 满幅。 */
constexpr std::int32_t FULL_SCALE = 32768;

/** 分析过程中的累计状态。 */
class Analyzer {
 public:
  /** 处理一批帧（左右交错）。 */
  void feed(const std::int16_t* interleaved, std::size_t frames) {
    for (std::size_t index = 0; index < frames; ++index) {
      windowPeak_ = std::max(windowPeak_, std::abs(static_cast<std::int32_t>(interleaved[index * 2 + 1])));
      ++windowFill_;
      ++totalFrames_;
      if (windowFill_ == TAPE_WINDOW_FRAMES) closeWindow();
    }
  }

  /** 读完：收尾最后不满一个窗口的部分，给出结果。 */
  TapeAnalysis finish() {
    if (windowFill_ > 0) closeWindow();
    if (stepWindows_ > 0) closeStep();
    closeBlank(windowIndex_);
    TapeAnalysis result;
    result.duration = static_cast<double>(totalFrames_) / hw::TapeSource::SAMPLE_RATE;
    result.step = static_cast<double>(TAPE_WINDOW_FRAMES * TAPE_WINDOWS_PER_STEP) / hw::TapeSource::SAMPLE_RATE;
    result.peaks = std::move(peaks_);
    result.blanks = std::move(blanks_);
    return result;
  }

  [[nodiscard]] std::uint64_t totalFrames() const { return totalFrames_; }

 private:
  void closeWindow() {
    stepPeak_ = std::max(stepPeak_, windowPeak_);
    if (windowPeak_ < TAPE_SILENCE_LEVEL) {
      if (silentRun_ == 0) silentStart_ = windowIndex_;
      ++silentRun_;
    } else {
      closeBlank(windowIndex_);
    }
    ++windowIndex_;
    windowPeak_ = 0;
    windowFill_ = 0;
    if (++stepWindows_ == TAPE_WINDOWS_PER_STEP) closeStep();
  }

  /** 静音窗口连续到 endWindow（不含）为止；够长就记成一段空白。 */
  void closeBlank(std::uint64_t endWindow) {
    if (silentRun_ >= TAPE_BLANK_WINDOWS) {
      const std::uint64_t endFrame = std::min(endWindow * TAPE_WINDOW_FRAMES, totalFrames_);
      blanks_.emplace_back(static_cast<double>(silentStart_ * TAPE_WINDOW_FRAMES) / hw::TapeSource::SAMPLE_RATE, static_cast<double>(endFrame) / hw::TapeSource::SAMPLE_RATE);
    }
    silentRun_ = 0;
  }

  void closeStep() {
    // 向上取整：只要有声音就至少是 1
    peaks_.push_back(static_cast<std::uint8_t>((stepPeak_ * 100 + FULL_SCALE - 1) / FULL_SCALE));
    stepPeak_ = 0;
    stepWindows_ = 0;
  }

  std::int32_t windowPeak_{0};
  std::uint64_t windowFill_{0};
  std::uint64_t windowIndex_{0};
  std::uint64_t totalFrames_{0};
  std::int32_t stepPeak_{0};
  std::uint64_t stepWindows_{0};
  std::uint64_t silentRun_{0};
  std::uint64_t silentStart_{0};
  std::vector<std::uint8_t> peaks_;
  std::vector<std::pair<double, double>> blanks_;
};

}  // namespace

std::optional<TapeAnalysis> analyzeTape(hw::TapeSource& source, const std::function<void(double)>& onProgress, const std::atomic<bool>& cancelled) {
  const double expected = source.durationSeconds() * hw::TapeSource::SAMPLE_RATE;
  Analyzer analyzer;
  std::vector<std::int16_t> chunk(CHUNK_FRAMES * 2);
  for (;;) {
    if (cancelled.load()) return std::nullopt;
    const std::size_t frames = source.read(chunk.data(), CHUNK_FRAMES);
    if (frames == 0) break;
    analyzer.feed(chunk.data(), frames);
    if (onProgress && expected > 0) onProgress(std::min(1.0, static_cast<double>(analyzer.totalFrames()) / expected));
  }
  if (cancelled.load()) return std::nullopt;
  return analyzer.finish();
}

}  // namespace ember::logic::media
