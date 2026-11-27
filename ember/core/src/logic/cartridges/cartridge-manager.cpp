/*
 * ------------------------------------------------------------------------
 * 名称：cartridge-manager.cpp
 * 说明：卡带服务的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/cartridges/cartridge-manager.hpp"

#include <utility>

#include "contracts/cartridges/cartridge-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::cartridges {

namespace api = contracts::cartridges;

namespace {

/** 8 位大写十六进制，与卡带描述里的写法一致。 */
std::string hex8(std::uint32_t value) {
  static constexpr char DIGITS[] = "0123456789ABCDEF";
  std::string text(8, '0');
  for (int index = 7; index >= 0; --index, value >>= 4) text[static_cast<std::size_t>(index)] = DIGITS[value & 0xFU];
  return text;
}

}  // namespace

CartridgeManager::CartridgeManager(std::shared_ptr<api::CartridgeFileReader> files, std::shared_ptr<api::CartridgeLoader> loader,
                                   std::shared_ptr<contracts::runtime::RuntimeService> runtime, std::shared_ptr<platform::EventSink> events)
    : files_(std::move(files)), loader_(std::move(loader)), runtime_(std::move(runtime)), events_(std::move(events)) {}

api::CartridgesLoadResult CartridgeManager::load(const api::CartridgesLoadParams& params) {
  const auto file = files_->read(params.path);
  auto plan = loader_->plan(file);
  contracts::runtime::CartridgeInsertion insertion;
  insertion.image = std::move(plan.image);
  insertion.match = std::move(plan.match);
  insertion.source = params.path;
  insertion.powerOn = params.powerOn.value_or(true);
  insertion.resume = params.resume.value_or(true);
  auto inserted = runtime_->insertCartridge(std::move(insertion));
  events_->publish(api::CARTRIDGES_LOADED_EVENT, nlohmann::json(api::CartridgesLoadedData{inserted.cartridge}));
  return api::CartridgesLoadResult{std::move(inserted.cartridge), std::move(inserted.state)};
}

api::CartridgesEjectResult CartridgeManager::eject() { return api::CartridgesEjectResult{runtime_->ejectCartridge()}; }

api::CartridgesParseHeaderResult CartridgeManager::parseHeader(const api::CartridgesParseHeaderParams& params) {
  const auto file = files_->read(params.path);
  const auto image = loader_->parse(file);
  const auto& description = image.description;
  api::CartridgesParseHeaderResult result;
  result.header.path = params.path;
  result.header.mapper = description.mapper;
  result.header.variant = description.variant;
  result.header.prgSize = static_cast<std::int64_t>(description.prgSize);
  result.header.chrSize = static_cast<std::int64_t>(description.chrSize);
  result.header.mirroring = std::string(contracts::mappers::mirroringName(description.mirroring));
  result.header.battery = description.battery;
  result.header.crc32Prg = hex8(description.crc32Prg);
  result.header.crc32All = hex8(description.crc32All);
  // 匹配失败是预览的一部分，不当作请求失败。
  try {
    const auto match = loader_->match(description);
    result.match = api::HeaderMatch{match.pluginId, match.pluginName, match.version, match.host, match.devices};
  } catch (const shared::CoreError& failure) {
    result.matchError = api::MatchFailure{failure.code(), failure.what()};
  }
  return result;
}

api::CartridgesInfoResult CartridgeManager::info() const {
  auto current = runtime_->cartridge();
  if (!current) throw shared::CoreError(api::ERROR_EMPTY, "卡槽是空的，没有插入卡带");
  return api::CartridgesInfoResult{std::move(*current)};
}

}  // namespace ember::logic::cartridges
