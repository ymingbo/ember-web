/*
 * ------------------------------------------------------------------------
 * 名称：fc-expansion-port.hpp
 * 说明：经典 FC 的 $4016-$4017：在手柄口与 APU 帧计数器之上，加上 Family BASIC 数据录音机（磁带机）与 Family BASIC 键盘的信号线。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：磁带接线：写 $4016 位 0 是送给磁带的 1 位信号，位 2 是读回使能（为 0 时读回恒为 0，与键盘的使能位是同一位）；
 *       读 $4016 位 1 是磁带送回的 1 位信号；键盘接线：写 $4016 整个字节给键盘，读 $4017 位 1-4 是键状态，键盘接上时才驱动这几位；
 *       手柄口与 APU 的处理原样交给 PadApuPort，本设备只叠加这两个外设；
 *       外设模块与手柄口模块互不引用，只在主机装配里经本设备接在一起；peek 保持默认（不驱动数据线），读磁带信号要推进磁带机，不是无副作用的。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/tape-deck.hpp"
#include "logic/hardware/keyboard-fc/keyboard-fc.hpp"

namespace ember::bootstrap {

/** $4016-$4017 的外设接口。 */
class FcExpansionPort final : public contracts::hardware::BusDevice {
 public:
  /**
   * @param padApu 手柄口与 APU 的转接设备。
   * @param tape 磁带机的信号线。
   * @param keyboard Family BASIC 键盘；接上之前（connectKeyboard）读写都不碰它。
   */
  FcExpansionPort(contracts::hardware::BusDevice& padApu, contracts::hardware::TapeLine& tape, logic::hardware::KeyboardFc& keyboard) : padApu_(padApu), tape_(tape), keyboard_(keyboard) {}

  /** 接上或拔下键盘；拔下时键盘的行列计数器复位。 */
  void connectKeyboard(bool connected) {
    keyboardConnected_ = connected;
    keyboard_.reset();
  }

  [[nodiscard]] bool keyboardConnected() const { return keyboardConnected_; }

  contracts::hardware::BusRead read(std::uint16_t address) override {
    contracts::hardware::BusRead result = padApu_.read(address);
    if (address == TAPE_ADDRESS) {
      if (tapeReadable_ && tape_.level()) result.value = static_cast<std::uint8_t>(result.value | TAPE_INPUT_BIT);
      result.drivenMask = static_cast<std::uint8_t>(result.drivenMask | TAPE_INPUT_BIT);
    } else if (address == KEYBOARD_ADDRESS && keyboardConnected_) {
      result.value = static_cast<std::uint8_t>(result.value | keyboard_.read());
      result.drivenMask = static_cast<std::uint8_t>(result.drivenMask | logic::hardware::KeyboardFc::DATA_MASK);
    }
    return result;
  }

  void write(std::uint16_t address, std::uint8_t value) override {
    padApu_.write(address, value);
    if (address == TAPE_ADDRESS) {
      tape_.drive((value & TAPE_OUTPUT_BIT) != 0);
      tapeReadable_ = (value & TAPE_ENABLE_BIT) != 0;
      if (keyboardConnected_) keyboard_.write(value);
    }
  }

 private:
  static constexpr std::uint16_t TAPE_ADDRESS = 0x4016;
  static constexpr std::uint16_t KEYBOARD_ADDRESS = 0x4017;
  static constexpr std::uint8_t TAPE_OUTPUT_BIT = 0x01;
  static constexpr std::uint8_t TAPE_ENABLE_BIT = 0x04;
  static constexpr std::uint8_t TAPE_INPUT_BIT = 0x02;

  contracts::hardware::BusDevice& padApu_;
  contracts::hardware::TapeLine& tape_;
  logic::hardware::KeyboardFc& keyboard_;
  bool keyboardConnected_{false};
  /** 读回使能：最近一次写 $4016 的位 2。 */
  bool tapeReadable_{false};
};

}  // namespace ember::bootstrap
