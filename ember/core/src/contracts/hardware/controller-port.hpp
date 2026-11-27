/*
 * ------------------------------------------------------------------------
 * 名称：controller-port.hpp
 * 说明：硬件契约：手柄口与插在上面的设备。信号线有锁存线、时钟线与数据线。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：端口是主机上的插座，不知道设备的种类；步步高的鼠标等也是插在手柄口上的设备。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::contracts::hardware {

/** 插在手柄口上的设备（标准手柄等）。 */
class ControllerDevice {
 public:
  virtual ~ControllerDevice() = default;

  /** 锁存线电平变化：高电平期间设备不断采样按键，变为低电平后保持。 */
  virtual void setLatch(bool high) = 0;

  /**
   * 时钟线发出一次脉冲：设备移出下一位。
   * @return 数据线的状态：位 0 为 D0，位 1 为 D1，依次类推；设备不驱动的数据线读 0。
   */
  virtual std::uint8_t clockData() = 0;
};

/** 手柄口。 */
class ControllerPort {
 public:
  virtual ~ControllerPort() = default;

  /** 插入设备，原来的设备被拔下；传空指针表示拔下。设备必须活到被拔下。 */
  virtual void plug(ControllerDevice* device) = 0;

  /** 设置锁存线，转给插着的设备；没有设备时什么也不做。 */
  virtual void setLatch(bool high) = 0;

  /** 发出一次时钟脉冲并返回数据线；没有设备时返回 0。 */
  virtual std::uint8_t clockData() = 0;
};

}  // namespace ember::contracts::hardware
