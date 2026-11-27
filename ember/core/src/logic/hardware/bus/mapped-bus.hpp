/*
 * ------------------------------------------------------------------------
 * 名称：mapped-bus.hpp
 * 说明：CPU 总线与 PPU 总线的实现：把契约接口接到共用的地址译码上，地址宽度由契约规定。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/cpu-bus.hpp"
#include "contracts/hardware/ppu-bus.hpp"
#include "logic/hardware/bus/bus-map.hpp"

namespace ember::logic::hardware {

/**
 * 按地址宽度分发读写的总线。
 * @tparam Interface 实现的总线契约（CpuBus 或 PpuBus）。
 * @tparam AddressBits 地址宽度（位）。
 */
template <typename Interface, unsigned AddressBits>
class MappedBus final : public Interface {
 public:
  MappedBus() : map_(AddressBits) {}

  std::uint8_t read(std::uint16_t address) override { return map_.read(address); }

  void write(std::uint16_t address, std::uint8_t value) override { map_.write(address, value); }

  /** 调试用的读：无副作用，读不到内容的地址为 0。 */
  [[nodiscard]] std::uint8_t peek(std::uint16_t address) const { return map_.peek(address); }

  /** 安装或去掉访问探针（空指针）。 */
  void setProbe(contracts::hardware::AccessProbe* probe) { map_.setProbe(probe); }

  void mount(std::uint16_t first, std::uint16_t last, contracts::hardware::BusDevice& device) override { map_.mount(first, last, device); }

 private:
  BusMap map_;
};

/** CPU 总线：16 位地址。 */
using CpuAddressBus = MappedBus<contracts::hardware::CpuBus, contracts::hardware::CPU_BUS_ADDRESS_BITS>;

/** PPU 总线：14 位地址。 */
using PpuAddressBus = MappedBus<contracts::hardware::PpuBus, contracts::hardware::PPU_BUS_ADDRESS_BITS>;

}  // namespace ember::logic::hardware
