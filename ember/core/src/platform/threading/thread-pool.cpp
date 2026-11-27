/*
 * ------------------------------------------------------------------------
 * 名称：thread-pool.cpp
 * 说明：任务池的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#include "platform/threading/thread-pool.hpp"

#include <algorithm>

namespace ember::platform {

ThreadPool::ThreadPool(std::size_t threadCount) {
  const std::size_t count = threadCount > 0 ? threadCount : std::max<std::size_t>(2, std::thread::hardware_concurrency());
  threads_.reserve(count);
  for (std::size_t index = 0; index < count; ++index) threads_.emplace_back([this] { run(); });
}

ThreadPool::~ThreadPool() { stop(); }

bool ThreadPool::post(std::function<void()> task) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return false;
    tasks_.push_back(std::move(task));
  }
  ready_.notify_one();
  return true;
}

void ThreadPool::stop() {
  {
    std::lock_guard lock(mutex_);
    if (stopping_ && threads_.empty()) return;
    stopping_ = true;
  }
  ready_.notify_all();
  for (auto& thread : threads_) {
    if (thread.joinable()) thread.join();
  }
  threads_.clear();
}

void ThreadPool::run() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
      if (tasks_.empty()) return;
      task = std::move(tasks_.front());
      tasks_.pop_front();
    }
    task();
  }
}

}  // namespace ember::platform
