/*
 * ------------------------------------------------------------------------
 * 名称：filemanager.hpp
 * 说明：媒体库模块的登记函数声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 登记媒体库模块。 */
void registerFileManager(ModuleRegistry& registry);

}  // namespace ember::bootstrap
