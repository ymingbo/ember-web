/*
 * ------------------------------------------------------------------------
 * 名称：input.hpp
 * 说明：输入注入模块的登记函数声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include "bootstrap/module-registry.hpp"

namespace ember::bootstrap {

/** 登记输入模块：创建手柄绑定服务与输入路由器（挂到运行服务上）、输入注入服务，并登记 input.* 请求；须排在 runtime 之后。 */
void registerInput(ModuleRegistry& registry);

}  // namespace ember::bootstrap
