/*
 * ------------------------------------------------------------------------
 * 名称：thread-pool.hpp
 * 说明：固定线程数的任务池：核心在这里执行请求，不占用 Node 的主线程。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：stop 后不再接收新任务，等待已排队的任务执行完毕；任务内的异常由调用方自行捕获。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace ember::platform {

/** 任务池。 */
class ThreadPool {
 public:
  /** @param threadCount 线程数，为 0 时取硬件并发数（至少 2）。 */
  explicit ThreadPool(std::size_t threadCount);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  /** 加入任务；已停止时返回 false。 */
  bool post(std::function<void()> task);

  /** 停止并等待全部线程结束。 */
  void stop();

 private:
  void run();

  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<std::function<void()>> tasks_;
  std::vector<std::thread> threads_;
  bool stopping_ = false;
};

}  // namespace ember::platform
