/*
 * ------------------------------------------------------------------------
 * 名称：dac-retimer.cpp
 * 说明：DAC 写入重新定时的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/psg-um6576/dac-retimer.hpp"

#include <algorithm>

namespace ember::logic::hardware {

namespace {

/** 间隔相对估计值的比例落在这个范围内，算“正常间隔”。 */
constexpr double NORMAL_LOW = 0.8;
constexpr double NORMAL_HIGH = 1.25;
/** 估计间隔的平滑系数：前 64 个间隔收敛快，之后平稳。 */
constexpr double FAST_SMOOTHING = 1.0 / 16.0;
constexpr double SLOW_SMOOTHING = 1.0 / 128.0;
constexpr unsigned FAST_WRITES = 64;
/** 输出间隔按水位微调的最大比例。 */
constexpr double MAX_RATE_ADJUST = 0.05;

}  // namespace

DacRetimer::DacRetimer(double cpuHz) : breakCycles_(cpuHz * BREAK_SECONDS) {}

void DacRetimer::reset() {
  resetStream();
  seen_ = false;
  lastPush_ = 0;
  hasDirect_ = false;
}

void DacRetimer::resetStream() {
  queue_.clear();
  interval_ = 0;
  stable_ = 0;
  writes_ = 0;
  buffered_ = false;
  playing_ = false;
}

void DacRetimer::direct(std::uint8_t level) {
  directLevel_ = level;
  hasDirect_ = true;
}

void DacRetimer::push(std::int64_t cycle, std::uint8_t level) {
  const double gap = static_cast<double>(cycle - lastPush_);
  const bool first = !seen_;
  seen_ = true;
  lastPush_ = cycle;
  if (first || gap > breakCycles_) {
    resetStream();
    direct(level);
    return;
  }
  if (interval_ <= 0) {
    interval_ = std::max(gap, 1.0);
    direct(level);
    return;
  }
  const double ratio = gap / interval_;
  // 停顿与随后的补写都计入平均，所以估计值是平均的到达间隔，缓冲水位才不会单向漂移。
  interval_ += (gap - interval_) * (writes_ < FAST_WRITES ? FAST_SMOOTHING : SLOW_SMOOTHING);
  ++writes_;
  if (ratio >= NORMAL_LOW && ratio <= NORMAL_HIGH) ++stable_;
  if (!buffered_) {
    if (stable_ < STABLE_WRITES) {
      direct(level);
      return;
    }
    buffered_ = true;
    hasDirect_ = false;
  }
  queue_.push_back(level);
  if (queue_.size() > CAPACITY) queue_.pop_front();
  if (!playing_ && queue_.size() >= PREFILL) {
    playing_ = true;
    nextPlay_ = static_cast<double>(cycle);
  }
}

bool DacRetimer::poll(std::int64_t cycle, std::uint8_t& level) {
  if (hasDirect_) {
    hasDirect_ = false;
    level = directLevel_;
    return true;
  }
  if (!playing_ || static_cast<double>(cycle) < nextPlay_) return false;
  if (queue_.empty()) {
    // 缓冲抽空：停止输出，等再攒够。
    playing_ = false;
    return false;
  }
  level = queue_.front();
  queue_.pop_front();
  const double deviation = (static_cast<double>(queue_.size()) - static_cast<double>(PREFILL)) / static_cast<double>(PREFILL);
  nextPlay_ += interval_ * (1.0 - MAX_RATE_ADJUST * std::clamp(deviation, -1.0, 1.0));
  return true;
}

}  // namespace ember::logic::hardware
