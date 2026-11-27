/*
 * ------------------------------------------------------------------------
 * 名称：pad-apu-port.hpp
 * 说明：$4016-$4017 的转接设备：手柄口与 APU 的帧计数器共用这两个地址，写入同时转给两边，读只来自手柄口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：手柄口模块（pad-2a03）与 APU 模块（apu-2a03）互不引用，只在主机装配里经本设备接在一起；
 *       读 $4016/$4017 只驱动手柄的低 5 位，帧 IRQ 标志不在这里，而是从 $4015 读取。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/bus-device.hpp"

namespace ember::bootstrap {

/** 把 $4016-$4017 的访问分给手柄口与 APU。 */
class PadApuPort final : public contracts::hardware::BusDevice {
 public:
  /**
   * @param pad 手柄口设备，读写都先交给它。
   * @param apu APU 设备，只接收 $4017 的写入（帧计数器）。
   */
  PadApuPort(contracts::hardware::BusDevice& pad, contracts::hardware::BusDevice& apu) : pad_(pad), apu_(apu) {}

  contracts::hardware::BusRead read(std::uint16_t address) override { return pad_.read(address); }

  void write(std::uint16_t address, std::uint8_t value) override {
    pad_.write(address, value);
    if (address == FRAME_COUNTER_ADDRESS) apu_.write(address, value);
  }

 private:
  /** 帧计数器的寄存器地址。 */
  static constexpr std::uint16_t FRAME_COUNTER_ADDRESS = 0x4017;

  contracts::hardware::BusDevice& pad_;
  contracts::hardware::BusDevice& apu_;
};

}  // namespace ember::bootstrap
