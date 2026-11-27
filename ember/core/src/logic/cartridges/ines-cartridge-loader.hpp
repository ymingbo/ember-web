/*
 * ------------------------------------------------------------------------
 * 名称：ines-cartridge-loader.hpp
 * 说明：卡带装载的实现：解析 iNES 或 NES 2.0 文件，经 mapper 契约匹配插件并创建卡带。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <utility>

#include "contracts/cartridges/cartridge-loader.hpp"
#include "contracts/mappers/mapper-factory.hpp"

namespace ember::logic::cartridges {

/** 按 NES 文件装载卡带。 */
class InesCartridgeLoader final : public contracts::cartridges::CartridgeLoader {
 public:
  /** @param factory 卡带工厂，不能为空。 */
  explicit InesCartridgeLoader(std::shared_ptr<contracts::mappers::MapperFactory> factory) : factory_(std::move(factory)) {}

  [[nodiscard]] contracts::cartridges::CartridgePlan plan(std::span<const std::uint8_t> file) const override;
  [[nodiscard]] contracts::mappers::CartridgeImage parse(std::span<const std::uint8_t> file) const override;
  [[nodiscard]] contracts::mappers::MapperMatch match(const contracts::mappers::CartridgeDescription& description) const override;

  [[nodiscard]] std::unique_ptr<contracts::mappers::MapperCartridge> assemble(const contracts::cartridges::CartridgePlan& plan, contracts::hardware::IrqOutput& irq,
                                                                              contracts::hardware::PadSink& pads) const override;

 private:
  std::shared_ptr<contracts::mappers::MapperFactory> factory_;
};

}  // namespace ember::logic::cartridges
