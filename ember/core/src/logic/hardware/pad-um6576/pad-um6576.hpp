/*
 * ------------------------------------------------------------------------
 * 名称：pad-um6576.hpp
 * 说明：6576 芯片组的手柄口 $4016、$4017，以及它们上面接的并口控制线与状态线。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：读 $4016：位 0 是手柄口 1 的数据；读 $4017：位 0 是手柄口 2 的数据，位 2 是并口 Error，位 3 是并口 #Busy，其余位不驱动（保持总线上的值）；
 *       写 $4016：位 0 同时是两个手柄口的锁存线、并口 #Select Printer，位 1 是并口 #Linefeed，位 2 是并口 Reset，每次写入都送一遍；
 *       真机实测 $4016 读出 $44、$4017 读出 $40，高位来自总线上的值；没有接打印机时 #Busy 与 Error 两根线为低电平。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>

#include "contracts/hardware/controller-port.hpp"
#include "contracts/hardware/device.hpp"
#include "contracts/hardware/parallel-port.hpp"

namespace ember::logic::hardware {

/** 6576 的手柄口：主机上的插座，设备插在上面。 */
class PadSocketUm6576 final : public contracts::hardware::ControllerPort {
 public:
  void plug(contracts::hardware::ControllerDevice* device) override { device_ = device; }

  void setLatch(bool high) override {
    if (device_ != nullptr) device_->setLatch(high);
  }

  std::uint8_t clockData() override { return device_ != nullptr ? device_->clockData() : std::uint8_t{0}; }

 private:
  contracts::hardware::ControllerDevice* device_{nullptr};
};

/** 6576 的 $4016、$4017。 */
class PadUm6576 final : public contracts::hardware::Device {
 public:
  /** 手柄口数量。 */
  static constexpr unsigned PORT_COUNT = 2;

  /** @param parallel 并口，控制线送给它，状态线从它读。 */
  explicit PadUm6576(contracts::hardware::ParallelPort& parallel) : parallel_(parallel) {}

  /** 取手柄口；编号超出时报错。 */
  contracts::hardware::ControllerPort& port(unsigned index);

  contracts::hardware::BusRead read(std::uint16_t address) override;
  void write(std::uint16_t address, std::uint8_t value) override;
  void reset(bool power) override;
  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

 private:
  contracts::hardware::ParallelPort& parallel_;
  std::array<PadSocketUm6576, PORT_COUNT> ports_;
};

}  // namespace ember::logic::hardware
