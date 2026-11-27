/*
 * ------------------------------------------------------------------------
 * 名称：preference-registry.cpp
 * 说明：首选项服务的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/preferences/preference-registry.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "contracts/preferences/generated/preferences.hpp"
#include "contracts/preferences/preferences-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::preferences {

namespace {

namespace api = contracts::preferences;
namespace fs = std::filesystem;

/** 文件格式的版本。 */
constexpr int FILE_VERSION = 1;

nlohmann::json toJson(const api::SettingValue& value) {
  return std::visit([](const auto& item) { return nlohmann::json(item); }, value);
}

/** JSON 值转成设置项取值；不是布尔值、整数或字符串时为空。 */
std::optional<api::SettingValue> fromJson(const nlohmann::json& json) {
  if (json.is_boolean()) return api::SettingValue{json.get<bool>()};
  if (json.is_number_integer()) return api::SettingValue{json.get<std::int64_t>()};
  if (json.is_string()) return api::SettingValue{json.get<std::string>()};
  return std::nullopt;
}

std::string join(const std::vector<api::OptionDefinition>& options) {
  std::string text;
  for (const auto& option : options) text += (text.empty() ? "" : "、") + option.value;
  return text;
}

/** 取值是否符合登记的种类、可选值与范围；不符合时 reason 里写原因。 */
bool isValid(const api::SettingDefinition& definition, const api::SettingValue& value, std::string& reason) {
  switch (definition.kind) {
    case api::SettingKind::Choice: {
      const auto* text = std::get_if<std::string>(&value);
      if (text == nullptr || std::none_of(definition.options.begin(), definition.options.end(), [&](const api::OptionDefinition& option) { return option.value == *text; })) {
        reason = "取值必须是下面之一：" + join(definition.options);
        return false;
      }
      return true;
    }
    case api::SettingKind::Switch:
      if (!std::holds_alternative<bool>(value)) {
        reason = "取值必须是布尔值";
        return false;
      }
      return true;
    case api::SettingKind::Integer: {
      const auto* number = std::get_if<std::int64_t>(&value);
      if (number == nullptr || *number < definition.minimum || *number > definition.maximum) {
        reason = "取值必须是 " + std::to_string(definition.minimum) + " 到 " + std::to_string(definition.maximum) + " 之间的整数";
        return false;
      }
      return true;
    }
    case api::SettingKind::Text: {
      const auto* text = std::get_if<std::string>(&value);
      if (text == nullptr || static_cast<std::int64_t>(text->size()) > definition.maxLength) {
        reason = "取值必须是不超过 " + std::to_string(definition.maxLength) + " 个字节的文字";
        return false;
      }
      return true;
    }
  }
  reason = "不认识的设置项种类";
  return false;
}

/** 登记时检查设置项本身是否合格；不合格是程序错误。 */
void checkDefinition(const api::SettingDefinition& setting, const std::vector<api::GroupDefinition>& groups, const std::vector<api::SettingEntry>& items) {
  const std::string where = "设置项 " + setting.id;
  if (setting.label.empty()) throw std::logic_error(where + " 没有显示名称");
  if (!setting.id.starts_with(setting.group + ".") || setting.id.size() == setting.group.size() + 1) throw std::logic_error(where + " 的标识必须是“分组标识.名称”");
  if (std::none_of(groups.begin(), groups.end(), [&](const api::GroupDefinition& group) { return group.id == setting.group; })) throw std::logic_error(where + " 的分组没有登记：" + setting.group);
  if (std::any_of(items.begin(), items.end(), [&](const api::SettingEntry& item) { return item.definition.id == setting.id; })) throw std::logic_error(where + " 重复登记");
  if (setting.kind == api::SettingKind::Choice) {
    if (setting.options.empty()) throw std::logic_error(where + " 没有可选值");
    for (std::size_t index = 0; index < setting.options.size(); ++index) {
      for (std::size_t other = 0; other < index; ++other) {
        if (setting.options[other].value == setting.options[index].value) throw std::logic_error(where + " 的可选值重复：" + setting.options[index].value);
      }
    }
  }
  if (setting.kind == api::SettingKind::Integer && setting.minimum > setting.maximum) throw std::logic_error(where + " 的范围不对");
  std::string reason;
  if (!isValid(setting, setting.defaultValue, reason)) throw std::logic_error(where + " 的默认值不合格：" + reason);
}

}  // namespace

PreferenceRegistry::PreferenceRegistry(PreferenceRegistryOptions options) : options_(std::move(options)) { load(); }

void PreferenceRegistry::addGroup(api::GroupDefinition group) {
  const std::lock_guard lock(mutex_);
  if (group.id.empty() || group.label.empty()) throw std::logic_error("分组的标识与显示名称都不能为空");
  if (std::any_of(groups_.begin(), groups_.end(), [&](const api::GroupDefinition& item) { return item.id == group.id; })) throw std::logic_error("分组重复登记：" + group.id);
  groups_.push_back(std::move(group));
}

void PreferenceRegistry::addSetting(api::SettingDefinition setting) {
  const std::lock_guard lock(mutex_);
  checkDefinition(setting, groups_, items_);
  api::SettingValue value = setting.defaultValue;
  const auto stored = stored_.find(setting.id);
  if (stored != stored_.end()) {
    const auto candidate = fromJson(*stored);
    std::string reason;
    if (candidate.has_value() && isValid(setting, *candidate, reason)) value = *candidate;
    else logError("preferences.valueInvalid", "首选项里保存的取值不合格，已改用默认值：" + setting.id, reason);
  }
  items_.push_back(api::SettingEntry{std::move(setting), std::move(value)});
}

