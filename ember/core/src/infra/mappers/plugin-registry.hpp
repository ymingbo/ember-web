/*
 * ------------------------------------------------------------------------
 * 名称：plugin-registry.hpp
 * 说明：mapper 插件的登记与管理：读取预装目录与用户目录的插件包，安装、卸载、启停，按卡带描述匹配插件，并用匹配到的插件创建卡带。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：规则见 docs/03-mapper插件规范.md 第 4.2、5、7 节。开发目录里的插件优先于同标识的其他来源，其余版本高的生效，版本相同时用户安装的优先；
 *       用户目录为空时安装只保存在内存里（测试用）。线程安全：请求线程与模拟线程可以同时使用。
 *       后续阶段：冲突时用户选择并记住（本阶段条件数相同即报冲突）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "contracts/hardware/mapper-circuit-catalog.hpp"
#include "contracts/mappers/mapper-factory.hpp"
#include "contracts/mappers/mapper-service.hpp"
#include "infra/mappers/emap-package.hpp"
#include "infra/mappers/loaded-plugin.hpp"
#include "infra/mappers/plugin-info.hpp"
#include "infra/mappers/script-runtime.hpp"
#include "platform/events/event-sink.hpp"
#include "platform/logging/error-log.hpp"

namespace ember::infra::mappers {

/** 主机型号目录：程序里已有的主机型号名。插件的 host 按它检查。 */
struct HostCatalog {
  std::set<std::string> hosts;
};

/** 插件登记的配置。 */
struct PluginRegistryOptions {
  /** 预装插件目录，只读；不存在时视为没有预装插件。 */
  std::filesystem::path bundledDirectory;
  /** 用户插件目录；为空时安装只保存在内存里。 */
  std::filesystem::path userDirectory;
  /** 取开发目录的函数（每次读取时调用）：目录下每个子目录是一个未打包的插件；为空函数或返回空路径时不用开发目录。 */
  std::function<std::filesystem::path()> developmentDirectory;
  HostCatalog hosts;
  /** 错误日志，可以为空；跳过不合格的插件包时写入。 */
  std::shared_ptr<platform::ErrorLog> errors;
  /** 事件发布，可以为空；插件变化时发布 mappers.changed。 */
  std::shared_ptr<platform::EventSink> events;
  /** 创建卡带时脚本的执行限制。 */
  ScriptLimits limits;
  /** 电路目录：插件清单里的 circuits 按它创建主机里已有的 mapper 电路；为空时引用电路的插件无法安装。 */
  std::shared_ptr<const contracts::hardware::CircuitCatalog> circuits;
};

/** 插件登记。 */
class PluginRegistry final : public contracts::mappers::MapperService, public contracts::mappers::MapperFactory {
 public:
  /** 创建并读取预装目录与用户目录。 */
  explicit PluginRegistry(PluginRegistryOptions options);

  // MapperService
  [[nodiscard]] contracts::mappers::MappersListResult list() const override;
  [[nodiscard]] contracts::mappers::MappersInfoResult info(const std::string& id) const override;
  contracts::mappers::MappersInstallResult install(const std::string& path) override;
  void uninstall(const std::string& id) override;
  contracts::mappers::MappersSetEnabledResult setEnabled(const std::string& id, bool enabled) override;
  contracts::mappers::MappersReloadResult reload() override;

  // MapperFactory
  [[nodiscard]] contracts::mappers::MapperMatch match(const contracts::mappers::CartridgeDescription& description) const override;
  [[nodiscard]] std::unique_ptr<contracts::mappers::MapperCartridge> create(const contracts::mappers::MapperMatch& match, const contracts::mappers::CartridgeImage& image,
                                                                            contracts::hardware::IrqOutput& irq, contracts::hardware::PadSink& pads) override;

  /**
   * 安装内存里的插件包：检查全部内容（含试加载脚本），通过后放进用户目录。
   * @throws shared::CoreError mappers.invalidPackage、mappers.hostUnknown、mappers.limitExceeded。
   */
  contracts::mappers::MapperSummary installPackage(std::span<const std::uint8_t> bytes);

 private:
  struct Entry {
    std::shared_ptr<const LoadedPlugin> plugin;
    PluginSource source{PluginSource::User};
    /** 用户安装的插件包文件；只在内存里的插件为空。 */
    std::filesystem::path file;
  };

  [[nodiscard]] std::shared_ptr<const LoadedPlugin> checkPackage(std::span<const std::uint8_t> bytes) const;
  /** 检查已读出的插件内容（插件包或开发目录都走这里）。 */
  [[nodiscard]] std::shared_ptr<const LoadedPlugin> checkContents(const EmapPackage& package) const;
  void scanDirectory(const std::filesystem::path& directory, PluginSource source, std::vector<Entry>& into) const;
  /** 扫描开发目录：每个子目录是一个未打包的插件，不合格的跳过并写错误日志。 */
  void scanDevelopmentDirectory(std::vector<Entry>& into) const;
  void loadDisabled();
  void saveDisabled(const std::set<std::string>& disabled) const;
  /** 每个标识生效的条目：开发目录的优先，其余版本高的生效，版本相同时用户安装的优先，按标识排序。调用方必须持有 mutex_。 */
  [[nodiscard]] std::vector<Entry> effectiveEntriesLocked() const;
  [[nodiscard]] std::optional<Entry> findEffectiveLocked(const std::string& id) const;
  void publishChanged(const std::string* id) const;
  void logError(const std::string& code, const std::string& message, const std::string& file) const;

  PluginRegistryOptions options_;
  mutable std::mutex mutex_;
  std::vector<Entry> entries_;
  std::set<std::string> disabled_;
};

}  // namespace ember::infra::mappers
