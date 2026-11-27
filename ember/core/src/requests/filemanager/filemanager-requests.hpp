/*
 * ------------------------------------------------------------------------
 * 名称：filemanager-requests.hpp
 * 说明：filemanager 模块的请求登记：把 filemanager.* 请求名映射到媒体库服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/filemanager/filemanager-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::filemanager {

/** 登记 filemanager 模块的请求。 */
void addFileManagerRequests(platform::RequestRouter& router, std::shared_ptr<contracts::filemanager::FileManagerService> service);

}  // namespace ember::requests::filemanager
