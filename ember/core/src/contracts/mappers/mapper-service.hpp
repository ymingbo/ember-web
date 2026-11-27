/*
 * ------------------------------------------------------------------------
 * 名称：mapper-service.hpp
 * 说明：mappers 模块的服务接口：插件的列表、详情、安装、卸载、启停与重新加载。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：数据类型由 schema/mappers.schema.json 生成到 generated/；请求层只经本接口使用插件管理。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string>

#include "contracts/mappers/generated/mappers.hpp"

namespace ember::contracts::mappers {

/** 首选项里“mapper 插件”分组与开发目录设置项的标识。 */
inline constexpr const char* SETTING_GROUP = "mappers";
inline constexpr const char* SETTING_DEVELOPMENT_DIRECTORY = "mappers.developmentDirectory";

/** 插件管理。 */
class MapperService {
 public:
  virtual ~MapperService() = default;

  /** 列出生效的插件。 */
  [[nodiscard]] virtual MappersListResult list() const = 0;

  /** 一个插件的详情。 @throws shared::CoreError mappers.notFound。 */
  [[nodiscard]] virtual MappersInfoResult info(const std::string& id) const = 0;

  /**
   * 安装 .emap 插件包。
   * @param path 包文件的路径。
   * @throws shared::CoreError 包不合格时的 mappers.invalidPackage、mappers.hostUnknown、mappers.limitExceeded。
   */
  virtual MappersInstallResult install(const std::string& path) = 0;

  /** 卸载用户安装的插件。 @throws shared::CoreError mappers.notFound、mappers.bundledReadonly。 */
  virtual void uninstall(const std::string& id) = 0;

  /** 启用或停用插件。 @throws shared::CoreError mappers.notFound。 */
  virtual MappersSetEnabledResult setEnabled(const std::string& id, bool enabled) = 0;

  /** 重新读取预装目录、用户目录与开发目录。 */
  virtual MappersReloadResult reload() = 0;
};

}  // namespace ember::contracts::mappers
