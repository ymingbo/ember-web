/*
 * ------------------------------------------------------------------------
 * 名称：plugin-registry.cpp
 * 说明：mapper 插件的登记与管理的实现：目录扫描、安装检查、卸载、启停、匹配与创建卡带。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：安装检查按 docs/03 第 7 节：包结构与大小、清单符合规范、apiVersion 受支持、host 里至少有一个存在的主机型号、
 *       main.js 能加载并导出声明的函数（用空卡带试创建一次）。
 *       插件包来自外部：用户目录里的文件名只由插件标识生成（小写字母、数字与 -），不会写到目录之外。
 * ------------------------------------------------------------------------
 */

#include "infra/mappers/plugin-registry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "contracts/mappers/mapper-error.hpp"
#include "infra/mappers/emap-package.hpp"
#include "infra/mappers/mapper-chip.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::mappers {

namespace {

namespace fs = std::filesystem;
namespace api = contracts::mappers;

constexpr const char* PACKAGE_EXTENSION = ".emap";
constexpr const char* STATE_FILE = "state.json";

/** 试创建卡带时用的空 IRQ 引脚。 */
class NullIrqOutput final : public contracts::hardware::IrqOutput {
 public:
  void set(bool) override {}
};

/** 试创建卡带时用的空手柄口：有两个口，设置的按键直接丢弃。 */
class NullPadSink final : public contracts::hardware::PadSink {
 public:
  [[nodiscard]] unsigned padPortCount() const override { return 2; }
  void setButtons(unsigned, std::uint8_t) override {}
};

std::string toUtf8(const fs::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

fs::path fromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

bool isPackageFile(const fs::path& path) {
  std::string extension = toUtf8(path.extension());
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == PACKAGE_EXTENSION;
}

std::string join(const std::vector<std::string>& items) {
  std::string text;
  for (const std::string& item : items) text += (text.empty() ? "" : "、") + item;
  return text;
}

/** 读文件，超过 limit 字节或读不了时抛出 mappers.invalidPackage。 */
std::vector<std::uint8_t> readFile(const fs::path& path, std::size_t limit) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error) throw shared::CoreError(api::ERROR_INVALID_PACKAGE, "无法读取插件包：" + toUtf8(path.filename()) + "（" + error.message() + "）");
  if (size > limit) throw shared::CoreError(api::ERROR_INVALID_PACKAGE, "插件包超过 " + std::to_string(limit / 1024 / 1024) + " MB 的上限：" + toUtf8(path.filename()));
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw shared::CoreError(api::ERROR_INVALID_PACKAGE, "无法打开插件包：" + toUtf8(path.filename()));
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (static_cast<std::size_t>(stream.gcount()) != bytes.size()) throw shared::CoreError(api::ERROR_INVALID_PACKAGE, "读取插件包时文件被截断：" + toUtf8(path.filename()));
  return bytes;
}

/** 先写临时文件再改名，避免写到一半留下半个文件。 */
void writeFileAtomically(const fs::path& path, const std::string& content) {
  std::error_code error;
  fs::create_directories(path.parent_path(), error);
  if (error) throw std::runtime_error("无法创建目录：" + error.message());
  fs::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.flush();
    if (!stream) throw std::runtime_error("无法写入文件：" + toUtf8(path.filename()));
  }
  fs::rename(temporary, path, error);
  if (error) {
    fs::remove(temporary, error);
    throw std::runtime_error("无法保存文件：" + toUtf8(path.filename()));
  }
}

}  // namespace

PluginRegistry::PluginRegistry(PluginRegistryOptions options) : options_(std::move(options)) {
  loadDisabled();
  scanDirectory(options_.bundledDirectory, PluginSource::Bundled, entries_);
  scanDirectory(options_.userDirectory, PluginSource::User, entries_);
  scanDevelopmentDirectory(entries_);
}

// ---------------------------------------------------------------- 检查与读取

std::shared_ptr<const LoadedPlugin> PluginRegistry::checkPackage(std::span<const std::uint8_t> bytes) const { return checkContents(readEmapPackage(bytes)); }

