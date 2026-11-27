/*
 * ------------------------------------------------------------------------
 * 名称：debug-error.hpp
 * 说明：debug 契约：错误码与协议常量。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：错误码以模块名 debug 开头，取值与 schema/debug.schema.json 登记的一致。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace ember::contracts::debug {

/** 令牌不对，或没有先完成 hello。 */
inline constexpr std::string_view ERROR_UNAUTHORIZED = "debug.unauthorized";
/** 客户端要求的协议版本服务端不支持。 */
inline constexpr std::string_view ERROR_UNSUPPORTED_PROTOCOL = "debug.unsupportedProtocol";
/** hello、subscribe、unsubscribe 只能经调试连接调用。 */
inline constexpr std::string_view ERROR_CONNECTION_ONLY = "debug.connectionOnly";

/** 协议版本。 */
inline constexpr std::int64_t PROTOCOL_VERSION = 1;

}  // namespace ember::contracts::debug
