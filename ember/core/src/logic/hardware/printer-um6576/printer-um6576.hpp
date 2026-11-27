/*
 * ------------------------------------------------------------------------
 * 名称：printer-um6576.hpp
 * 说明：6576 芯片组的并口数据寄存器 $4025：写入的字节送到并口的数据线；并口的控制线与状态线在手柄口与扩展 I/O 的寄存器里。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：“写 $4025 就把字节交给打印机”（芯片自动产生选通），所以这里把数据送到并口就算打印机收下了这个字节；
 *       $4026 位 2 的 #Strobe 电平只是送给并口的控制线；读不驱动数据线。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/device.hpp"
#include "contracts/hardware/parallel-port.hpp"

namespace ember::logic::hardware {

/** 6576 并口数据寄存器（$4025）。 */
class PrinterUm6576 final : public contracts::hardware::Device {
 public:
  /** @param port 并口，数据线接到它。 */
  explicit PrinterUm6576(contracts::hardware::ParallelPort& port) : port_(port) {}

  contracts::hardware::BusRead read(std::uint16_t /*address*/) override { return contracts::hardware::releaseBus(); }

  void write(std::uint16_t /*address*/, std::uint8_t value) override {
    data_ = value;
    port_.writeData(value);
  }

  void reset(bool /*power*/) override { data_ = 0; }

  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

  /** 最近写入的数据。 */
  [[nodiscard]] std::uint8_t data() const { return data_; }

 private:
  contracts::hardware::ParallelPort& port_;
  std::uint8_t data_{0};
};

}  // namespace ember::logic::hardware
