/*
 * ------------------------------------------------------------------------
 * 名称：cartridges.cpp
 * 说明：卡带模块的登记：创建 NES 文件的装载实现与卡带服务，登记 cartridges.* 请求。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：取用 mappers 模块的卡带工厂与 runtime 模块的运行服务，须排在它们之后登记。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/cartridges.hpp"

#include <memory>

#include "contracts/cartridges/cartridge-loader.hpp"
#include "contracts/cartridges/cartridge-service.hpp"
#include "contracts/mappers/mapper-factory.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "infra/cartridges/disk-cartridge-file-reader.hpp"
#include "logic/cartridges/cartridge-manager.hpp"
#include "logic/cartridges/ines-cartridge-loader.hpp"
#include "requests/cartridges/cartridges-requests.hpp"

namespace ember::bootstrap {

void registerCartridges(ModuleRegistry& registry) {
  auto loader = std::make_shared<logic::cartridges::InesCartridgeLoader>(registry.services.require<contracts::mappers::MapperFactory>());
  registry.services.provide<contracts::cartridges::CartridgeLoader>(loader);
  auto service = std::make_shared<logic::cartridges::CartridgeManager>(std::make_shared<infra::cartridges::DiskCartridgeFileReader>(), loader,
                                                                       registry.services.require<contracts::runtime::RuntimeService>(), registry.events);
  registry.services.provide<contracts::cartridges::CartridgeService>(service);
  requests::cartridges::addCartridgeRequests(registry.requests, service);
}

}  // namespace ember::bootstrap
