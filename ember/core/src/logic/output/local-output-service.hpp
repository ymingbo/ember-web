/*
 * ------------------------------------------------------------------------
 * 名称：local-output-service.hpp
 * 说明：输出设置服务的实现：音量与静音保存在内存里，变化时应用到声音输出；有首选项服务时，启动时从首选项读音量、“启动时静音”与缓冲大小，音量变化时保存回去。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：没有首选项服务时初始音量 100、未静音；构造时就把初始值应用到声音输出。静音状态不保存，只由“启动时静音”决定。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "contracts/output/output-service.hpp"
#include "contracts/preferences/preference-service.hpp"
#include "platform/audio/audio-output.hpp"

namespace ember::logic::output {

/** 本机的输出设置服务。 */
class LocalOutputService final : public contracts::output::OutputService {
 public:
  /** 初始音量（百分比）。 */
  static constexpr std::int64_t DEFAULT_VOLUME = 100;

  /**
   * @param audio 声音输出，设置变化时应用到它。
   * @param preferences 首选项服务，已登记声音设置项；为空时不读不存。
   */
  explicit LocalOutputService(std::shared_ptr<platform::AudioOutput> audio, std::shared_ptr<contracts::preferences::PreferenceService> preferences = nullptr);

  contracts::output::OutputSetVolumeResult setVolume(std::optional<std::int64_t> volume, std::optional<bool> muted) override;
  [[nodiscard]] contracts::output::OutputSetVolumeResult volume() const override;
  std::string setAudioMode(const std::string& mode) override;

  /** 首选项的取值变了时调用（由登记函数订阅）：缓冲大小立即应用，音量与当前不同时也应用；其他设置项忽略。 */
  void applyPreference(const std::string& id, const contracts::preferences::SettingValue& value);

  /** 缓冲大小设置项的取值对应的档位；不认识的取值按标准处理。 */
  static platform::AudioBufferProfile profileFor(const std::string& value);

 private:
  std::shared_ptr<platform::AudioOutput> audio_;
  std::shared_ptr<contracts::preferences::PreferenceService> preferences_;
  mutable std::mutex mutex_;
  std::int64_t volume_{DEFAULT_VOLUME};
  bool muted_{false};
  bool fast_{false};
};

}  // namespace ember::logic::output
