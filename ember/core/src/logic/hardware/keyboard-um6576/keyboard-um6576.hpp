/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-um6576.hpp
 * 说明：6576 芯片组的键盘接口：$4020 读键盘数据（写为时序设置字节），$4021 写发往键盘的数据，$4022 控制（位 7 时钟、位 2 允许发送完成中断、位 0 方向）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：键盘送来的字节锁存进 $4020 并发出键盘中断（$4033 位 5）；
 *       写 $4022 时若时钟位出现上升沿且方向位为 1，就把 $4021 的字节发给键盘，位 2 为 1 时约 1 毫秒后再发一次键盘中断（发送完成）；
 *       $4020 的写入值只是“时序设置字节”，由 DMA 模块读它的位 7（经 timingControl 取得），不影响键盘；
 *       芯片手册里的 AT 键盘 11 位串行帧不模拟：设备直接以字节为单位收发。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/device.hpp"
#include "contracts/hardware/interrupt-sink.hpp"
#include "contracts/hardware/serial-device.hpp"

namespace ember::logic::hardware {

/** 6576 键盘接口（$4020-$4022）。 */
class KeyboardUm6576 final : public contracts::hardware::Device, public contracts::hardware::ByteReceiver {
 public:
  /**
   * @param keyboard 键盘设备，本对象作为它的接收方。
   * @param sink 中断汇集器。
   * @param scheduler 调度器，发送完成中断靠它的事件。
   * @param sendDelayTicks 位 2 为 1 时，从发送到发出发送完成中断经过的主时钟周期数（约 1 毫秒）。
   */
  KeyboardUm6576(contracts::hardware::SerialDevice& keyboard, contracts::hardware::InterruptSink& sink, contracts::hardware::Scheduler& scheduler, contracts::hardware::MasterTime sendDelayTicks);

  ~KeyboardUm6576() override;

  contracts::hardware::BusRead read(std::uint16_t address) override;
  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }
  void write(std::uint16_t address, std::uint8_t value) override;
  void reset(bool power) override;
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;
  void receiveByte(std::uint8_t value) override;

  /** $4020 最近一次写入的时序设置字节；位 7 为 1 时 DMA 的目的选择位才起作用。 */
  [[nodiscard]] std::uint8_t timingControl() const { return timingControl_; }

  /** $4022 的值。 */
  [[nodiscard]] std::uint8_t control() const { return control_; }

 private:
  contracts::hardware::SerialDevice& keyboard_;
  contracts::hardware::InterruptSink& sink_;
  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::MasterTime sendDelayTicks_;
  /** $4020 读出的值：键盘最近送来的字节。 */
  std::uint8_t input_{0xFF};
  /** $4021 写入的值：发往键盘的字节。 */
  std::uint8_t output_{0};
  std::uint8_t control_{0};
  std::uint8_t timingControl_{0};
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};
};

}  // namespace ember::logic::hardware
