/*
 * ------------------------------------------------------------------------
 * 名称：system-clock.hpp
 * 说明：时钟的系统实现：取系统时间与单调时间。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#pragma once

#include "platform/time/clock.hpp"

namespace ember::platform {

/** 系统时钟。 */
class SystemClock final : public Clock {
 public:
  [[nodiscard]] std::chrono::system_clock::time_point now() const override { return std::chrono::system_clock::now(); }
  [[nodiscard]] std::chrono::steady_clock::time_point steadyNow() const override { return std::chrono::steady_clock::now(); }
};

}  // namespace ember::platform
