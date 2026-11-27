/*
 * ------------------------------------------------------------------------
 * 名称：machines.cpp
 * 说明：机型模块的登记：创建 ROM 的磁盘存取与机型服务，登记 machines.* 请求。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：取用 runtime 模块的运行服务，须排在它之后登记；ROM 保存在用户数据目录的 roms/<机型>/ 下。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/machines.hpp"

#include <memory>

#include "contracts/machines/machine-service.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "infra/machines/disk-rom-storage.hpp"
#include "logic/machines/machine-manager.hpp"
#include "requests/machines/machines-requests.hpp"

namespace ember::bootstrap {

void registerMachines(ModuleRegistry& registry) {
  auto storage = std::make_shared<infra::machines::DiskRomStorage>(registry.userDataPath / "roms");
  auto service = std::make_shared<logic::machines::MachineManager>(storage, registry.services.require<contracts::runtime::RuntimeService>(), registry.events);
  // 运行服务只拿弱引用，避免与机型服务互相持有。
  registry.services.require<contracts::runtime::RuntimeService>()->setMachineRomSource([weak = std::weak_ptr<logic::machines::MachineManager>(service)](const std::string& machine) -> std::optional<contracts::runtime::MachineRom> {
    const auto manager = weak.lock();
    return manager ? manager->currentRom(machine) : std::nullopt;
  });
  registry.services.provide<contracts::machines::MachineService>(service);
  requests::machines::addMachineRequests(registry.requests, service);
}

}  // namespace ember::bootstrap
