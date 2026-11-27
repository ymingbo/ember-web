/*
 * ------------------------------------------------------------------------
 * 名称：debug-token.cpp
 * 说明：令牌生成的实现，见 debug-token.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/debug/debug-token.hpp"

#include <random>

namespace ember::infra::debug {

std::string generateToken() {
  static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  std::random_device source;
  std::uniform_int_distribution<std::size_t> pick(0, sizeof(ALPHABET) - 2);
  std::string token;
  token.reserve(TOKEN_LENGTH);
  for (std::size_t i = 0; i < TOKEN_LENGTH; ++i) token.push_back(ALPHABET[pick(source)]);
  return token;
}

}  // namespace ember::infra::debug
