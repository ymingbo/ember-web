/*
 * ------------------------------------------------------------------------
 * 名称：runtime.hpp
 * 说明：运行模块的登记函数声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 登记运行模块。 */
void registerRuntime(ModuleRegistry& registry);

}  // namespace ember::bootstrap
