/*
 * ------------------------------------------------------------------------
 * 名称：system-requests.cpp
 * 说明：系统模块的请求登记实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#include "requests/system/system-requests.hpp"

namespace ember::requests::system {

namespace contract = ::ember::contracts::system;

void addSystemRequests(platform::RequestRouter& router, std::shared_ptr<const contract::SystemService> service) {
  router.add("system", contract::SYSTEM_VERSION_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseSystemVersionParams(params, "params"));
    return nlohmann::json(service->version());
  });
}

}  // namespace ember::requests::system
