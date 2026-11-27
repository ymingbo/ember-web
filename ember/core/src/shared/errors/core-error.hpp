/*
 * ------------------------------------------------------------------------
 * 名称：core-error.hpp
 * 说明：核心的错误类型：带稳定错误码的异常，以及与 TypeScript 一侧一致的通用错误码。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：各模块的错误码在模块的 Schema 中登记，以模块名开头；这里只放通用错误码（core.*），
 *       取值与 src/contracts/core-gateway/core-messages.ts 的 CORE_ERROR_CODES 一致。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace ember::shared {

/** 参数不符合 Schema。 */
inline constexpr std::string_view ERROR_INVALID_PARAMS = "core.invalidParams";
/** 请求名没有登记。 */
inline constexpr std::string_view ERROR_UNKNOWN_REQUEST = "core.unknownRequest";
/** 请求已取消。 */
inline constexpr std::string_view ERROR_CANCELLED = "core.cancelled";
/** 核心尚未启动或已停止。 */
inline constexpr std::string_view ERROR_UNAVAILABLE = "core.unavailable";
/** 意外的内部错误。 */
inline constexpr std::string_view ERROR_INTERNAL = "core.internal";

/** 带错误码的异常；说明面向用户。 */
class CoreError : public std::runtime_error {
 public:
  CoreError(std::string_view code, const std::string& message) : std::runtime_error(message), code_(code) {}

  /** 错误码。 */
  [[nodiscard]] const std::string& code() const noexcept { return code_; }

 private:
  std::string code_;
};

/** 数据不符合 Schema。 */
class SchemaError : public CoreError {
 public:
  explicit SchemaError(const std::string& message) : CoreError(ERROR_INVALID_PARAMS, message) {}
};

}  // namespace ember::shared
