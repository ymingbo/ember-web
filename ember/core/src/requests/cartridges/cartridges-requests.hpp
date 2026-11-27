/*
 * ------------------------------------------------------------------------
 * 名称：cartridges-requests.hpp
 * 说明：cartridges 模块的请求登记：把 cartridges.* 请求名映射到卡带服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/cartridges/cartridge-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::cartridges {

/** 登记 cartridges 模块的请求。 */
void addCartridgeRequests(platform::RequestRouter& router, std::shared_ptr<contracts::cartridges::CartridgeService> service);

}  // namespace ember::requests::cartridges
