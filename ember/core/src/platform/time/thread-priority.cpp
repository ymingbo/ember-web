/*
 * ------------------------------------------------------------------------
 * 名称：thread-priority.cpp
 * 说明：线程优先级的实现：Windows 用 SetThreadPriority 提到“最高”（不用“时间关键”，避免饿死系统与界面线程），其他平台不做事。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * ------------------------------------------------------------------------
 */

#include "platform/time/thread-priority.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ember::platform {

#ifdef _WIN32

ThreadPriorityBoost::ThreadPriorityBoost() : previous_(GetThreadPriority(GetCurrentThread())) {
  applied_ = previous_ != THREAD_PRIORITY_ERROR_RETURN && SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0;
}

ThreadPriorityBoost::~ThreadPriorityBoost() {
  if (applied_) SetThreadPriority(GetCurrentThread(), previous_);
}

#else

ThreadPriorityBoost::ThreadPriorityBoost() = default;

ThreadPriorityBoost::~ThreadPriorityBoost() = default;

#endif

}  // namespace ember::platform
