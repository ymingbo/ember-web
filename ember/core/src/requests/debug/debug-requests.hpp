/*
 * ------------------------------------------------------------------------
 * 名称：debug-requests.hpp
 * 说明：debug 模块的请求登记：debug.status 返回调试服务状态；hello、subscribe、unsubscribe 只能经调试连接处理，经其他途径调用时报 debug.connectionOnly。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/debug/debug-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::debug {

/** 登记 debug 模块的请求。 */
void addDebugRequests(platform::RequestRouter& router, std::shared_ptr<contracts::debug::DebugService> service);

}  // namespace ember::requests::debug
