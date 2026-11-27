/*
 * ------------------------------------------------------------------------
 * 名称：system.hpp
 * 说明：系统模块的登记函数声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 登记系统模块。 */
void registerSystem(ModuleRegistry& registry);

}  // namespace ember::bootstrap
