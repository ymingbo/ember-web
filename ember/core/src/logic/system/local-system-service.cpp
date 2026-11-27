/*
 * ------------------------------------------------------------------------
 * 名称：local-system-service.cpp
 * 说明：系统服务的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#include "logic/system/local-system-service.hpp"

namespace ember::logic::system {

LocalSystemService::LocalSystemService(std::string appVersion, std::string coreVersion)
    : appVersion_(std::move(appVersion)), coreVersion_(std::move(coreVersion)) {}

contracts::system::SystemVersionResult LocalSystemService::version() const {
  contracts::system::SystemVersionResult result;
  result.appVersion = appVersion_;
  result.coreVersion = coreVersion_;
  return result;
}

}  // namespace ember::logic::system
