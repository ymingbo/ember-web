/*
 * ------------------------------------------------------------------------
 * 名称：cartridges-requests.cpp
 * 说明：cartridges 模块的请求登记实现：校验参数，调用卡带服务，返回结果。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "requests/cartridges/cartridges-requests.hpp"

namespace ember::requests::cartridges {

namespace contract = ::ember::contracts::cartridges;

void addCartridgeRequests(platform::RequestRouter& router, std::shared_ptr<contract::CartridgeService> service) {
  router.add("cartridges", contract::CARTRIDGES_LOAD_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->load(contract::parseCartridgesLoadParams(params, "params")));
  });
  router.add("cartridges", contract::CARTRIDGES_PARSE_HEADER_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->parseHeader(contract::parseCartridgesParseHeaderParams(params, "params")));
  });
  router.add("cartridges", contract::CARTRIDGES_EJECT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseCartridgesEjectParams(params, "params"));
    return nlohmann::json(service->eject());
  });
  router.add("cartridges", contract::CARTRIDGES_INFO_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseCartridgesInfoParams(params, "params"));
    return nlohmann::json(service->info());
  });
}

}  // namespace ember::requests::cartridges
