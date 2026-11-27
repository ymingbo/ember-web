/*
 * ------------------------------------------------------------------------
 * 名称：sysinit-um6576.hpp
 * 说明：6576 芯片组的初始设置寄存器 $4031：依次写入 $65、$76 之后进入 6576 工作模式。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：两次写入要在两个指令周期内连续完成，这里只按顺序识别，不检查间隔；
 *       中间写入别的值会回到未进入的状态；上电复位回到未进入，软复位保持。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/device.hpp"

namespace ember::logic::hardware {

/** 6576 初始设置（挂在 $4031）。 */
class SysinitUm6576 final : public contracts::hardware::Device {
 public:
  /** 第一次写入需要的值。 */
  static constexpr std::uint8_t FIRST_KEY = 0x65;
  /** 第二次写入需要的值。 */
  static constexpr std::uint8_t SECOND_KEY = 0x76;

  /** 读不驱动数据线。 */
  contracts::hardware::BusRead read(std::uint16_t /*address*/) override { return contracts::hardware::releaseBus(); }

  void write(std::uint16_t /*address*/, std::uint8_t value) override {
    if (state_ == 0) {
      if (value == FIRST_KEY) state_ = 1;
    } else if (state_ == 1) {
      state_ = (value == SECOND_KEY) ? 2 : 0;
    }
  }

  void reset(bool power) override {
    if (power) state_ = 0;
  }

  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

  /** 是否已进入 6576 模式。 */
  [[nodiscard]] bool enabled() const { return state_ == 2; }

  /** 识别进度：0 没有开始，1 已收到第一个值，2 已进入 6576 模式。 */
  [[nodiscard]] unsigned state() const { return state_; }

 private:
  unsigned state_{0};
};

}  // namespace ember::logic::hardware
