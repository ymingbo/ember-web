/*
 * ------------------------------------------------------------------------
 * 名称：preferences-requests.hpp
 * 说明：preferences 模块的请求登记：把 preferences.* 请求名映射到首选项服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/preferences/preference-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::preferences {

/** 登记 preferences 模块的请求。 */
void addPreferencesRequests(platform::RequestRouter& router, std::shared_ptr<contracts::preferences::PreferenceService> service);

}  // namespace ember::requests::preferences
