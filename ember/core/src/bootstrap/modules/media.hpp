/*
 * ------------------------------------------------------------------------
 * 名称：media.hpp
 * 说明：介质模块的登记函数声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 登记介质模块。 */
void registerMedia(ModuleRegistry& registry);

}  // namespace ember::bootstrap
