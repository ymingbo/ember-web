/*
 * ------------------------------------------------------------------------
 * 名称：plugin-info.cpp
 * 说明：把已加载的插件转成 mappers 请求返回的摘要与详情。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/mappers/plugin-info.hpp"

#include <string>
#include <utility>

namespace ember::infra::mappers {

namespace {

namespace api = contracts::mappers;

std::string hex8(std::uint32_t value) {
  static constexpr char DIGITS[] = "0123456789ABCDEF";
  std::string text(8, '0');
  for (int index = 7; index >= 0; --index, value >>= 4) text[static_cast<std::size_t>(index)] = DIGITS[value & 0xFU];
  return text;
}

api::MapperMatchRule makeRule(const MatchRule& rule) {
  api::MapperMatchRule result;
  result.mapper = rule.mapper;
  if (rule.variant) result.variant = *rule.variant;
  if (rule.prgSize) result.prgSize = static_cast<std::int64_t>(*rule.prgSize);
  if (rule.chrSize) result.chrSize = static_cast<std::int64_t>(*rule.chrSize);
  if (rule.crc32Prg) result.crc32Prg = hex8(*rule.crc32Prg);
  if (rule.crc32All) result.crc32All = hex8(*rule.crc32All);
  return result;
}

const char* busName(bool ppu) { return ppu ? "ppu" : "cpu"; }

}  // namespace

api::MapperSummary makeSummary(const LoadedPlugin& plugin, PluginSource source, bool enabled) {
  const Manifest& manifest = plugin.manifest;
  api::MapperSummary summary;
  summary.id = manifest.id;
  summary.name = manifest.name;
  summary.version = manifest.version;
  summary.publisher = manifest.publisher;
  if (!manifest.description.empty()) summary.description = manifest.description;
  summary.host = manifest.host;
  summary.devices = manifest.devices;
  for (const MatchRule& rule : manifest.match) summary.match.push_back(makeRule(rule));
  summary.source = source == PluginSource::Bundled ? "bundled" : (source == PluginSource::Development ? "development" : "user");
  summary.enabled = enabled;
  return summary;
}

api::MapperDetail makeDetail(const LoadedPlugin& plugin, PluginSource source, bool enabled) {
  const Manifest& manifest = plugin.manifest;
  api::MapperDetail detail;
  detail.summary = makeSummary(plugin, source, enabled);
  for (const StorageDecl& decl : manifest.storage) {
    api::MapperStorageInfo info;
    info.id = decl.id;
    info.kind = decl.ram ? "ram" : "rom";
    if (decl.from != StorageSource::None) info.from = decl.from == StorageSource::FilePrg ? "file.prg" : "file.chr";
    if (decl.ram) info.size = decl.size;
    info.battery = decl.battery;
    if (decl.fallback) info.fallbackSize = decl.fallback->size;
    detail.storage.push_back(std::move(info));
  }
  for (const WindowDecl& decl : manifest.windows) {
    api::MapperWindowInfo info;
    info.id = decl.id;
    info.bus = busName(decl.ppu);
    info.range = formatRange(decl.range);
    info.storage = decl.storage;
    for (std::size_t index = 1; index < decl.storageChoices.size(); ++index) info.storage += "," + decl.storageChoices[index];
    info.size = decl.size;
    info.initial = decl.initialKind == InitialKind::Bank ? std::to_string(decl.initialBank) : (decl.initialKind == InitialKind::Last ? "last" : "first");
    info.access = decl.writable ? "rw" : "r";
    detail.windows.push_back(std::move(info));
  }
  detail.nametable = std::string(nametableName(manifest.nametable));
  for (const RegisterDecl& decl : manifest.registers) detail.registers.push_back(api::MapperRegisterInfo{busName(decl.ppu), formatRange(decl.range), decl.read, decl.write});
  for (const StateDecl& decl : manifest.state) detail.state.push_back(api::MapperStateInfo{decl.name, decl.bits, decl.description});
  for (const SettingDecl& decl : manifest.settings) {
    api::MapperSettingInfo info;
    info.key = decl.key;
    info.label = decl.label;
    info.kind = decl.kind == SettingKind::Switch ? "switch" : (decl.kind == SettingKind::Choice ? "choice" : "text");
    info.defaultValue = decl.kind == SettingKind::Switch ? nlohmann::json(decl.switchDefault) : nlohmann::json(decl.textDefault);
    if (!decl.description.empty()) info.description = decl.description;
    if (decl.kind == SettingKind::Choice) {
      std::vector<api::MapperSettingOption> options;
      for (const SettingOption& option : decl.options) options.push_back(api::MapperSettingOption{option.value, option.label});
      info.options = std::move(options);
    }
    detail.settings.push_back(std::move(info));
  }
  return detail;
}

}  // namespace ember::infra::mappers
