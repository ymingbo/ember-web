/*
 * ------------------------------------------------------------------------
 * 名称：local-system-service.hpp
 * 说明：系统服务的实现：应用版本由主进程在启动核心时传入，核心版本取自构建参数。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string>

#include "contracts/system/system-service.hpp"

namespace ember::logic::system {

/** 本机的系统服务。 */
class LocalSystemService final : public contracts::system::SystemService {
 public:
  /**
   * @param appVersion 应用版本。
   * @param coreVersion 核心版本。
   */
  LocalSystemService(std::string appVersion, std::string coreVersion);

  [[nodiscard]] contracts::system::SystemVersionResult version() const override;

 private:
  std::string appVersion_;
  std::string coreVersion_;
};

}  // namespace ember::logic::system
