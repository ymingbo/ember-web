/*
 * ------------------------------------------------------------------------
 * 名称：timer-resolution.hpp
 * 说明：系统定时器精度：Windows 默认约 15.6 毫秒，帧节拍休眠会因此忽快忽慢；对象存活期间把精度提高到 1 毫秒。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：其他平台没有这个问题，对象什么也不做。
 * ------------------------------------------------------------------------
 */

#pragma once

namespace ember::platform {

/** 在对象存活期间提高系统定时器精度。 */
class TimerResolution {
 public:
  TimerResolution();
  ~TimerResolution();

  TimerResolution(const TimerResolution&) = delete;
  TimerResolution& operator=(const TimerResolution&) = delete;

 private:
  [[maybe_unused]] bool applied_{false};
};

}  // namespace ember::platform
