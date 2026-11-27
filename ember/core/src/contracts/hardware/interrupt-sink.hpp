/*
 * ------------------------------------------------------------------------
 * 名称：interrupt-sink.hpp
 * 说明：硬件契约：中断汇集器。多个功能模块（定时器、软驱、键盘、鼠标）把各自的中断请求交给它，由它决定是否屏蔽、是否向 CPU 的 IRQ 线发出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：中断源用位掩码表示，同一时刻可以有多个来源；屏蔽与待处理状态属于汇集器自己，来源只负责“发出请求”。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::contracts::hardware {

/** 中断汇集器：功能模块经它向 CPU 发出中断请求。 */
class InterruptSink {
 public:
  virtual ~InterruptSink() = default;

  /**
   * 发出一次中断请求。来源被屏蔽时请求被丢弃，不会保留。
   * @param sourceMask 来源的位掩码（只有一位有效）。
   */
  virtual void raise(std::uint8_t sourceMask) = 0;
};

}  // namespace ember::contracts::hardware
