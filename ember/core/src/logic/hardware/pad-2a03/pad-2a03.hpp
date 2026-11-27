/*
 * ------------------------------------------------------------------------
 * 名称：pad-2a03.hpp
 * 说明：2A03 片上的手柄口：$4016、$4017 的读写，两个手柄口（ControllerPort），写 $4016 时把锁存线同时送给两个口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：读 $4016 取手柄口 1 的数据，读 $4017 取手柄口 2 的数据，只驱动低 5 位，高 3 位保持总线上的值；
 *       写 $4016 的位 0 是锁存线，写 $4017 是 APU 的帧计数器，不属于本模块（装配时由转接设备处理）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/controller-port.hpp"

namespace ember::logic::hardware {

/** 手柄口：主机上的插座，设备插在上面。 */
class ControllerSocket final : public contracts::hardware::ControllerPort {
 public:
  void plug(contracts::hardware::ControllerDevice* device) override { device_ = device; }

  void setLatch(bool high) override {
    if (device_ != nullptr) device_->setLatch(high);
  }

  std::uint8_t clockData() override { return device_ != nullptr ? device_->clockData() : std::uint8_t{0}; }

 private:
  contracts::hardware::ControllerDevice* device_{nullptr};
};

/** $4016、$4017 的手柄接口。 */
class Pad2a03 final : public contracts::hardware::BusDevice {
 public:
  /** 手柄口数量。 */
  static constexpr unsigned PORT_COUNT = 2;

  /**
   * 取手柄口。
   * @param index 手柄口编号，0 或 1；其他值报错。
   */
  contracts::hardware::ControllerPort& port(unsigned index);

  contracts::hardware::BusRead read(std::uint16_t address) override;

  void write(std::uint16_t address, std::uint8_t value) override;

 private:
  std::array<ControllerSocket, PORT_COUNT> ports_;
};

}  // namespace ember::logic::hardware
