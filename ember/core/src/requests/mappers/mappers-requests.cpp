/*
 * ------------------------------------------------------------------------
 * 名称：mappers-requests.cpp
 * 说明：mappers 模块的请求登记实现：校验参数，调用插件管理服务，返回结果。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "requests/mappers/mappers-requests.hpp"

namespace ember::requests::mappers {

namespace contract = ::ember::contracts::mappers;

void addMapperRequests(platform::RequestRouter& router, std::shared_ptr<contract::MapperService> service) {
  router.add("mappers", contract::MAPPERS_LIST_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMappersListParams(params, "params"));
    return nlohmann::json(service->list());
  });
  router.add("mappers", contract::MAPPERS_INFO_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->info(contract::parseMappersInfoParams(params, "params").id));
  });
  router.add("mappers", contract::MAPPERS_INSTALL_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->install(contract::parseMappersInstallParams(params, "params").path));
  });
  router.add("mappers", contract::MAPPERS_UNINSTALL_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    service->uninstall(contract::parseMappersUninstallParams(params, "params").id);
    return nlohmann::json(contract::MappersUninstallResult{});
  });
  router.add("mappers", contract::MAPPERS_SET_ENABLED_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseMappersSetEnabledParams(params, "params");
    return nlohmann::json(service->setEnabled(parsed.id, parsed.enabled));
  });
  router.add("mappers", contract::MAPPERS_RELOAD_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMappersReloadParams(params, "params"));
    return nlohmann::json(service->reload());
  });
}

}  // namespace ember::requests::mappers
