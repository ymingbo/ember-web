/*
 * ------------------------------------------------------------------------
 * 名称：thread-priority.hpp
 * 说明：线程优先级：模拟线程按固定节拍产出声音，被其他线程抢占而晚了几十毫秒，声音缓冲就会被抽空而爆音；对象存活期间提高当前线程的优先级。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：只有 Windows 实现，其他平台对象什么也不做；对象必须在被提高优先级的线程里创建与销毁。
 * ------------------------------------------------------------------------
 */

#pragma once

namespace ember::platform {

/** 在对象存活期间提高当前线程的优先级。 */
class ThreadPriorityBoost {
 public:
  ThreadPriorityBoost();
  ~ThreadPriorityBoost();

  ThreadPriorityBoost(const ThreadPriorityBoost&) = delete;
  ThreadPriorityBoost& operator=(const ThreadPriorityBoost&) = delete;

 private:
  [[maybe_unused]] int previous_{0};
  [[maybe_unused]] bool applied_{false};
};

}  // namespace ember::platform
