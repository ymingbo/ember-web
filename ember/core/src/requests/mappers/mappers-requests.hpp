/*
 * ------------------------------------------------------------------------
 * 名称：mappers-requests.hpp
 * 说明：mappers 模块的请求登记：把 mappers.* 请求名映射到插件管理服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/mappers/mapper-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::mappers {

/** 登记 mappers 模块的请求。 */
void addMapperRequests(platform::RequestRouter& router, std::shared_ptr<contracts::mappers::MapperService> service);

}  // namespace ember::requests::mappers
