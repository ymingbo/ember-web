/*
 * ------------------------------------------------------------------------
 * 名称：output.cpp
 * 说明：输出模块的登记：打开系统声音输出（失败时降级为空输出并记录错误日志），创建输出设置服务并登记 output.* 请求，把声音输出提供给运行模块。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：运行模块取用声音输出，须排在本模块之后登记；核心停止时系统设备随声音输出一起释放。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/output.hpp"

#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>

#include "bootstrap/machines/machine-catalog.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "contracts/output/debug-file-writer.hpp"
#include "infra/output/debug-text-file-writer.hpp"
#include "infra/output/png-screenshot-service.hpp"
#include "contracts/output/output-service.hpp"
#include "contracts/output/output-settings.hpp"
#include "contracts/preferences/preference-service.hpp"
#include "logic/output/local-output-service.hpp"
#include "platform/audio/miniaudio-output.hpp"
#include "platform/audio/null-audio-output.hpp"
#include "requests/output/output-requests.hpp"

namespace ember::bootstrap {

std::shared_ptr<platform::AudioOutput> openAudioOutputOrFallback(const AudioOpener& opener, unsigned sampleRate, platform::ErrorLog& errors) {
  std::string failure;
  std::shared_ptr<platform::AudioOutput> device;
  try {
    device = opener(sampleRate, failure);
  } catch (const std::exception& error) {
    failure = error.what();
  }
  if (device != nullptr) return device;
  errors.write({"output", "output.deviceUnavailable", "无法打开系统声音设备，已降级为无声输出", {{"reason", failure}}});
  return std::make_shared<platform::NullAudioOutput>(sampleRate);
}

void registerSoundSettings(contracts::preferences::PreferenceService& service) {
  namespace setting = contracts::output;
  namespace api = contracts::preferences;
  service.addGroup({std::string(setting::SETTING_GROUP), "声音"});
  service.addSetting({.id = std::string(setting::SETTING_BUFFER),
                      .group = std::string(setting::SETTING_GROUP),
                      .label = "缓冲大小",
                      .description = "缓冲越小延迟越低，电脑负载高时容易爆音。",
                      .kind = api::SettingKind::Choice,
                      .options = {{"low", "低延迟"}, {"standard", "标准"}, {"stable", "稳定"}},
                      .defaultValue = std::string("standard")});
  service.addSetting({.id = std::string(setting::SETTING_START_MUTED),
                      .group = std::string(setting::SETTING_GROUP),
                      .label = "启动时静音",
                      .description = "音量在底部面板的“运行”页签里调整。",
                      .kind = api::SettingKind::Switch,
                      .defaultValue = false});
  service.addSetting({.id = std::string(setting::SETTING_VOLUME),
                      .group = std::string(setting::SETTING_GROUP),
                      .label = "音量",
                      .description = "底部面板的音量滑块调整后保存，重启后保持。",
                      .kind = api::SettingKind::Integer,
                      .minimum = 0,
                      .maximum = 100,
                      .hidden = true,
                      .defaultValue = std::int64_t{100}});
}

void registerOutput(ModuleRegistry& registry) {
  auto preferences = registry.services.require<contracts::preferences::PreferenceService>();
  registerSoundSettings(*preferences);
  const AudioOpener opener = [](unsigned sampleRate, std::string& failure) -> std::shared_ptr<platform::AudioOutput> { return platform::MiniaudioOutput::open(sampleRate, failure); };
  auto audio = openAudioOutputOrFallback(opener, MachineCatalog::AUDIO_SAMPLE_RATE, *registry.errors);
  auto service = std::make_shared<logic::output::LocalOutputService>(audio, preferences);
  preferences->subscribe([service](const std::string& id, const contracts::preferences::SettingValue& value) { service->applyPreference(id, value); });
  registry.services.provide<platform::AudioOutput>(audio);
  registry.services.provide<contracts::output::OutputService>(service);
  // 截图要用运行服务（排在本模块之后登记），在全部模块登记完之后再绑定。
  const auto userData = registry.userDataPath;
  const auto outputDirectory = [preferences, userData] {
    const auto value = preferences->value(contracts::output::SETTING_DEBUG_OUTPUT_DIRECTORY);
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr || text->empty()) return userData / "debug-output";
    return std::filesystem::path(std::u8string(text->begin(), text->end()));
  };
  auto screenshots = std::make_shared<infra::output::PngScreenshotService>(outputDirectory);
  registry.services.provide<contracts::output::DebugFileWriter>(std::make_shared<infra::output::DebugTextFileWriter>(outputDirectory));
  registry.startHandlers.push_back([screenshots, &registry] { screenshots->bindRuntime(registry.services.require<contracts::runtime::RuntimeService>()); });
  requests::output::addOutputRequests(registry.requests, service, screenshots);
}

}  // namespace ember::bootstrap
