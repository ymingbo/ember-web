/*
 * ------------------------------------------------------------------------
 * 名称：machines.hpp
 * 说明：机型模块的登记函数声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 登记机型模块。 */
void registerMachines(ModuleRegistry& registry);

}  // namespace ember::bootstrap
