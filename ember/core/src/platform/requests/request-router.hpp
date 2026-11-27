/*
 * ------------------------------------------------------------------------
 * 名称：request-router.hpp
 * 说明：请求路由：各模块按请求名登记处理函数，核心按请求名分发；参数与结果都是已解析的 JSON。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：请求名必须以登记它的模块名开头；同名重复登记视为程序错误。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "platform/requests/cancellation.hpp"

namespace ember::platform {

/** 请求处理函数：参数不合格时抛出 SchemaError，业务错误抛出 CoreError。 */
using RequestHandler = std::function<nlohmann::json(const nlohmann::json& params, const CancellationToken& token)>;

/** 请求路由。 */
class RequestRouter {
 public:
  /**
   * 登记请求。
   * @param module 登记的模块名。
   * @param name 请求名，必须是“模块名.操作”。
   * @param handler 处理函数。
   */
  void add(std::string_view module, std::string_view name, RequestHandler handler);

  /** 请求名是否已登记。 */
  [[nodiscard]] bool has(std::string_view name) const;

  /** 分发请求；未登记时抛出 core.unknownRequest。 */
  [[nodiscard]] nlohmann::json dispatch(std::string_view name, const nlohmann::json& params, const CancellationToken& token) const;

  /** 全部已登记的请求名。 */
  [[nodiscard]] std::vector<std::string> names() const;

 private:
  std::map<std::string, RequestHandler, std::less<>> handlers_;
};

}  // namespace ember::platform
