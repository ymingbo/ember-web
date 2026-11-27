/*
 * ------------------------------------------------------------------------
 * 名称：system-requests.hpp
 * 说明：系统模块的请求登记：把 system.* 请求名映射到系统服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/system/system-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::system {

/** 登记系统模块的请求。 */
void addSystemRequests(platform::RequestRouter& router, std::shared_ptr<const contracts::system::SystemService> service);

}  // namespace ember::requests::system
