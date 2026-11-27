/*
 * ------------------------------------------------------------------------
 * 名称：serial-device.hpp
 * 说明：硬件契约：串行设备（键盘、鼠标）与它的接收方。设备把字节交给接收方，主机可以向设备发字节与控制信号。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：设备自己用调度器安排字节发出的时间；接收方通常是主机上的键盘口或鼠标口寄存器模块，字节到达时锁存并发出中断。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::contracts::hardware {

/** 字节的接收方。 */
class ByteReceiver {
 public:
  virtual ~ByteReceiver() = default;

  /** 设备送来一个字节。 */
  virtual void receiveByte(std::uint8_t value) = 0;
};

/** 串行设备：插在键盘口或鼠标口上。 */
class SerialDevice {
 public:
  virtual ~SerialDevice() = default;

  /** 接入接收方；设备发出的字节都交给它，传空指针表示断开（之后发出的字节被丢弃）。接收方必须活到被断开。 */
  virtual void connect(ByteReceiver* receiver) = 0;

  /** 主机向设备发一个字节（键盘命令）。 */
  virtual void sendByte(std::uint8_t value) = 0;

  /** 主机在控制线上发出一次脉冲（鼠标的 RTS 复位）。键盘忽略它。 */
  virtual void pulseControl() = 0;

  /** 复位：丢弃进行中的传送，回到初始状态；power 为 true 时同时清掉按键与位移。 */
  virtual void reset(bool power) = 0;
};

}  // namespace ember::contracts::hardware
