/*
 * ------------------------------------------------------------------------
 * 名称：irq-output.hpp
 * 说明：硬件契约：中断输出引脚。卡带经它向 CPU 的 IRQ 线发出中断，不接触线本身。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：引脚由主机型号在卡槽上提供，在主机的 IRQ 线上占一个固定的来源；换卡带不会增加线的来源数量。
 * ------------------------------------------------------------------------
 */

#pragma once

namespace ember::contracts::hardware {

/** 中断输出引脚。 */
class IrqOutput {
 public:
  virtual ~IrqOutput() = default;

  /**
   * 设置引脚状态。
   * @param active 为 true 表示拉低（发出中断请求），为 false 表示释放。
   */
  virtual void set(bool active) = 0;
};

}  // namespace ember::contracts::hardware
