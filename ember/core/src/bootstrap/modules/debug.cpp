/*
 * ------------------------------------------------------------------------
 * 名称：debug.cpp
 * 说明：调试服务模块的登记：登记设置项，创建宿主并按配置启动，登记 debug.* 请求，旁听核心事件，核心停止时关闭服务。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：配置来自首选项（debug.enabled、debug.port、debug.token），启动参数 --debug-port 指定时强制开启并覆盖端口，不写入首选项；
 *       端点文件写在用户数据目录的 run/debug-endpoint.json，里面有地址与令牌，供命令行与 Copilot 的工具自动连接。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/debug.hpp"

#include <memory>
#include <string>
#include <variant>

#include "contracts/debug/generated/openrpc.hpp"
#include "contracts/output/screenshot-service.hpp"
#include "infra/debug/debug-host.hpp"
#include "infra/debug/debug-token.hpp"
#include "requests/debug/debug-requests.hpp"

namespace ember::bootstrap {

namespace api = contracts::preferences;

namespace {

std::string textOf(const api::SettingValue& value) {
  const auto* text = std::get_if<std::string>(&value);
  return text == nullptr ? std::string() : *text;
}

bool boolOf(const api::SettingValue& value) {
  const auto* flag = std::get_if<bool>(&value);
  return flag != nullptr && *flag;
}

std::int64_t integerOf(const api::SettingValue& value) {
  const auto* number = std::get_if<std::int64_t>(&value);
  return number == nullptr ? DEFAULT_DEBUG_PORT : *number;
}

}  // namespace

void registerDebugSettings(api::PreferenceService& service) {
  service.addGroup({"debug", "调试服务"});
  service.addSetting({.id = SETTING_DEBUG_ENABLED,
                      .group = "debug",
                      .label = "启用调试服务",
                      .description = "开启后只监听本机（127.0.0.1），其他程序凭令牌连接，可读写模拟器的内存与状态；只在需要时开启。",
                      .kind = api::SettingKind::Switch,
                      .defaultValue = false});
  service.addSetting({.id = SETTING_DEBUG_PORT,
                      .group = "debug",
                      .label = "端口",
                      .description = "修改后立即重新监听。",
                      .kind = api::SettingKind::Integer,
                      .minimum = 1024,
                      .maximum = 65535,
                      .defaultValue = std::int64_t{DEFAULT_DEBUG_PORT}});
  service.addSetting({.id = SETTING_DEBUG_TOKEN,
                      .group = "debug",
                      .label = "令牌",
                      .description = "连接时必须提供；点“更换”重新生成，也可以直接改写，清空同样会重新生成。更换后已连接的工具会被断开。",
                      .kind = api::SettingKind::Text,
                      .maxLength = 128,
                      .picker = "regenerate",
                      .defaultValue = std::string()});
  service.addSetting({.id = contracts::output::SETTING_DEBUG_OUTPUT_DIRECTORY,
                      .group = "debug",
                      .label = "输出目录",
                      .description = "调试工具保存截图等文件的位置；留空使用数据目录下的 debug-output。",
                      .kind = api::SettingKind::Text,
                      .maxLength = 260,
                      .picker = "directory",
                      .defaultValue = std::string()});
}

void registerDebug(ModuleRegistry& registry) {
  auto preferences = registry.services.require<api::PreferenceService>();
  registerDebugSettings(*preferences);
  // 令牌始终有值：第一次运行、或被清空时生成并保存。
  if (textOf(preferences->value(SETTING_DEBUG_TOKEN)).empty()) preferences->set(SETTING_DEBUG_TOKEN, api::SettingValue{infra::debug::generateToken()});

  auto discovery = std::make_shared<const nlohmann::json>(nlohmann::json::parse(contracts::debug::openRpcDocument(), nullptr, false));
  if (discovery->is_discarded()) discovery = nullptr;

  platform::RequestRouter* router = &registry.requests;
  infra::debug::DebugHostOptions options;
  options.endpointFile = registry.userDataPath / "run" / "debug-endpoint.json";
  options.coreVersion = registry.coreVersion;
  options.appVersion = registry.appVersion;
  auto host = std::make_shared<infra::debug::DebugHost>(
      std::move(options), [router](std::string_view name, const nlohmann::json& params, const platform::CancellationToken& token) { return router->dispatch(name, params, token); }, discovery, registry.events,
      registry.errors);
  registry.services.provide<contracts::debug::DebugService>(host);
  requests::debug::addDebugRequests(registry.requests, host);
  registry.eventSource->subscribe([host](const std::string& name, const nlohmann::json& data) { host->publishEvent(name, data); });

  const auto forcedPort = registry.debugPort;
  const auto forcedToken = registry.debugToken;
  const auto applyConfig = [preferences, host, forcedPort, forcedToken] {
    infra::debug::DebugConfig config;
    config.enabled = forcedPort.has_value() || boolOf(preferences->value(SETTING_DEBUG_ENABLED));
    config.port = forcedPort.value_or(static_cast<std::uint16_t>(integerOf(preferences->value(SETTING_DEBUG_PORT))));
    config.token = forcedToken.empty() ? textOf(preferences->value(SETTING_DEBUG_TOKEN)) : forcedToken;
    if (config.token.empty()) {
      // 令牌被清空或恢复默认：重新生成并保存，保存会再通知一次，那一次会应用新令牌。
      preferences->set(SETTING_DEBUG_TOKEN, api::SettingValue{infra::debug::generateToken()});
      return;
    }
    host->apply(config);
  };
  preferences->subscribe([applyConfig](const std::string& id, const api::SettingValue&) {
    if (id.starts_with("debug.") && id != contracts::output::SETTING_DEBUG_OUTPUT_DIRECTORY) applyConfig();
  });
  applyConfig();
  registry.stopHandlers.push_back([host] { host->shutdown(); });
}

}  // namespace ember::bootstrap
