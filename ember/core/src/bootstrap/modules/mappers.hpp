/*
 * ------------------------------------------------------------------------
 * 名称：mappers.hpp
 * 说明：mapper 插件模块的登记函数声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 登记 mapper 插件模块。 */
void registerMappers(ModuleRegistry& registry);

}  // namespace ember::bootstrap
