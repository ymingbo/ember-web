/*
 * ------------------------------------------------------------------------
 * 名称：preferences-error.hpp
 * 说明：preferences 模块的错误码：设置项不存在、取值不合格时经 shared::CoreError 抛出，说明面向用户。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：错误码以模块名 preferences 开头，取值与 schema/preferences.schema.json 登记的一致。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::preferences {

/** 没有这个设置项。 */
inline constexpr std::string_view ERROR_UNKNOWN = "preferences.unknown";
/** 取值不符合设置项登记的种类、可选值或范围。 */
inline constexpr std::string_view ERROR_INVALID_VALUE = "preferences.invalidValue";

}  // namespace ember::contracts::preferences
