/*
 * ------------------------------------------------------------------------
 * 名称：cancellation.hpp
 * 说明：请求的取消标记：调用方取消后，正在处理的请求在下一次检查时停止并以 core.cancelled 结束。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：标记可以复制，副本共享同一个状态；耗时的处理应在循环或分段处调用 throwIfCancelled。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <atomic>
#include <memory>

#include "shared/errors/core-error.hpp"

namespace ember::platform {

/** 请求的取消标记。 */
class CancellationToken {
 public:
  CancellationToken() : state_(std::make_shared<std::atomic_bool>(false)) {}

  /** 标记为已取消。 */
  void cancel() const noexcept { state_->store(true); }

  /** 是否已取消。 */
  [[nodiscard]] bool isCancelled() const noexcept { return state_->load(); }

  /** 已取消时抛出 core.cancelled。 */
  void throwIfCancelled() const {
    if (isCancelled()) throw shared::CoreError(shared::ERROR_CANCELLED, "请求已取消");
  }

 private:
  std::shared_ptr<std::atomic_bool> state_;
};

}  // namespace ember::platform
