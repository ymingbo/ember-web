/*
 * ------------------------------------------------------------------------
 * 名称：request-router.cpp
 * 说明：请求路由的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#include "platform/requests/request-router.hpp"

#include <stdexcept>

#include "shared/errors/core-error.hpp"

namespace ember::platform {

void RequestRouter::add(std::string_view module, std::string_view name, RequestHandler handler) {
  const std::string prefix = std::string(module) + ".";
  if (name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix) {
    throw std::logic_error("请求名必须以模块名开头：" + std::string(name));
  }
  if (!handler) throw std::logic_error("请求没有处理函数：" + std::string(name));
  if (!handlers_.emplace(std::string(name), std::move(handler)).second) throw std::logic_error("请求重复登记：" + std::string(name));
}

bool RequestRouter::has(std::string_view name) const { return handlers_.find(name) != handlers_.end(); }

nlohmann::json RequestRouter::dispatch(std::string_view name, const nlohmann::json& params, const CancellationToken& token) const {
  const auto it = handlers_.find(name);
  if (it == handlers_.end()) throw shared::CoreError(shared::ERROR_UNKNOWN_REQUEST, "没有这个请求：" + std::string(name));
  return it->second(params, token);
}

std::vector<std::string> RequestRouter::names() const {
  std::vector<std::string> result;
  result.reserve(handlers_.size());
  for (const auto& [name, handler] : handlers_) {
    static_cast<void>(handler);
    result.push_back(name);
  }
  return result;
}

}  // namespace ember::platform
