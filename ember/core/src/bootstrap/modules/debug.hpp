/*
 * ------------------------------------------------------------------------
 * 名称：debug.hpp
 * 说明：调试服务模块的登记函数声明，以及“调试服务”分组的设置项登记。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"
#include "contracts/preferences/preference-service.hpp"

namespace ember::bootstrap {

/** 设置项标识。 */
inline constexpr const char* SETTING_DEBUG_ENABLED = "debug.enabled";
inline constexpr const char* SETTING_DEBUG_PORT = "debug.port";
inline constexpr const char* SETTING_DEBUG_TOKEN = "debug.token";
/** 出厂默认端口。 */
inline constexpr int DEFAULT_DEBUG_PORT = 5688;

/** 登记“调试服务”分组与设置项：启用、端口、令牌。 */
void registerDebugSettings(contracts::preferences::PreferenceService& service);

/**
 * 登记调试服务模块：创建宿主，按首选项（或启动参数）启动调试服务，登记 debug.* 请求，把核心的事件转给订阅的连接；核心停止时关闭服务。
 * 令牌为空时自动生成并保存；取用 PreferenceService，须排在 preferences 之后，并排在其他业务模块之后（核心停止时最先停下）。
 */
void registerDebug(ModuleRegistry& registry);

}  // namespace ember::bootstrap