std::shared_ptr<const LoadedPlugin> PluginRegistry::checkContents(const EmapPackage& package) const {
  auto plugin = std::make_shared<LoadedPlugin>();
  plugin->manifest = parseManifest(package.manifestText);
  plugin->script = package.scriptText;
  plugin->hasIcon = package.hasIcon;
  const Manifest& manifest = plugin->manifest;

  std::vector<std::string> knownHosts;
  for (const std::string& host : manifest.host) {
    if (options_.hosts.hosts.contains(host)) knownHosts.push_back(host);
  }
  if (knownHosts.empty()) {
    std::vector<std::string> available;
    for (const auto& host : options_.hosts.hosts) available.push_back(host);
    throw shared::CoreError(api::ERROR_HOST_UNKNOWN, "插件 " + manifest.id + " 要求的主机型号 " + join(manifest.host) + " 都不存在；程序支持的型号：" + join(available));
  }

  // 用空卡带试创建一次：检查脚本能加载并导出了清单声明的函数。
  NullIrqOutput irq;
  NullPadSink pads;
  const MapperChip probe(plugin, api::CartridgeImage{}, irq, pads, options_.limits, options_.circuits.get());
  return plugin;
}

void PluginRegistry::scanDirectory(const fs::path& directory, PluginSource source, std::vector<Entry>& into) const {
  std::error_code error;
  if (directory.empty() || !fs::is_directory(directory, error)) return;
  std::vector<fs::path> files;
  for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
    if (it->is_regular_file(error) && isPackageFile(it->path())) files.push_back(it->path());
  }
  std::sort(files.begin(), files.end());
  for (const fs::path& file : files) {
    try {
      into.push_back(Entry{checkPackage(readFile(file, MAX_PACKAGE_BYTES)), source, source == PluginSource::User ? file : fs::path()});
    } catch (const shared::CoreError& failure) {
      logError(failure.code(), failure.what(), toUtf8(file.filename()));
    }
  }
}

void PluginRegistry::scanDevelopmentDirectory(std::vector<Entry>& into) const {
  if (!options_.developmentDirectory) return;
  const fs::path directory = options_.developmentDirectory();
  std::error_code error;
  if (directory.empty() || !fs::is_directory(directory, error)) return;
  std::vector<fs::path> children;
  for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
    if (it->is_directory(error)) children.push_back(it->path());
  }
  std::sort(children.begin(), children.end());
  for (const fs::path& child : children) {
    try {
      into.push_back(Entry{checkContents(readEmapDirectory(child)), PluginSource::Development, fs::path()});
    } catch (const shared::CoreError& failure) {
      logError(failure.code(), failure.what(), toUtf8(child.filename()));
    }
  }
}

