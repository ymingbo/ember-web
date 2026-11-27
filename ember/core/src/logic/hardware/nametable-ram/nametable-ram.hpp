/*
 * ------------------------------------------------------------------------
 * 名称：nametable-ram.hpp
 * 说明：主板上的名称表显存（CIRAM，2K）：PPU 访问名称表地址时，按卡槽的显存控制线选择由主板显存的哪一页响应，或交给卡带自带的显存。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：挂在 PPU 总线的 $2000-$3EFF；水平、垂直、单屏镜像都由卡带决定的 A10 实现，本模块不判断镜像方式；
 *       $3000-$3EFF 是 $2000-$2EFF 的镜像，PPU 总线 14 位地址已经保证落在这一段。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/cartridge-slot.hpp"

namespace ember::logic::hardware {

/** 名称表显存。 */
class NametableRam final : public contracts::hardware::BusDevice {
 public:
  /** @param slot 卡槽，提供显存控制线，必须活到本对象销毁。 */
  explicit NametableRam(contracts::hardware::CartridgeSlot& slot) : slot_(slot) {}

  contracts::hardware::BusRead read(std::uint16_t address) override {
    const contracts::hardware::CiramControl control = slot_.ciramControl(address);
    if (!control.enable) return slot_.ppuSide().read(address);
    slot_.videoAddress(address);
    return contracts::hardware::driveBus(ram_[offset(control, address)]);
  }

  void write(std::uint16_t address, std::uint8_t value) override {
    const contracts::hardware::CiramControl control = slot_.ciramControl(address);
    if (!control.enable) {
      slot_.ppuSide().write(address, value);
      return;
    }
    slot_.videoAddress(address);
    ram_[offset(control, address)] = value;
  }

  contracts::hardware::BusRead peek(std::uint16_t address) override {
    const contracts::hardware::CiramControl control = slot_.ciramControl(address);
    if (!control.enable) return slot_.ppuSide().peek(address);
    return contracts::hardware::driveBus(ram_[offset(control, address)]);
  }

  /** 全部清为指定的值（上电或测试用）。 */
  void fill(std::uint8_t value) { ram_.fill(value); }

  /** 调试写入：直接改 2K 显存的一个字节。 */
  void poke(std::size_t index, std::uint8_t value) { ram_.at(index) = value; }

  /** 2K 显存的内容，测试与调试读取。 */
  [[nodiscard]] std::span<const std::uint8_t> bytes() const { return ram_; }

 private:
  /** 显存内的偏移：A10 选 1K 页，页内取地址的低 10 位。 */
  static std::size_t offset(contracts::hardware::CiramControl control, std::uint16_t address) { return (control.a10 ? 0x400U : 0U) | (address & 0x3FFU); }

  contracts::hardware::CartridgeSlot& slot_;
  std::array<std::uint8_t, 2048> ram_{};
};

}  // namespace ember::logic::hardware