void PreferenceRegistry::subscribe(Listener listener) {
  const std::lock_guard lock(mutex_);
  listeners_.push_back(std::move(listener));
}

api::SettingValue PreferenceRegistry::value(const std::string& id) const {
  const std::lock_guard lock(mutex_);
  const auto found = std::find_if(items_.begin(), items_.end(), [&](const api::SettingEntry& item) { return item.definition.id == id; });
  if (found == items_.end()) throw shared::CoreError(api::ERROR_UNKNOWN, "没有这个设置项：" + id);
  return found->value;
}

std::vector<api::GroupDefinition> PreferenceRegistry::groups() const {
  const std::lock_guard lock(mutex_);
  return groups_;
}

std::vector<api::SettingEntry> PreferenceRegistry::entries() const {
  const std::lock_guard lock(mutex_);
  return items_;
}

bool PreferenceRegistry::assign(const std::string& id, const api::SettingValue& value, api::SettingValue& result) {
  const auto found = std::find_if(items_.begin(), items_.end(), [&](const api::SettingEntry& item) { return item.definition.id == id; });
  if (found == items_.end()) throw shared::CoreError(api::ERROR_UNKNOWN, "没有这个设置项：" + id);
  result = found->value;
  if (found->value == value) return false;
  found->value = value;
  result = value;
  save();
  return true;
}

api::SettingValue PreferenceRegistry::set(const std::string& id, const api::SettingValue& value) {
  api::SettingValue result;
  std::vector<Listener> listeners;
  {
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(items_.begin(), items_.end(), [&](const api::SettingEntry& item) { return item.definition.id == id; });
    if (found == items_.end()) throw shared::CoreError(api::ERROR_UNKNOWN, "没有这个设置项：" + id);
    std::string reason;
    if (!isValid(found->definition, value, reason)) throw shared::CoreError(api::ERROR_INVALID_VALUE, found->definition.label + "：" + reason);
    if (!assign(id, value, result)) return result;
    listeners = listeners_;
  }
  announce(id, result, listeners);
  return this->value(id);
}

api::SettingValue PreferenceRegistry::reset(const std::string& id) {
  api::SettingValue result;
  std::vector<Listener> listeners;
  {
    const std::lock_guard lock(mutex_);
    const auto found = std::find_if(items_.begin(), items_.end(), [&](const api::SettingEntry& item) { return item.definition.id == id; });
    if (found == items_.end()) throw shared::CoreError(api::ERROR_UNKNOWN, "没有这个设置项：" + id);
    if (!assign(id, found->definition.defaultValue, result)) return result;
    listeners = listeners_;
  }
  announce(id, result, listeners);
  return this->value(id);
}

void PreferenceRegistry::announce(const std::string& id, const api::SettingValue& value, const std::vector<Listener>& listeners) const {
  if (options_.events != nullptr) {
    options_.events->publish(api::PREFERENCES_CHANGED_EVENT, nlohmann::json(api::PreferencesChangedData{id, toJson(value)}));
  }
  for (const Listener& listener : listeners) listener(id, value);
}

void PreferenceRegistry::load() {
  if (options_.file.empty()) return;
  std::error_code error;
  if (!fs::exists(options_.file, error)) return;
  std::ifstream stream(options_.file, std::ios::binary);
  if (!stream) {
    logError("preferences.fileUnreadable", "无法读取首选项文件，已使用默认设置", error.message());
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const auto document = nlohmann::json::parse(text, nullptr, false);
  if (!document.is_object() || document.value("version", 0) != FILE_VERSION || !document.contains("values") || !document["values"].is_object()) {
    logError("preferences.fileInvalid", "首选项文件的格式不对，已使用默认设置", "");
    return;
  }
  stored_ = document["values"];
}

void PreferenceRegistry::save() {
  if (options_.file.empty()) return;
  nlohmann::json values = nlohmann::json::object();
  for (const auto& item : items_) values[item.definition.id] = toJson(item.value);
  const nlohmann::json document = {{"version", FILE_VERSION}, {"values", values}};
  try {
    std::error_code error;
    fs::create_directories(options_.file.parent_path(), error);
    if (error) throw std::runtime_error("无法创建目录：" + error.message());
    fs::path temporary = options_.file;
    temporary += ".tmp";
    {
      std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
      const std::string text = document.dump(2);
      stream.write(text.data(), static_cast<std::streamsize>(text.size()));
      stream.flush();
      if (!stream) throw std::runtime_error("无法写入临时文件");
    }
    fs::rename(temporary, options_.file, error);
    if (error) {
      fs::remove(temporary, error);
      throw std::runtime_error("无法替换首选项文件");
    }
  } catch (const std::exception& exception) {
    logError("preferences.saveFailed", "无法保存首选项，设置只在本次运行里有效", exception.what());
  }
}

void PreferenceRegistry::logError(const std::string& code, const std::string& message, const std::string& reason) const {
  if (options_.errors == nullptr) return;
  options_.errors->write({"preferences", code, message, {{"reason", reason}}});
}

}  // namespace ember::infra::preferences
