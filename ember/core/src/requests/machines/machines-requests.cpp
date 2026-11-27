/*
 * ------------------------------------------------------------------------
 * 名称：machines-requests.cpp
 * 说明：machines 模块的请求登记实现：校验参数，调用机型服务，返回结果。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "requests/machines/machines-requests.hpp"

namespace ember::requests::machines {

namespace contract = ::ember::contracts::machines;

void addMachineRequests(platform::RequestRouter& router, std::shared_ptr<contract::MachineService> service) {
  router.add("machines", contract::MACHINES_LIST_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMachinesListParams(params, "params"));
    return nlohmann::json(service->list());
  });
  router.add("machines", contract::MACHINES_SET_ROM_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->setRom(contract::parseMachinesSetRomParams(params, "params")));
  });
  router.add("machines", contract::MACHINES_SET_ROMS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->setRoms(contract::parseMachinesSetRomsParams(params, "params")));
  });
  router.add("machines", contract::MACHINES_REMOVE_ROM_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->removeRom(contract::parseMachinesRemoveRomParams(params, "params")));
  });
  router.add("machines", contract::MACHINES_BOOT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->boot(contract::parseMachinesBootParams(params, "params")));
  });
}

}  // namespace ember::requests::machines