void PluginRegistry::loadDisabled() {
  disabled_.clear();
  if (options_.userDirectory.empty()) return;
  const fs::path file = options_.userDirectory / STATE_FILE;
  std::error_code error;
  if (!fs::is_regular_file(file, error)) return;
  std::ifstream stream(file, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const nlohmann::json state = nlohmann::json::parse(text, nullptr, false);
  if (state.is_object() && state.contains("disabled") && state["disabled"].is_array()) {
    for (const auto& item : state["disabled"]) {
      if (item.is_string()) disabled_.insert(item.get<std::string>());
    }
  } else {
    logError("mappers.invalidState", "插件状态文件无法识别，已按全部启用处理", STATE_FILE);
  }
}

void PluginRegistry::saveDisabled(const std::set<std::string>& disabled) const {
  if (options_.userDirectory.empty()) return;
  const nlohmann::json state{{"disabled", std::vector<std::string>(disabled.begin(), disabled.end())}};
  writeFileAtomically(options_.userDirectory / STATE_FILE, state.dump(2));
}

void PluginRegistry::logError(const std::string& code, const std::string& message, const std::string& file) const {
  if (options_.errors) options_.errors->write({"mappers", code, message, {{"file", file}}});
}

void PluginRegistry::publishChanged(const std::string* id) const {
  if (!options_.events) return;
  options_.events->publish(api::MAPPERS_CHANGED_EVENT, nlohmann::json{{"id", id != nullptr ? nlohmann::json(*id) : nlohmann::json(nullptr)}});
}

std::vector<PluginRegistry::Entry> PluginRegistry::effectiveEntriesLocked() const {
  std::map<std::string, Entry> best;
  for (const Entry& entry : entries_) {
    const std::string& id = entry.plugin->manifest.id;
    const auto found = best.find(id);
    if (found == best.end()) {
      best.emplace(id, entry);
      continue;
    }
    // 开发目录里的插件优先，不比版本。
    const bool candidateDevelopment = entry.source == PluginSource::Development;
    if (candidateDevelopment != (found->second.source == PluginSource::Development)) {
      if (candidateDevelopment) found->second = entry;
      continue;
    }
    const Version& current = found->second.plugin->manifest.versionNumber;
    const Version& candidate = entry.plugin->manifest.versionNumber;
    if (candidate > current || (candidate == current && entry.source == PluginSource::User && found->second.source == PluginSource::Bundled)) found->second = entry;
  }
  std::vector<Entry> result;
  for (auto& item : best) result.push_back(std::move(item.second));
  return result;
}

std::optional<PluginRegistry::Entry> PluginRegistry::findEffectiveLocked(const std::string& id) const {
  for (Entry& entry : effectiveEntriesLocked()) {
    if (entry.plugin->manifest.id == id) return std::move(entry);
  }
  return std::nullopt;
}

// ---------------------------------------------------------------- MapperService

api::MappersListResult PluginRegistry::list() const {
  const std::lock_guard lock(mutex_);
  api::MappersListResult result;
  for (const Entry& entry : effectiveEntriesLocked()) result.plugins.push_back(makeSummary(*entry.plugin, entry.source, !disabled_.contains(entry.plugin->manifest.id)));
  return result;
}

api::MappersInfoResult PluginRegistry::info(const std::string& id) const {
  const std::lock_guard lock(mutex_);
  const auto entry = findEffectiveLocked(id);
  if (!entry) throw shared::CoreError(api::ERROR_NOT_FOUND, "没有标识为 " + id + " 的插件");
  api::MappersInfoResult result;
  result.plugin = makeDetail(*entry->plugin, entry->source, !disabled_.contains(id));
  return result;
}

api::MapperSummary PluginRegistry::installPackage(std::span<const std::uint8_t> bytes) {
  const std::shared_ptr<const LoadedPlugin> plugin = checkPackage(bytes);
  const std::string id = plugin->manifest.id;
  fs::path file;
  if (!options_.userDirectory.empty()) {
    file = options_.userDirectory / (id + PACKAGE_EXTENSION);
    writeFileAtomically(file, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  }
  api::MapperSummary summary;
  {
    const std::lock_guard lock(mutex_);
    std::erase_if(entries_, [&](const Entry& entry) { return entry.source == PluginSource::User && entry.plugin->manifest.id == id; });
    entries_.push_back(Entry{plugin, PluginSource::User, file});
    summary = makeSummary(*plugin, PluginSource::User, !disabled_.contains(id));
  }
  publishChanged(&id);
  return summary;
}

api::MappersInstallResult PluginRegistry::install(const std::string& path) {
  const fs::path file = fromUtf8(path);
  if (!isPackageFile(file)) throw shared::CoreError(api::ERROR_INVALID_PACKAGE, "插件包的扩展名应为 .emap：" + toUtf8(file.filename()));
  const std::vector<std::uint8_t> bytes = readFile(file, MAX_PACKAGE_BYTES);
  api::MappersInstallResult result;
  result.plugin = installPackage(bytes);
  return result;
}

void PluginRegistry::uninstall(const std::string& id) {
  {
    const std::lock_guard lock(mutex_);
    const bool anyUser = std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.source == PluginSource::User && entry.plugin->manifest.id == id; });
    if (!anyUser) {
      const bool anyDevelopment = std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.source == PluginSource::Development && entry.plugin->manifest.id == id; });
      if (anyDevelopment) throw shared::CoreError(api::ERROR_DEVELOPMENT_READONLY, "开发目录里的插件 " + id + " 不能卸载，从开发目录里删除它的目录后重新加载");
      const bool anyBundled = std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.plugin->manifest.id == id; });
      if (anyBundled) throw shared::CoreError(api::ERROR_BUNDLED_READONLY, "预装的插件 " + id + " 只能停用，不能卸载");
      throw shared::CoreError(api::ERROR_NOT_FOUND, "没有标识为 " + id + " 的插件");
    }
    for (const Entry& entry : entries_) {
      if (entry.source != PluginSource::User || entry.plugin->manifest.id != id || entry.file.empty()) continue;
      std::error_code error;
      fs::remove(entry.file, error);
      if (error) throw std::runtime_error("无法删除插件文件：" + error.message());
    }
    std::erase_if(entries_, [&](const Entry& entry) { return entry.source == PluginSource::User && entry.plugin->manifest.id == id; });
    const bool remains = std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.plugin->manifest.id == id; });
    if (!remains && disabled_.contains(id)) {
      std::set<std::string> disabled = disabled_;
      disabled.erase(id);
      saveDisabled(disabled);
      disabled_ = std::move(disabled);
    }
  }
  publishChanged(&id);
}

