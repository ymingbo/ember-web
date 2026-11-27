/*
 * ------------------------------------------------------------------------
 * 名称：system-service.hpp
 * 说明：系统模块的服务接口：应用与核心的版本查询。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：数据类型由 schema/system.schema.json 生成到 generated/。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/system/generated/system.hpp"

namespace ember::contracts::system {

/** 系统服务。 */
class SystemService {
 public:
  virtual ~SystemService() = default;

  /** 返回应用版本与核心版本。 */
  [[nodiscard]] virtual SystemVersionResult version() const = 0;
};

}  // namespace ember::contracts::system
