/*
 * ------------------------------------------------------------------------
 * 名称：line-output.hpp
 * 说明：中断输出引脚的实现：占用一根中断线的一个来源，把引脚状态写到这个来源上。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/hardware/irq-line.hpp"
#include "contracts/hardware/irq-output.hpp"

namespace ember::logic::hardware {

/** 中断线上的一个固定来源，作为卡槽的 IRQ 引脚。 */
class LineIrqOutput final : public contracts::hardware::IrqOutput {
 public:
  /** @param line 中断线，必须活到本对象销毁；构造时在线上登记一个来源。 */
  explicit LineIrqOutput(contracts::hardware::IrqLine& line) : line_(line), source_(line.addSource()) {}

  void set(bool active) override { line_.set(source_, active); }

 private:
  contracts::hardware::IrqLine& line_;
  contracts::hardware::SignalSource source_;
};

}  // namespace ember::logic::hardware
