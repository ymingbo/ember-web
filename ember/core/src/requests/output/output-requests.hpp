/*
 * ------------------------------------------------------------------------
 * 名称：output-requests.hpp
 * 说明：output 模块的请求登记：把 output.* 请求名映射到输出设置服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/output/output-service.hpp"
#include "contracts/output/screenshot-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::output {

/** 登记 output 模块的请求。 */
void addOutputRequests(platform::RequestRouter& router, std::shared_ptr<contracts::output::OutputService> service, std::shared_ptr<contracts::output::ScreenshotService> screenshots);

}  // namespace ember::requests::output
