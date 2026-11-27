/*
 * ------------------------------------------------------------------------
 * 名称：signal-line.hpp
 * 说明：硬件契约：线或信号线。多个来源接在同一根线上，任一来源有效，线就有效。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：IRQ 线与 RDY 线都是这种线，分别用 IrqLine、RdyLine 区分用途，避免接错。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::contracts::hardware {

/** 信号来源编号，由线分配。 */
using SignalSource = std::uint32_t;

/** 线或信号线。“有效”指信号发出（对应真机的低电平有效）。 */
class SignalLine {
 public:
  virtual ~SignalLine() = default;

  /** 登记一个新的来源，初始为无效。来源数量有上限，超出时报错。 */
  virtual SignalSource addSource() = 0;

  /** 设置来源的状态。 */
  virtual void set(SignalSource source, bool active) = 0;

  /** 线是否有效：任一来源有效即为 true。 */
  [[nodiscard]] virtual bool active() const = 0;
};

}  // namespace ember::contracts::hardware
