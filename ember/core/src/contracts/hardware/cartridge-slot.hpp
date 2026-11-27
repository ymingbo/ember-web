/*
 * ------------------------------------------------------------------------
 * 名称：cartridge-slot.hpp
 * 说明：硬件契约：卡槽。主机上的卡带插座；总线上属于卡带的地址都转交给卡槽，由卡槽转给插着的卡带。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：没有卡带时，卡槽对读写的响应与空槽一致：不驱动数据线（开放总线），写入被丢弃。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/cartridge.hpp"

namespace ember::contracts::hardware {

/** 卡槽。 */
class CartridgeSlot {
 public:
  virtual ~CartridgeSlot() = default;

  /** 插入卡带，已有的卡带被拔下。卡带必须活到被拔下。 */
  virtual void insert(Cartridge& cartridge) = 0;

  /** 拔下卡带；没有卡带时什么也不做。 */
  virtual void eject() = 0;

  /** 是否插着卡带。 */
  [[nodiscard]] virtual bool occupied() const = 0;

  /** CPU 侧的总线设备，由主机型号挂到 CPU 总线属于卡带的地址范围上。 */
  virtual BusDevice& cpuSide() = 0;

  /** PPU 侧的总线设备，由主机型号挂到 PPU 总线的图案表地址范围上。 */
  virtual BusDevice& ppuSide() = 0;

  /** 显存控制线；空槽时选择主板显存，A10 为低。 */
  [[nodiscard]] virtual CiramControl ciramControl(std::uint16_t ppuAddress) const = 0;

  /**
   * 复位：转给插着的卡带，没有卡带时什么也不做。
   * @param power 为 true 表示上电复位。
   */
  virtual void reset(bool power) = 0;

  /** 用户输入：转给插着的卡带，没有卡带时什么也不做。 */
  virtual void input(const InputEvent& event) = 0;

  /**
   * PPU 对名称表由主板显存响应的一次访问：卡带要观察 PPU 地址线时转给它。图案表与由卡带自己响应的名称表走 ppuSide，不经过这里。
   * @param ppuAddress PPU 总线地址。
   */
  virtual void videoAddress(std::uint16_t ppuAddress) { static_cast<void>(ppuAddress); }
};

}  // namespace ember::contracts::hardware
