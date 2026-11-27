/*
 * ------------------------------------------------------------------------
 * 名称：output-settings.hpp
 * 说明：output 契约：声音设置项在首选项里的标识。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：设置项由 output 模块登记（bootstrap/modules/output.cpp），取值保存在首选项里，见 docs/05-界面.md 第 4.5 节。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::output {

/** 分组标识：声音。 */
inline constexpr std::string_view SETTING_GROUP = "sound";
/** 缓冲大小：low（低延迟）、standard（标准）、stable（稳定）。 */
inline constexpr std::string_view SETTING_BUFFER = "sound.buffer";
/** 启动时是否静音。 */
inline constexpr std::string_view SETTING_START_MUTED = "sound.startMuted";
/** 音量百分比，0 到 100；不在首选项页显示，底部面板的音量滑块修改它并保存。 */
inline constexpr std::string_view SETTING_VOLUME = "sound.volume";

}  // namespace ember::contracts::output