api::MappersSetEnabledResult PluginRegistry::setEnabled(const std::string& id, bool enabled) {
  api::MappersSetEnabledResult result;
  {
    const std::lock_guard lock(mutex_);
    const auto entry = findEffectiveLocked(id);
    if (!entry) throw shared::CoreError(api::ERROR_NOT_FOUND, "没有标识为 " + id + " 的插件");
    std::set<std::string> disabled = disabled_;
    if (enabled) disabled.erase(id);
    else disabled.insert(id);
    saveDisabled(disabled);
    disabled_ = std::move(disabled);
    result.plugin = makeSummary(*entry->plugin, entry->source, enabled);
  }
  publishChanged(&id);
  return result;
}

api::MappersReloadResult PluginRegistry::reload() {
  std::vector<Entry> fresh;
  scanDirectory(options_.bundledDirectory, PluginSource::Bundled, fresh);
  scanDirectory(options_.userDirectory, PluginSource::User, fresh);
  scanDevelopmentDirectory(fresh);
  api::MappersReloadResult result;
  {
    const std::lock_guard lock(mutex_);
    // 没有用户目录时，用户安装的插件只在内存里，重新读取时保留。
    if (options_.userDirectory.empty()) {
      for (const Entry& entry : entries_) {
        if (entry.source == PluginSource::User) fresh.push_back(entry);
      }
    }
    entries_ = std::move(fresh);
    loadDisabled();
    for (const Entry& entry : effectiveEntriesLocked()) result.plugins.push_back(makeSummary(*entry.plugin, entry.source, !disabled_.contains(entry.plugin->manifest.id)));
  }
  publishChanged(nullptr);
  return result;
}

// ---------------------------------------------------------------- MapperFactory

api::MapperMatch PluginRegistry::match(const api::CartridgeDescription& description) const {
  struct Candidate {
    int conditions;
    Entry entry;
  };
  std::vector<Candidate> candidates;
  std::vector<std::string> disabledMatches;
  {
    const std::lock_guard lock(mutex_);
    for (Entry& entry : effectiveEntriesLocked()) {
      int conditions = 0;
      for (const MatchRule& rule : entry.plugin->manifest.match) {
        if (rule.matches(description)) conditions = std::max(conditions, rule.conditionCount());
      }
      if (conditions == 0) continue;
      if (disabled_.contains(entry.plugin->manifest.id)) disabledMatches.push_back(entry.plugin->manifest.id);
      else candidates.push_back(Candidate{conditions, std::move(entry)});
    }
  }
  const std::string subject = "mapper 号 " + std::to_string(description.mapper) + "、变体 " + std::to_string(description.variant);
  if (candidates.empty()) {
    std::string message = "缺少 " + subject + " 的插件，请在“mapper 插件”页安装";
    if (!disabledMatches.empty()) message = subject + " 的插件 " + join(disabledMatches) + " 已被停用，请在“mapper 插件”页启用";
    throw shared::CoreError(api::ERROR_NO_MATCH, message);
  }
  const int best = std::max_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.conditions < b.conditions; })->conditions;
  std::vector<const Candidate*> top;
  for (const Candidate& candidate : candidates) {
    if (candidate.conditions == best) top.push_back(&candidate);
  }
  if (top.size() > 1) {
    std::vector<std::string> ids;
    for (const Candidate* candidate : top) ids.push_back(candidate->entry.plugin->manifest.id);
    throw shared::CoreError(api::ERROR_CONFLICT, subject + " 有 " + std::to_string(top.size()) + " 个插件同样匹配：" + join(ids) + "，请停用不需要的插件");
  }
  const Manifest& manifest = top.front()->entry.plugin->manifest;
  const auto host = std::find_if(manifest.host.begin(), manifest.host.end(), [&](const std::string& name) { return options_.hosts.hosts.contains(name); });
  if (host == manifest.host.end()) throw shared::CoreError(api::ERROR_HOST_UNKNOWN, "插件 " + manifest.id + " 要求的主机型号 " + join(manifest.host) + " 都不存在");
  return api::MapperMatch{manifest.id, manifest.name, manifest.version, *host, manifest.devices};
}

std::unique_ptr<api::MapperCartridge> PluginRegistry::create(const api::MapperMatch& match, const api::CartridgeImage& image, contracts::hardware::IrqOutput& irq,
                                                             contracts::hardware::PadSink& pads) {
  std::shared_ptr<const LoadedPlugin> plugin;
  {
    const std::lock_guard lock(mutex_);
    const auto entry = findEffectiveLocked(match.pluginId);
    if (!entry || disabled_.contains(match.pluginId)) throw shared::CoreError(api::ERROR_NOT_FOUND, "插件 " + match.pluginId + " 已被卸载或停用");
    plugin = entry->plugin;
  }
  return std::make_unique<MapperChip>(std::move(plugin), image, irq, pads, options_.limits, options_.circuits.get());
}

}  // namespace ember::infra::mappers
