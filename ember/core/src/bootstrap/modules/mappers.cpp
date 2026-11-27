/*
 * ------------------------------------------------------------------------
 * 名称：mappers.cpp
 * 说明：mapper 插件模块的登记：创建插件登记（预装目录、用户插件目录、主机型号目录），登记 mappers.* 请求，并把插件管理与卡带工厂提供给其他模块。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：主机型号目录列出程序里已有的主机型号；目前只有 fc；增加主机型号时在这里登记。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/mappers.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <variant>

#include "bootstrap/machines/common/mapper-circuit-catalog.hpp"

#include "contracts/mappers/mapper-factory.hpp"
#include "contracts/mappers/mapper-service.hpp"
#include "contracts/preferences/preference-service.hpp"
#include "infra/mappers/plugin-registry.hpp"
#include "requests/mappers/mappers-requests.hpp"

namespace ember::bootstrap {

void registerMappers(ModuleRegistry& registry) {
  namespace api = contracts::preferences;
  auto preferences = registry.services.require<api::PreferenceService>();
  preferences->addGroup({contracts::mappers::SETTING_GROUP, "mapper 插件"});
  preferences->addSetting({.id = contracts::mappers::SETTING_DEVELOPMENT_DIRECTORY,
                           .group = contracts::mappers::SETTING_GROUP,
                           .label = "开发中的插件目录",
                           .description = "目录下每个子目录是一个未打包的 mapper 插件（含 manifest.json 与 main.js），优先于同标识的已安装插件；改动插件后重新加载就能生效，不用反复打包。留空时不使用。",
                           .kind = api::SettingKind::Text,
                           .maxLength = 260,
                           .picker = "directory",
                           .defaultValue = std::string()});

  infra::mappers::PluginRegistryOptions options;
  options.bundledDirectory = registry.bundledPluginsPath;
  options.userDirectory = registry.userDataPath / "plugins";
  options.developmentDirectory = [preferences] {
    const auto value = preferences->value(contracts::mappers::SETTING_DEVELOPMENT_DIRECTORY);
    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr || text->empty()) return std::filesystem::path();
    return std::filesystem::path(std::u8string(text->begin(), text->end()));
  };
  options.hosts.hosts = {"fc"};
  options.circuits = std::make_shared<StandardCircuitCatalog>();
  options.errors = registry.errors;
  options.events = registry.events;
  auto plugins = std::make_shared<infra::mappers::PluginRegistry>(std::move(options));
  // 开发目录改了就重新读取。
  preferences->subscribe([plugins](const std::string& id, const api::SettingValue&) {
    if (id == contracts::mappers::SETTING_DEVELOPMENT_DIRECTORY) plugins->reload();
  });
  registry.services.provide<contracts::mappers::MapperService>(plugins);
  registry.services.provide<contracts::mappers::MapperFactory>(plugins);
  requests::mappers::addMapperRequests(registry.requests, plugins);
}

}  // namespace ember::bootstrap
