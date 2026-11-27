/*
 * ------------------------------------------------------------------------
 * 名称：inspector.hpp
 * 说明：检查器模块的登记函数声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 登记检查器模块：创建服务（取用运行服务）并登记 inspector.* 请求；须排在 runtime 之后。 */
void registerInspector(ModuleRegistry& registry);

}  // namespace ember::bootstrap
