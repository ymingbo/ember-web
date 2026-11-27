/*
 * ------------------------------------------------------------------------
 * 名称：preferences.hpp
 * 说明：首选项模块的登记函数声明，以及界面类设置项（外观、画面）的登记。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"
#include "contracts/preferences/preference-service.hpp"

namespace ember::bootstrap {

/** 登记界面类的分组与设置项：外观（主题）与画面（比例、缩放方式、显示全部 240 行）；这些设置只由界面解释，核心只保存。 */
void registerInterfaceSettings(contracts::preferences::PreferenceService& service);

/** 登记首选项模块：创建首选项服务（取值保存在用户数据目录的 config/preferences.json），登记 preferences.* 请求，并把服务提供给其他模块登记自己的设置项。 */
void registerPreferences(ModuleRegistry& registry);

}  // namespace ember::bootstrap
