/*
 * ------------------------------------------------------------------------
 * 名称：runtime-requests.hpp
 * 说明：runtime 模块的请求登记：把 runtime.* 请求名映射到运行服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/runtime/runtime-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::runtime {

/** 登记 runtime 模块的请求。 */
void addRuntimeRequests(platform::RequestRouter& router, std::shared_ptr<contracts::runtime::RuntimeService> service);

}  // namespace ember::requests::runtime
