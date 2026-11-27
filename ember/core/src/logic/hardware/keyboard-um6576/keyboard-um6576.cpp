/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-um6576.cpp
 * 说明：6576 键盘接口的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/keyboard-um6576/keyboard-um6576.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

constexpr std::uint16_t ADDRESS_INPUT = 0x4020;
constexpr std::uint16_t ADDRESS_OUTPUT = 0x4021;
constexpr std::uint8_t CONTROL_DIRECTION = 0x01;
constexpr std::uint8_t CONTROL_SEND_IRQ = 0x04;
constexpr std::uint8_t CONTROL_CLOCK = 0x80;
/** 中断汇集器里键盘的来源位（$4033 位 5）。 */
constexpr std::uint8_t INTERRUPT_SOURCE = 0x20;

}  // namespace

KeyboardUm6576::KeyboardUm6576(hw::SerialDevice& keyboard, hw::InterruptSink& sink, hw::Scheduler& scheduler, hw::MasterTime sendDelayTicks)
    : keyboard_(keyboard), sink_(sink), scheduler_(scheduler), sendDelayTicks_(sendDelayTicks) {
  keyboard_.connect(this);
}

KeyboardUm6576::~KeyboardUm6576() { keyboard_.connect(nullptr); }

hw::BusRead KeyboardUm6576::read(std::uint16_t address) {
  if (address == ADDRESS_INPUT) return hw::driveBus(input_);
  if (address == ADDRESS_OUTPUT) return hw::releaseBus();
  // 位 6 是键盘数据线，空闲时为高（真机开机值 $C0）。
  return hw::driveBus(static_cast<std::uint8_t>(control_ | 0x40));
}

void KeyboardUm6576::write(std::uint16_t address, std::uint8_t value) {
  if (address == ADDRESS_INPUT) {
    timingControl_ = value;
    return;
  }
  if (address == ADDRESS_OUTPUT) {
    output_ = value;
    return;
  }
  // $4022：时钟位出现上升沿且方向为“系统到键盘”时，发出 $4021 里的字节。
  const bool risingClock = ((control_ ^ value) & CONTROL_CLOCK) != 0 && (control_ & CONTROL_CLOCK) == 0;
  control_ = value;
  if (!risingClock || (value & CONTROL_DIRECTION) == 0) return;
  keyboard_.sendByte(output_);
  if ((value & CONTROL_SEND_IRQ) != 0) {
    if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
    eventId_ = scheduler_.scheduleAt(scheduler_.now() + sendDelayTicks_, *this, 0);
  }
}

void KeyboardUm6576::reset(bool /*power*/) {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  input_ = 0xFF;
  output_ = 0;
  control_ = 0;
  timingControl_ = 0;
}

void KeyboardUm6576::onEvent(hw::MasterTime /*when*/, std::uint32_t /*tag*/) {
  eventId_ = hw::NO_EVENT;
  sink_.raise(INTERRUPT_SOURCE);
}

void KeyboardUm6576::receiveByte(std::uint8_t value) {
  input_ = value;
  sink_.raise(INTERRUPT_SOURCE);
}

}  // namespace ember::logic::hardware
