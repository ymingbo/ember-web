/*
 * ------------------------------------------------------------------------
 * 名称：irq-um6576.hpp
 * 说明：6576 芯片组的中断屏蔽与状态：$4032 读写屏蔽寄存器，$4033 只读状态；向 CPU 的 IRQ 线发出中断。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：位 7 定时器、位 6 鼠标、位 5 键盘、位 4 INT4、位 3 软驱、位 2 INT2、位 1 INT1；
 *       写 $4032：值为 1 的位屏蔽该来源，同时清除 $4033 里对应的待处理位；状态变为 0 时 IRQ 线回到无效；
 *       来源被屏蔽时它的请求被丢弃；复位时全部屏蔽（$FF）。读 $4033 不清除状态。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/device.hpp"
#include "contracts/hardware/interrupt-sink.hpp"
#include "contracts/hardware/irq-line.hpp"

namespace ember::logic::hardware {

/** 6576 中断控制（$4032、$4033）。 */
class IrqUm6576 final : public contracts::hardware::Device, public contracts::hardware::InterruptSink {
 public:
  /** 中断来源。 */
  static constexpr std::uint8_t SOURCE_TIMER = 0x80;
  static constexpr std::uint8_t SOURCE_MOUSE = 0x40;
  static constexpr std::uint8_t SOURCE_KEYBOARD = 0x20;
  static constexpr std::uint8_t SOURCE_INT4 = 0x10;
  static constexpr std::uint8_t SOURCE_FLOPPY = 0x08;
  static constexpr std::uint8_t SOURCE_INT2 = 0x04;
  static constexpr std::uint8_t SOURCE_INT1 = 0x02;

  /** @param irq CPU 的 IRQ 线，本对象在其上登记一个来源。 */
  explicit IrqUm6576(contracts::hardware::IrqLine& irq) : irq_(irq), source_(irq.addSource()) {}

  contracts::hardware::BusRead read(std::uint16_t address) override { return contracts::hardware::driveBus(address == ADDRESS_MASK ? mask_ : status_); }

  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }

  void write(std::uint16_t address, std::uint8_t value) override {
    if (address != ADDRESS_MASK) return;
    mask_ = value;
    status_ = static_cast<std::uint8_t>(status_ & ~value);
    if (status_ == 0) irq_.set(source_, false);
  }

  void reset(bool /*power*/) override {
    mask_ = 0xFF;
    status_ = 0;
    irq_.set(source_, false);
  }

  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

  void raise(std::uint8_t sourceMask) override {
    if ((mask_ & sourceMask) != 0) return;
    status_ = static_cast<std::uint8_t>(status_ | sourceMask);
    irq_.set(source_, true);
  }

  /** 屏蔽寄存器的值。 */
  [[nodiscard]] std::uint8_t mask() const { return mask_; }

  /** 待处理状态。 */
  [[nodiscard]] std::uint8_t status() const { return status_; }

 private:
  /** 屏蔽寄存器的地址，另一个地址（$4033）是状态。 */
  static constexpr std::uint16_t ADDRESS_MASK = 0x4032;

  contracts::hardware::IrqLine& irq_;
  contracts::hardware::SignalSource source_;
  std::uint8_t mask_{0xFF};
  std::uint8_t status_{0};
};

}  // namespace ember::logic::hardware
