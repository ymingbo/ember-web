/*
 * ------------------------------------------------------------------------
 * 名称：cartridge-manager.hpp
 * 说明：卡带服务的实现：读取文件、解析并匹配插件、交给运行服务装配主机并插入卡槽，装配完成后发 cartridges.loaded。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：读文件、解析与匹配在请求线程完成，不碰硬件状态；装配主机与插入卡槽由运行服务在模拟线程完成。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/cartridges/cartridge-file-reader.hpp"
#include "contracts/cartridges/cartridge-loader.hpp"
#include "contracts/cartridges/cartridge-service.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "platform/events/event-sink.hpp"

namespace ember::logic::cartridges {

/** 卡带服务。 */
class CartridgeManager final : public contracts::cartridges::CartridgeService {
 public:
  CartridgeManager(std::shared_ptr<contracts::cartridges::CartridgeFileReader> files, std::shared_ptr<contracts::cartridges::CartridgeLoader> loader,
                   std::shared_ptr<contracts::runtime::RuntimeService> runtime, std::shared_ptr<platform::EventSink> events);

  contracts::cartridges::CartridgesLoadResult load(const contracts::cartridges::CartridgesLoadParams& params) override;
  contracts::cartridges::CartridgesEjectResult eject() override;
  contracts::cartridges::CartridgesParseHeaderResult parseHeader(const contracts::cartridges::CartridgesParseHeaderParams& params) override;
  [[nodiscard]] contracts::cartridges::CartridgesInfoResult info() const override;

 private:
  std::shared_ptr<contracts::cartridges::CartridgeFileReader> files_;
  std::shared_ptr<contracts::cartridges::CartridgeLoader> loader_;
  std::shared_ptr<contracts::runtime::RuntimeService> runtime_;
  std::shared_ptr<platform::EventSink> events_;
};

}  // namespace ember::logic::cartridges
