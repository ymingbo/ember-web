/*
 * ------------------------------------------------------------------------
 * 名称：debug-token.hpp
 * 说明：生成调试服务的令牌：用系统的随机数源生成固定长度的字母数字串。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string>

namespace ember::infra::debug {

/** 令牌的长度。 */
inline constexpr std::size_t TOKEN_LENGTH = 32;

/** 生成一个新令牌：32 个字母与数字，约 190 位随机性。 */
[[nodiscard]] std::string generateToken();

}  // namespace ember::infra::debug
