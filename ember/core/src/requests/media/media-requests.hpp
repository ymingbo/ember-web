/*
 * ------------------------------------------------------------------------
 * 名称：media-requests.hpp
 * 说明：media 模块的请求登记：把 media.* 请求名映射到介质服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/media/media-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::media {

/** 登记 media 模块的请求。 */
void addMediaRequests(platform::RequestRouter& router, std::shared_ptr<contracts::media::MediaService> service);

}  // namespace ember::requests::media
