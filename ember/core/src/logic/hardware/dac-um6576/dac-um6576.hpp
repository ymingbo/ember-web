/*
 * ------------------------------------------------------------------------
 * 名称：dac-um6576.hpp
 * 说明：6576 芯片组的 8 位 DAC $4027：输出语音（PCM）；写入的值就是当前输出电平，经回调交给声音输出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：按写入的时刻与值输出电平（采样节拍由程序用定时器控制），
 *       不再模拟定时器门控；DAC 的直流偏置与渐入渐出的“噗”声不模拟；读不驱动数据线。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <functional>

#include "contracts/hardware/device.hpp"

namespace ember::logic::hardware {

/** 6576 的 DAC（$4027）。 */
class DacUm6576 final : public contracts::hardware::Device {
 public:
  contracts::hardware::BusRead read(std::uint16_t /*address*/) override { return contracts::hardware::releaseBus(); }

  void write(std::uint16_t /*address*/, std::uint8_t value) override {
    level_ = value;
    if (onLevel) onLevel(value);
  }

  void reset(bool /*power*/) override {
    level_ = 0;
    if (onLevel) onLevel(0);
  }

  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

  /** 输出电平变化时的回调（写入与复位时调用）。 */
  std::function<void(std::uint8_t)> onLevel;

  /** 当前输出电平（最近一次写入的值）。 */
  [[nodiscard]] std::uint8_t level() const { return level_; }

 private:
  std::uint8_t level_{0};
};

}  // namespace ember::logic::hardware
