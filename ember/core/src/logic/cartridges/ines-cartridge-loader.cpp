/*
 * ------------------------------------------------------------------------
 * 名称：ines-cartridge-loader.cpp
 * 说明：卡带装载的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/cartridges/ines-cartridge-loader.hpp"

#include <utility>

#include "logic/cartridges/ines-parser.hpp"

namespace ember::logic::cartridges {

contracts::cartridges::CartridgePlan InesCartridgeLoader::plan(std::span<const std::uint8_t> file) const {
  contracts::cartridges::CartridgePlan result;
  result.image = parse(file);
  result.match = match(result.image.description);
  return result;
}

contracts::mappers::CartridgeImage InesCartridgeLoader::parse(std::span<const std::uint8_t> file) const { return parseInes(file); }

contracts::mappers::MapperMatch InesCartridgeLoader::match(const contracts::mappers::CartridgeDescription& description) const { return factory_->match(description); }

std::unique_ptr<contracts::mappers::MapperCartridge> InesCartridgeLoader::assemble(const contracts::cartridges::CartridgePlan& plan, contracts::hardware::IrqOutput& irq,
                                                                                    contracts::hardware::PadSink& pads) const {
  return factory_->create(plan.match, plan.image, irq, pads);
}

}  // namespace ember::logic::cartridges
