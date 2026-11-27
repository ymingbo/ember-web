/*
 * ------------------------------------------------------------------------
 * 名称：cartridges.hpp
 * 说明：卡带模块的登记函数声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 登记卡带模块。 */
void registerCartridges(ModuleRegistry& registry);

}  // namespace ember::bootstrap
