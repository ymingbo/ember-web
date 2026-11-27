/*
 * ------------------------------------------------------------------------
 * 名称：runtime-settings.hpp
 * 说明：运行的首选项：设置项标识。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-20
 * 备注：设置项由 preferences 模块与其他界面设置一起登记，运行模块读取它并交给运行服务。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::runtime::setting {

/** 开机与复位后是否把最初的几帧显示为黑色（开关，默认关闭）；只影响交给界面显示的帧。 */
inline constexpr std::string_view SETTING_HIDE_STARTUP_FRAMES = "display.hideStartupFrames";

}  // namespace ember::contracts::runtime::setting
