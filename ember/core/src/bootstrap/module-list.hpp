/*
 * ------------------------------------------------------------------------
 * 名称：module-list.hpp
 * 说明：C++ 模块清单：核心启动时按清单顺序登记全部模块。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#pragma once

#include <span>

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 全部模块，按登记顺序排列。 */
std::span<const ModuleRegistration> moduleList();

}  // namespace ember::bootstrap
