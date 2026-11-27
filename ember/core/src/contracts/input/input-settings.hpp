/*
 * ------------------------------------------------------------------------
 * 名称：input-settings.hpp
 * 说明：输入的首选项：设置项标识与所属分组。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：设置项由 input 模块登记；点击画面是否捕获鼠标只给界面用，核心不读它。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::input::setting {

/** 首选项分组：输入。 */
inline constexpr std::string_view SETTING_GROUP = "input";

/** 点击画面时是否捕获鼠标（开关，默认关闭）。 */
inline constexpr std::string_view SETTING_CAPTURE_MOUSE = "input.captureMouse";

}  // namespace ember::contracts::input::setting
