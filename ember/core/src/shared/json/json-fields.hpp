/*
 * ------------------------------------------------------------------------
 * 名称：json-fields.hpp
 * 说明：按 Schema 读取 JSON 字段的辅助函数：对象、未知字段、字符串（长度、枚举）、整数与数字（范围）、布尔、数组（项数），
 *       不合格时抛出带位置的 SchemaError。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：供 Schema 生成的代码使用；字符串长度按 Unicode 字符计；带小数点但值为整数的数字也算整数。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "shared/errors/core-error.hpp"

namespace ember::shared::json_fields {

/** 要求是对象。 */
inline void requireObject(const nlohmann::json& json, const std::string& path) {
  if (!json.is_object()) throw SchemaError(path + " 应为对象");
}

/** 对象里不能有 Schema 以外的字段。 */
inline void rejectUnknown(const nlohmann::json& json, const std::string& path, std::initializer_list<std::string_view> known) {
  for (const auto& [key, value] : json.items()) {
    static_cast<void>(value);
    bool found = false;
    for (const auto& name : known) found = found || name == key;
    if (!found) throw SchemaError(path + "." + key + " 是不认识的字段");
  }
}

/** 取对象里的字段，没有时返回 nullptr。 */
inline const nlohmann::json* find(const nlohmann::json& json, std::string_view key) {
  const auto it = json.find(key);
  return it == json.end() ? nullptr : &*it;
}

/** UTF-8 字符串的字符数。 */
inline std::size_t characterCount(const std::string& text) {
  std::size_t count = 0;
  for (const unsigned char byte : text) count += (byte & 0xC0U) != 0x80U ? 1 : 0;
  return count;
}

/** 读取字符串。 */
inline std::string readString(const nlohmann::json& json, const std::string& path, std::optional<std::size_t> minLength, std::optional<std::size_t> maxLength,
                              const std::vector<std::string>& allowed) {
  if (!json.is_string()) throw SchemaError(path + " 应为字符串");
  auto value = json.get<std::string>();
  const auto length = characterCount(value);
  if (minLength && length < *minLength) throw SchemaError(path + " 至少 " + std::to_string(*minLength) + " 个字符");
  if (maxLength && length > *maxLength) throw SchemaError(path + " 最多 " + std::to_string(*maxLength) + " 个字符");
  if (!allowed.empty()) {
    bool found = false;
    for (const auto& item : allowed) found = found || item == value;
    if (!found) throw SchemaError(path + " 的取值不在允许的范围内");
  }
  return value;
}

/** 读取整数。 */
inline std::int64_t readInteger(const nlohmann::json& json, const std::string& path, std::optional<std::int64_t> minimum, std::optional<std::int64_t> maximum) {
  std::int64_t value = 0;
  if (json.is_number_integer()) {
    value = json.get<std::int64_t>();
  } else if (json.is_number_float() && std::isfinite(json.get<double>()) && std::trunc(json.get<double>()) == json.get<double>()) {
    value = static_cast<std::int64_t>(json.get<double>());
  } else {
    throw SchemaError(path + " 应为整数");
  }
  if (minimum && value < *minimum) throw SchemaError(path + " 不能小于 " + std::to_string(*minimum));
  if (maximum && value > *maximum) throw SchemaError(path + " 不能大于 " + std::to_string(*maximum));
  return value;
}

/** 读取数字。 */
inline double readNumber(const nlohmann::json& json, const std::string& path, std::optional<double> minimum, std::optional<double> maximum) {
  if (!json.is_number() || !std::isfinite(json.get<double>())) throw SchemaError(path + " 应为数字");
  const auto value = json.get<double>();
  if (minimum && value < *minimum) throw SchemaError(path + " 不能小于 " + std::to_string(*minimum));
  if (maximum && value > *maximum) throw SchemaError(path + " 不能大于 " + std::to_string(*maximum));
  return value;
}

/** 读取布尔值。 */
inline bool readBoolean(const nlohmann::json& json, const std::string& path) {
  if (!json.is_boolean()) throw SchemaError(path + " 应为布尔值");
  return json.get<bool>();
}

/** 读取数组，每一项由 readItem 读取。 */
template <typename T, typename ReadItem>
std::vector<T> readArray(const nlohmann::json& json, const std::string& path, std::optional<std::size_t> minItems, std::optional<std::size_t> maxItems,
                         ReadItem readItem) {
  if (!json.is_array()) throw SchemaError(path + " 应为数组");
  if (minItems && json.size() < *minItems) throw SchemaError(path + " 至少 " + std::to_string(*minItems) + " 项");
  if (maxItems && json.size() > *maxItems) throw SchemaError(path + " 最多 " + std::to_string(*maxItems) + " 项");
  std::vector<T> values;
  values.reserve(json.size());
  for (std::size_t index = 0; index < json.size(); ++index) values.push_back(readItem(json[index], path + "[" + std::to_string(index) + "]"));
  return values;
}

}  // namespace ember::shared::json_fields
