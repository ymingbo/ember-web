/*
 * ------------------------------------------------------------------------
 * 名称：expio-um6576.hpp
 * 说明：6576 芯片组的扩展 I/O $4026：8 位双向口，位 1 鼠标复位、位 2 并口 #Strobe、位 3-5 并口状态输入、位 6 语音复位。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：位定义：EIO0 空，EIO1 鼠标复位（上升沿发出复位），EIO2 并口 #Strobe（输出），EIO3 Ack（输入），EIO4 Paper Out（输入），
 *       EIO5 Select（输入），EIO6 语音芯片 #INIT（上升沿复位语音），EIO7 键盘复位（不记录，读出恒为 0）；
 *       输入位读到的是“写入的值与引脚电平相与”（引脚为低就读 0）；每次写入都把位 2 送给并口的 #Strobe 线；
 *       鼠标复位与语音复位经回调通知主机装配里接着的模块，本模块不引用它们。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <functional>

#include "contracts/hardware/device.hpp"
#include "contracts/hardware/parallel-port.hpp"

namespace ember::logic::hardware {

/** 6576 扩展 I/O（$4026）。 */
class ExpioUm6576 final : public contracts::hardware::Device {
 public:
  /** @param port 并口，状态位从它读，#Strobe 位送给它。 */
  explicit ExpioUm6576(contracts::hardware::ParallelPort& port) : port_(port) {}

  contracts::hardware::BusRead read(std::uint16_t /*address*/) override {
    std::uint8_t data = latch_;
    if (!port_.ack()) data = static_cast<std::uint8_t>(data & ~BIT_ACK);
    if (!port_.paperOut()) data = static_cast<std::uint8_t>(data & ~BIT_PAPER_OUT);
    if (!port_.select()) data = static_cast<std::uint8_t>(data & ~BIT_SELECT);
    return contracts::hardware::driveBus(data);
  }

  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }

  void write(std::uint16_t /*address*/, std::uint8_t value) override {
    const std::uint8_t changed = static_cast<std::uint8_t>(latch_ ^ value);
    if ((changed & BIT_MOUSE_RESET) != 0 && (value & BIT_MOUSE_RESET) != 0 && onMouseReset) onMouseReset();
    if ((changed & BIT_SPEECH_RESET) != 0 && (value & BIT_SPEECH_RESET) != 0 && onSpeechReset) onSpeechReset();
    port_.setStrobeN((value & BIT_STROBE) != 0);
    // 位 7（键盘复位）不保存，读出恒为 0。
    latch_ = static_cast<std::uint8_t>(value & 0x7F);
  }

  void reset(bool /*power*/) override { latch_ = 0; }

  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

  /** 位 1 出现上升沿：鼠标复位。 */
  std::function<void()> onMouseReset;
  /** 位 6 出现上升沿：语音芯片复位。 */
  std::function<void()> onSpeechReset;

  /** 写入的锁存值（位 7 恒为 0）。 */
  [[nodiscard]] std::uint8_t latch() const { return latch_; }

 private:
  static constexpr std::uint8_t BIT_MOUSE_RESET = 0x02;
  static constexpr std::uint8_t BIT_STROBE = 0x04;
  static constexpr std::uint8_t BIT_ACK = 0x08;
  static constexpr std::uint8_t BIT_PAPER_OUT = 0x10;
  static constexpr std::uint8_t BIT_SELECT = 0x20;
  static constexpr std::uint8_t BIT_SPEECH_RESET = 0x40;

  contracts::hardware::ParallelPort& port_;
  std::uint8_t latch_{0};
};

}  // namespace ember::logic::hardware
