/*
 * ------------------------------------------------------------------------
 * 名称：debug-requests.cpp
 * 说明：debug 模块的请求登记实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "requests/debug/debug-requests.hpp"

#include "contracts/debug/debug-error.hpp"
#include "contracts/debug/generated/debug.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::requests::debug {

namespace contract = ::ember::contracts::debug;

void addDebugRequests(platform::RequestRouter& router, std::shared_ptr<contract::DebugService> service) {
  router.add("debug", contract::DEBUG_STATUS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseDebugStatusParams(params, "params"));
    return nlohmann::json(service->status());
  });
  // 握手与订阅属于连接：由调试服务器在会话里处理，不经路由；这里登记只是让请求名与 Schema 一致，被直接调用时说明原因。
  for (const auto name : {contract::DEBUG_HELLO_REQUEST, contract::DEBUG_SUBSCRIBE_REQUEST, contract::DEBUG_UNSUBSCRIBE_REQUEST}) {
    router.add("debug", name, [](const nlohmann::json&, const platform::CancellationToken&) -> nlohmann::json {
      throw shared::CoreError(contract::ERROR_CONNECTION_ONLY, "这个请求只能经调试连接（WebSocket）调用");
    });
  }
}

}  // namespace ember::requests::debug
