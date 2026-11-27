/*
 * ------------------------------------------------------------------------
 * 名称：inspector-requests.hpp
 * 说明：inspector 模块的请求登记：把 inspector.* 请求转给检查器服务。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/inspector/inspector-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::inspector {

/** 登记 inspector 模块的请求。 */
void addInspectorRequests(platform::RequestRouter& router, std::shared_ptr<contracts::inspector::InspectorService> service);

}  // namespace ember::requests::inspector
