/*
 * ------------------------------------------------------------------------
 * 名称：joypad.hpp
 * 说明：标准手柄：8 个按键，经锁存线与时钟线串行输出：A、B、选择、开始、上、下、左、右，读完 8 位后读到 1。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：锁存线为高电平时手柄不断采样当前按键并输出 A 键；变为低电平后保持，每个时钟脉冲移出下一位，移空后补 1。
 *       按键由外部经 ButtonInput 设置。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/button-input.hpp"
#include "contracts/hardware/controller-port.hpp"

namespace ember::logic::hardware {

/** 标准手柄。 */
class Joypad final : public contracts::hardware::ControllerDevice, public contracts::hardware::ButtonInput {
 public:
  void setLatch(bool high) override {
    strobe_ = high;
    if (high) shift_ = buttons_;
  }

  std::uint8_t clockData() override {
    if (strobe_) {
      shift_ = buttons_;
      return buttons_ & 1U;
    }
    const std::uint8_t bit = shift_ & 1U;
    shift_ = static_cast<std::uint8_t>((shift_ >> 1) | 0x80U);
    return bit;
  }

  void setButtons(std::uint8_t buttons) override { buttons_ = buttons; }

 private:
  std::uint8_t buttons_{0};
  std::uint8_t shift_{0};
  bool strobe_{false};
};

}  // namespace ember::logic::hardware
