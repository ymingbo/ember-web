/*
 * ------------------------------------------------------------------------
 * 名称：timer-resolution.cpp
 * 说明：系统定时器精度的实现：Windows 用 timeBeginPeriod / timeEndPeriod，其他平台不做事。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "platform/time/timer-resolution.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")
#endif

namespace ember::platform {

namespace {

/** 要求的定时器精度（毫秒）。 */
constexpr unsigned PERIOD_MS = 1;

}  // namespace

#ifdef _WIN32

TimerResolution::TimerResolution() : applied_(timeBeginPeriod(PERIOD_MS) == TIMERR_NOERROR) {}

TimerResolution::~TimerResolution() {
  if (applied_) timeEndPeriod(PERIOD_MS);
}

#else

TimerResolution::TimerResolution() = default;

TimerResolution::~TimerResolution() = default;

#endif

}  // namespace ember::platform
