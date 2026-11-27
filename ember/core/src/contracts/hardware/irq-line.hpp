/*
 * ------------------------------------------------------------------------
 * 名称：irq-line.hpp
 * 说明：硬件契约：中断线。IRQ 与 NMI 各用一根，由主机型号创建；多个来源是线或关系。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：IRQ 是电平信号，CPU 在每条指令前采样且受 I 标志屏蔽；NMI 的有效边沿（无效变有效）触发一次中断。
 *       RESET 不是线：复位直接调用 Cpu::reset。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/hardware/signal-line.hpp"

namespace ember::contracts::hardware {

/** 中断线（IRQ 或 NMI）：有效表示请求中断。 */
class IrqLine : public SignalLine {};

}  // namespace ember::contracts::hardware
