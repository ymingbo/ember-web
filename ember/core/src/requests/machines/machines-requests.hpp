/*
 * ------------------------------------------------------------------------
 * 名称：machines-requests.hpp
 * 说明：machines 模块的请求登记：把 machines.* 请求名映射到机型服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/machines/machine-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::machines {

/** 登记 machines 模块的请求。 */
void addMachineRequests(platform::RequestRouter& router, std::shared_ptr<contracts::machines::MachineService> service);

}  // namespace ember::requests::machines
