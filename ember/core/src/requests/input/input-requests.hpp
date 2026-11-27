/*
 * ------------------------------------------------------------------------
 * 名称：input-requests.hpp
 * 说明：input 模块的请求登记：把 input.* 请求名映射到输入注入服务的方法。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/input/input-binding-service.hpp"
#include "contracts/input/input-service.hpp"
#include "platform/requests/request-router.hpp"

namespace ember::requests::input {

/** 登记 input 模块的请求：注入输入（调试用）、Family BASIC 键盘接入状态与手柄绑定。 */
void addInputRequests(platform::RequestRouter& router, std::shared_ptr<contracts::input::InputService> service, std::shared_ptr<contracts::input::InputBindingService> bindings);

}  // namespace ember::requests::input
