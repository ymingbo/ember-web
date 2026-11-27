/*
 * ------------------------------------------------------------------------
 * 名称：preferences-requests.cpp
 * 说明：preferences 模块的请求登记实现：校验参数，调用首选项服务，把登记的内容与取值转成生成的数据类型返回。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "requests/preferences/preferences-requests.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "contracts/preferences/generated/preferences.hpp"
#include "contracts/preferences/preferences-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::requests::preferences {

namespace contract = ::ember::contracts::preferences;

namespace {

nlohmann::json toJson(const contract::SettingValue& value) {
  return std::visit([](const auto& item) { return nlohmann::json(item); }, value);
}

/** 请求里的取值转成设置项取值；不是布尔值、整数或字符串时报取值不合格。 */
contract::SettingValue fromJson(const nlohmann::json& json) {
  if (json.is_boolean()) return contract::SettingValue{json.get<bool>()};
  if (json.is_number_integer()) return contract::SettingValue{json.get<std::int64_t>()};
  if (json.is_string()) return contract::SettingValue{json.get<std::string>()};
  throw shared::CoreError(contract::ERROR_INVALID_VALUE, "取值必须是字符串、布尔值或整数");
}

const char* kindName(contract::SettingKind kind) {
  switch (kind) {
    case contract::SettingKind::Choice:
      return "choice";
    case contract::SettingKind::Switch:
      return "switch";
    case contract::SettingKind::Integer:
      return "integer";
    case contract::SettingKind::Text:
      return "text";
  }
  return "switch";
}

contract::PreferenceSetting toSetting(const contract::SettingEntry& entry) {
  const auto& definition = entry.definition;
  contract::PreferenceSetting setting;
  setting.id = definition.id;
  setting.group = definition.group;
  setting.label = definition.label;
  setting.description = definition.description;
  setting.kind = kindName(definition.kind);
  setting.hidden = definition.hidden;
  setting.defaultValue = toJson(definition.defaultValue);
  setting.value = toJson(entry.value);
  if (definition.kind == contract::SettingKind::Choice) {
    std::vector<contract::PreferenceOption> options;
    for (const auto& option : definition.options) options.push_back({option.value, option.label});
    setting.options = std::move(options);
  } else if (definition.kind == contract::SettingKind::Integer) {
    setting.minimum = definition.minimum;
    setting.maximum = definition.maximum;
  } else if (definition.kind == contract::SettingKind::Text) {
    setting.maxLength = definition.maxLength;
    if (!definition.picker.empty()) setting.picker = definition.picker;
  }
  return setting;
}

}  // namespace

void addPreferencesRequests(platform::RequestRouter& router, std::shared_ptr<contract::PreferenceService> service) {
  router.add("preferences", contract::PREFERENCES_LIST_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parsePreferencesListParams(params, "params"));
    contract::PreferencesListResult result;
    for (const auto& group : service->groups()) result.groups.push_back({group.id, group.label});
    for (const auto& entry : service->entries()) result.settings.push_back(toSetting(entry));
    return nlohmann::json(result);
  });
  router.add("preferences", contract::PREFERENCES_SET_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parsePreferencesSetParams(params, "params");
    const auto value = service->set(parsed.id, fromJson(parsed.value));
    return nlohmann::json(contract::PreferencesSetResult{parsed.id, toJson(value)});
  });
  router.add("preferences", contract::PREFERENCES_RESET_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parsePreferencesResetParams(params, "params");
    const auto value = service->reset(parsed.id);
    return nlohmann::json(contract::PreferencesResetResult{parsed.id, toJson(value)});
  });
}

}  // namespace ember::requests::preferences
