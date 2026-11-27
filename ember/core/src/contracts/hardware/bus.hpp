/*
 * ------------------------------------------------------------------------
 * 名称：bus.hpp
 * 说明：硬件契约：总线的公共部分。总线把地址范围挂给总线设备，并按地址分发读写。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：CPU 总线与 PPU 总线只是地址宽度不同，分别见 cpu-bus.hpp 与 ppu-bus.hpp。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/bus-device.hpp"

namespace ember::contracts::hardware {

/** 总线。 */
class Bus {
 public:
  virtual ~Bus() = default;

  /**
   * 读。超出地址宽度的高位被忽略；没有设备挂载的地址、以及设备没有驱动的数据位，读到总线上最近一次的值。
   * @param address 地址。
   */
  virtual std::uint8_t read(std::uint16_t address) = 0;

  /**
   * 写。超出地址宽度的高位被忽略；没有设备挂载的地址，写入被丢弃，数据仍出现在总线上。
   * @param address 地址。
   * @param value 数据。
   */
  virtual void write(std::uint16_t address, std::uint8_t value) = 0;

  /**
   * 把地址范围挂给设备。范围超出地址宽度、起点大于终点或与已挂载的范围重叠时报错，不改变已有的挂载。
   * @param first 范围的第一个地址。
   * @param last 范围的最后一个地址（含）。
   * @param device 设备，必须活到总线销毁。
   */
  virtual void mount(std::uint16_t first, std::uint16_t last, BusDevice& device) = 0;
};

}  // namespace ember::contracts::hardware
