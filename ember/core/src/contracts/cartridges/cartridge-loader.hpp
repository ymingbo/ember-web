/*
 * ------------------------------------------------------------------------
 * 名称：cartridge-loader.hpp
 * 说明：cartridges 契约：卡带装载。解析 NES 文件并匹配插件得到装配计划，再按计划创建卡带。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：加载流程见 docs/03-mapper插件规范.md 第 5 节：解析 → 匹配 → 调用方按主机型号装配主机 → 创建卡带并插入卡槽。
 *       运行模块（以后）用 plan 得到主机型号，装配主机，再用 assemble 创建卡带。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "contracts/hardware/button-input.hpp"
#include "contracts/hardware/irq-output.hpp"
#include "contracts/mappers/cartridge-image.hpp"
#include "contracts/mappers/mapper-cartridge.hpp"
#include "contracts/mappers/mapper-factory.hpp"

namespace ember::contracts::cartridges {

/** 装配计划：卡带映像与匹配到的插件。 */
struct CartridgePlan {
  mappers::CartridgeImage image;
  mappers::MapperMatch match;
};

/** 卡带装载。 */
class CartridgeLoader {
 public:
  virtual ~CartridgeLoader() = default;

  /**
   * 解析 NES 文件并匹配插件。
   * @throws shared::CoreError cartridges.notInes、cartridges.truncated、cartridges.unsupportedFormat、cartridges.invalidHeader，
   *         以及 mappers.noMatch、mappers.conflict、mappers.hostUnknown。
   */
  [[nodiscard]] virtual CartridgePlan plan(std::span<const std::uint8_t> file) const = 0;

  /**
   * 只解析 NES 文件头与内容，不匹配插件。
   * @throws shared::CoreError cartridges.notInes、cartridges.truncated、cartridges.unsupportedFormat、cartridges.invalidHeader。
   */
  [[nodiscard]] virtual mappers::CartridgeImage parse(std::span<const std::uint8_t> file) const = 0;

  /**
   * 按卡带描述匹配插件。
   * @throws shared::CoreError mappers.noMatch、mappers.conflict、mappers.hostUnknown。
   */
  [[nodiscard]] virtual mappers::MapperMatch match(const mappers::CartridgeDescription& description) const = 0;

  /**
   * 按计划创建卡带。
   * @param irq 主机卡槽上的 IRQ 引脚，必须活到卡带销毁。
   * @param pads 主机上的手柄口，必须活到卡带销毁。
   */
  [[nodiscard]] virtual std::unique_ptr<mappers::MapperCartridge> assemble(const CartridgePlan& plan, hardware::IrqOutput& irq, hardware::PadSink& pads) const = 0;
};

}  // namespace ember::contracts::cartridges
