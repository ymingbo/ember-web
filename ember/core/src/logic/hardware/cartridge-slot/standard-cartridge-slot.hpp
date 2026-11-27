/*
 * ------------------------------------------------------------------------
 * 名称：standard-cartridge-slot.hpp
 * 说明：卡槽的实现：主机上的卡带插座，把 CPU 总线与 PPU 总线上属于卡带的访问转交给插着的卡带。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：空槽时与真机一致：不驱动数据线（开放总线），写入被丢弃，显存控制线选择主板显存且 A10 为低。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/cartridge-slot.hpp"
#include "contracts/hardware/cartridge.hpp"

namespace ember::logic::hardware {

/** 经典 FC 的卡槽。 */
class StandardCartridgeSlot final : public contracts::hardware::CartridgeSlot {
 public:
  StandardCartridgeSlot() : cpuSide_(*this, false), ppuSide_(*this, true) {}

  StandardCartridgeSlot(const StandardCartridgeSlot&) = delete;
  StandardCartridgeSlot& operator=(const StandardCartridgeSlot&) = delete;

  void insert(contracts::hardware::Cartridge& cartridge) override {
    cartridge_ = &cartridge;
    watching_ = cartridge.watchesVideoAddress();
  }

  void eject() override {
    cartridge_ = nullptr;
    watching_ = false;
  }

  [[nodiscard]] bool occupied() const override { return cartridge_ != nullptr; }

  contracts::hardware::BusDevice& cpuSide() override { return cpuSide_; }

  contracts::hardware::BusDevice& ppuSide() override { return ppuSide_; }

  [[nodiscard]] contracts::hardware::CiramControl ciramControl(std::uint16_t ppuAddress) const override {
    return cartridge_ != nullptr ? cartridge_->ciramControl(ppuAddress) : contracts::hardware::CiramControl{true, false};
  }

  void reset(bool power) override {
    if (cartridge_ != nullptr) cartridge_->reset(power);
  }

  void input(const contracts::hardware::InputEvent& event) override {
    if (cartridge_ != nullptr) cartridge_->input(event);
  }

  void videoAddress(std::uint16_t ppuAddress) override {
    if (watching_) cartridge_->onVideoAddress(ppuAddress);
  }

 private:
  /** 一侧的总线设备：转给卡带对应的一侧，空槽时什么也不驱动。 */
  class Side final : public contracts::hardware::BusDevice {
   public:
    Side(StandardCartridgeSlot& slot, bool ppu) : slot_(slot), ppu_(ppu) {}

    contracts::hardware::BusRead read(std::uint16_t address) override {
      if (slot_.cartridge_ == nullptr) return contracts::hardware::releaseBus();
      if (ppu_) slot_.videoAddress(address);
      return target().read(address);
    }

    void write(std::uint16_t address, std::uint8_t value) override {
      if (slot_.cartridge_ == nullptr) return;
      if (ppu_) slot_.videoAddress(address);
      target().write(address, value);
    }

    contracts::hardware::BusRead peek(std::uint16_t address) override {
      if (slot_.cartridge_ == nullptr) return contracts::hardware::releaseBus();
      return target().peek(address);
    }

   private:
    contracts::hardware::BusDevice& target() { return ppu_ ? slot_.cartridge_->ppuSide() : slot_.cartridge_->cpuSide(); }

    StandardCartridgeSlot& slot_;
    bool ppu_;
  };

  contracts::hardware::Cartridge* cartridge_{nullptr};
  /** 插着的卡带是否要观察 PPU 地址线。 */
  bool watching_{false};
  Side cpuSide_;
  Side ppuSide_;
};

}  // namespace ember::logic::hardware
