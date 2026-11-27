/*
 * ------------------------------------------------------------------------
 * 名称：system.cpp
 * 说明：系统模块的登记：创建系统服务的实现并登记 system.* 请求。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/system.hpp"

#include "logic/system/local-system-service.hpp"
#include "requests/system/system-requests.hpp"

namespace ember::bootstrap {

void registerSystem(ModuleRegistry& registry) {
  auto service = std::make_shared<logic::system::LocalSystemService>(registry.appVersion, registry.coreVersion);
  requests::system::addSystemRequests(registry.requests, service);
}

}  // namespace ember::bootstrap
