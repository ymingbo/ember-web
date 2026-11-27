/*
 * ------------------------------------------------------------------------
 * 名称：mouse-um6576.hpp
 * 说明：6576 芯片组的串口式鼠标接口：$4023 选择手柄 2 或鼠标（位 0），$4024 读鼠标送来的串行数据（取反后的字节）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：真机实测（hwprobe）：$4023 读回 $80 加位 0（位 7 是输入线，闲置为 1，位 6-1 读 0）；
 *       鼠标送来字节时，只有选择了鼠标（位 0 为 1）才锁存进 $4024（取反，因为 /RXD 反相）并发出鼠标中断（$4033 位 6），否则字节丢失；
 *       鼠标的复位（RTS）经 $4026 的位 1，由扩展 I/O 模块的回调转给鼠标设备，不在本模块里。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/device.hpp"
#include "contracts/hardware/interrupt-sink.hpp"
#include "contracts/hardware/serial-device.hpp"

namespace ember::logic::hardware {

/** 6576 鼠标接口（$4023、$4024）。 */
class MouseUm6576 final : public contracts::hardware::Device, public contracts::hardware::ByteReceiver {
 public:
  /**
   * @param mouse 鼠标设备，本对象作为它的接收方。
   * @param sink 中断汇集器。
   */
  MouseUm6576(contracts::hardware::SerialDevice& mouse, contracts::hardware::InterruptSink& sink) : mouse_(mouse), sink_(sink) { mouse_.connect(this); }

  ~MouseUm6576() override { mouse_.connect(nullptr); }

  contracts::hardware::BusRead read(std::uint16_t address) override {
    if (address == ADDRESS_SELECT) return contracts::hardware::driveBus(static_cast<std::uint8_t>(0x80 | (select_ ? 1 : 0)));
    return contracts::hardware::driveBus(data_);
  }

  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }

  void write(std::uint16_t address, std::uint8_t value) override {
    if (address == ADDRESS_SELECT) select_ = (value & 1) != 0;
  }

  void reset(bool /*power*/) override {
    select_ = false;
    data_ = 0;
  }

  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

  void receiveByte(std::uint8_t value) override {
    if (!select_) return;
    data_ = static_cast<std::uint8_t>(~value);
    sink_.raise(INTERRUPT_SOURCE);
  }

  /** 是否选择了鼠标（$4023 位 0）。 */
  [[nodiscard]] bool mouseSelected() const { return select_; }

 private:
  static constexpr std::uint16_t ADDRESS_SELECT = 0x4023;
  /** 中断汇集器里鼠标的来源位（$4033 位 6）。 */
  static constexpr std::uint8_t INTERRUPT_SOURCE = 0x40;

  contracts::hardware::SerialDevice& mouse_;
  contracts::hardware::InterruptSink& sink_;
  bool select_{false};
  std::uint8_t data_{0};
};

}  // namespace ember::logic::hardware
