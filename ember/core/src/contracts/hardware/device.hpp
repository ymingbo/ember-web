/*
 * ------------------------------------------------------------------------
 * 名称：device.hpp
 * 说明：硬件契约：设备。挂在总线上的独立硬件模块（APU、手柄、软驱控制器等），有复位、寄存器读写与事件回调。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：设备的中断输出经构造时接入的 IrqLine 发出；状态快照在调试服务阶段补充。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/scheduler.hpp"

namespace ember::contracts::hardware {

/** 设备：总线设备加复位与定时事件。不用定时事件的设备让 onEvent 什么也不做。 */
class Device : public BusDevice, public EventListener {
 public:
  /**
   * 复位。
   * @param power 为 true 表示上电复位，为 false 表示软复位。
   */
  virtual void reset(bool power) = 0;
};

}  // namespace ember::contracts::hardware
