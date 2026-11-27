/*
 * ------------------------------------------------------------------------
 * 名称：local-output-service.cpp
 * 说明：输出设置服务的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/output/local-output-service.hpp"

#include <algorithm>
#include <variant>

#include "contracts/output/output-settings.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::output {

namespace {

constexpr std::int64_t MAX_VOLUME = 100;

namespace pref = contracts::preferences;
namespace setting = contracts::output;

/** 设置项的取值是字符串时取出来，否则为空串。 */
std::string textOf(const pref::SettingValue& value) {
  const auto* text = std::get_if<std::string>(&value);
  return text == nullptr ? std::string() : *text;
}

}  // namespace

platform::AudioBufferProfile LocalOutputService::profileFor(const std::string& value) {
  if (value == "low") return platform::AudioBufferProfile::LowLatency;
  if (value == "stable") return platform::AudioBufferProfile::Stable;
  return platform::AudioBufferProfile::Standard;
}

LocalOutputService::LocalOutputService(std::shared_ptr<platform::AudioOutput> audio, std::shared_ptr<pref::PreferenceService> preferences)
    : audio_(std::move(audio)), preferences_(std::move(preferences)) {
  if (preferences_ != nullptr) {
    const auto savedVolume = preferences_->value(std::string(setting::SETTING_VOLUME));
    const auto startMuted = preferences_->value(std::string(setting::SETTING_START_MUTED));
    if (const auto* volume = std::get_if<std::int64_t>(&savedVolume)) volume_ = std::clamp<std::int64_t>(*volume, 0, MAX_VOLUME);
    if (const auto* muted = std::get_if<bool>(&startMuted)) muted_ = *muted;
    if (audio_ != nullptr) audio_->setBufferProfile(profileFor(textOf(preferences_->value(std::string(setting::SETTING_BUFFER)))));
  }
  if (audio_ != nullptr) audio_->setVolume(static_cast<unsigned>(volume_), muted_);
}

contracts::output::OutputSetVolumeResult LocalOutputService::setVolume(std::optional<std::int64_t> volume, std::optional<bool> muted) {
  if (volume.has_value() && (*volume < 0 || *volume > MAX_VOLUME)) throw shared::SchemaError("音量必须在 0 到 100 之间");
  contracts::output::OutputSetVolumeResult result;
  bool volumeChanged = false;
  {
    std::lock_guard lock(mutex_);
    if (volume.has_value()) {
      volumeChanged = volume_ != *volume;
      volume_ = *volume;
    }
    if (muted.has_value()) muted_ = *muted;
    if (audio_ != nullptr) audio_->setVolume(static_cast<unsigned>(volume_), muted_);
    result = contracts::output::OutputSetVolumeResult{volume_, muted_};
  }
  // 音量保存回首选项（静音不保存）；在锁外调用，首选项通知回来时音量已经相同，不会再应用一次。
  if (volumeChanged && preferences_ != nullptr) preferences_->set(std::string(setting::SETTING_VOLUME), pref::SettingValue{result.volume});
  return result;
}

contracts::output::OutputSetVolumeResult LocalOutputService::volume() const {
  std::lock_guard lock(mutex_);
  return contracts::output::OutputSetVolumeResult{volume_, muted_};
}

std::string LocalOutputService::setAudioMode(const std::string& mode) {
  std::lock_guard lock(mutex_);
  fast_ = mode == "fast";
  if (audio_ != nullptr) audio_->setFast(fast_);
  return fast_ ? "fast" : "real";
}

void LocalOutputService::applyPreference(const std::string& id, const pref::SettingValue& value) {
  if (id == setting::SETTING_BUFFER) {
    if (audio_ != nullptr) audio_->setBufferProfile(profileFor(textOf(value)));
  } else if (id == setting::SETTING_VOLUME) {
    const auto* volume = std::get_if<std::int64_t>(&value);
    if (volume == nullptr) return;
    std::lock_guard lock(mutex_);
    if (volume_ == *volume) return;
    volume_ = *volume;
    if (audio_ != nullptr) audio_->setVolume(static_cast<unsigned>(volume_), muted_);
  }
}

}  // namespace ember::logic::output
