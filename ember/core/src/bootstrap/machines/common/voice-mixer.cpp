/*
 * ------------------------------------------------------------------------
 * 名称：voice-mixer.cpp
 * 说明：语音芯片输出电平混音器的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/common/voice-mixer.hpp"

#include <algorithm>
#include <cstddef>

namespace ember::bootstrap {

namespace hw = contracts::hardware;

void VoiceMixer::reset() {
  changes_.clear();
  level_ = 0;
  mixedUntil_ = 0;
}

void VoiceMixer::note(hw::MasterTime when, double level) { changes_.emplace_back(when, level); }

void VoiceMixer::mixInto(std::vector<float>& samples, hw::MasterTime now) {
  if (samples.empty()) return;
  const double from = static_cast<double>(mixedUntil_);
  const double span = std::max(0.0, static_cast<double>(now) - from);
  std::size_t consumed = 0;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const double moment = from + span * static_cast<double>(index + 1) / static_cast<double>(samples.size());
    while (consumed < changes_.size() && static_cast<double>(changes_[consumed].first) <= moment) level_ = changes_[consumed++].second;
    samples[index] = std::clamp(samples[index] + static_cast<float>(level_), -1.0F, 1.0F);
  }
  changes_.erase(changes_.begin(), changes_.begin() + static_cast<std::ptrdiff_t>(consumed));
  mixedUntil_ = now;
}

}  // namespace ember::bootstrap
