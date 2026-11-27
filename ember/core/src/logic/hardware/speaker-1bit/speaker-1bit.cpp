/*
 * ------------------------------------------------------------------------
 * 名称：speaker-1bit.cpp
 * 说明：一位扬声器的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/speaker-1bit/speaker-1bit.hpp"

#include <algorithm>

namespace ember::logic::hardware {

Speaker1Bit::Speaker1Bit(double masterHz, unsigned sampleRate) : ticksPerSample_(masterHz / static_cast<double>(sampleRate)) {}

void Speaker1Bit::reset() {
  toggles_.clear();
  position_ = 0;
  high_ = false;
}

void Speaker1Bit::toggle(contracts::hardware::MasterTime when) { toggles_.push_back(when); }

void Speaker1Bit::take(std::vector<float>& samples, contracts::hardware::MasterTime now) {
  samples.clear();
  const auto count = static_cast<std::size_t>(std::max(0.0, (static_cast<double>(now) - position_) / ticksPerSample_));
  samples.reserve(count);
  std::size_t consumed = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const double end = position_ + ticksPerSample_;
    double cursor = position_;
    double sum = 0;
    // 翻转之前的一段按当时的电平累加，翻转之后换电平。
    while (consumed < toggles_.size() && static_cast<double>(toggles_[consumed]) < end) {
      const double moment = std::max(cursor, static_cast<double>(toggles_[consumed]));
      sum += (high_ ? 1.0 : 0.0) * (moment - cursor);
      cursor = moment;
      high_ = !high_;
      ++consumed;
    }
    sum += (high_ ? 1.0 : 0.0) * (end - cursor);
    samples.push_back(static_cast<float>(sum / ticksPerSample_) * AMPLITUDE);
    position_ = end;
  }
  toggles_.erase(toggles_.begin(), toggles_.begin() + static_cast<std::ptrdiff_t>(consumed));
}

}  // namespace ember::logic::hardware
