/*
 * ------------------------------------------------------------------------
 * 名称：rtc-um6576.hpp
 * 说明：6576 芯片组的实时时钟接口：$4080 写地址，$4081 读写数据。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：SB2000 的 BIOS 不访问它；真机实测 $4081 不论有没有设地址，读出的都是固定的 $60，写入没有效果，
 *       所以这里只记下地址与写入的值（供调试读取），读固定返回 $60。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/device.hpp"

namespace ember::logic::hardware {

/** 6576 实时时钟接口（$4080、$4081）。 */
class RtcUm6576 final : public contracts::hardware::Device {
 public:
  /** 真机实测的固定读数。 */
  static constexpr std::uint8_t READ_VALUE = 0x60;

  contracts::hardware::BusRead read(std::uint16_t address) override {
    if (address == ADDRESS_DATA) return contracts::hardware::driveBus(READ_VALUE);
    return contracts::hardware::releaseBus();
  }

  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }

  void write(std::uint16_t address, std::uint8_t value) override {
    if (address == ADDRESS_INDEX) {
      index_ = value;
    } else {
      data_ = value;
    }
  }

  void reset(bool /*power*/) override {
    index_ = 0;
    data_ = 0;
  }

  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

  /** 最近写入的地址。 */
  [[nodiscard]] std::uint8_t index() const { return index_; }

  /** 最近写入的数据。 */
  [[nodiscard]] std::uint8_t data() const { return data_; }

 private:
  static constexpr std::uint16_t ADDRESS_INDEX = 0x4080;
  static constexpr std::uint16_t ADDRESS_DATA = 0x4081;

  std::uint8_t index_{0};
  std::uint8_t data_{0};
};

}  // namespace ember::logic::hardware
