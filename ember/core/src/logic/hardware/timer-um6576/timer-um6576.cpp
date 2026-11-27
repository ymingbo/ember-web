/*
 * ------------------------------------------------------------------------
 * 名称：timer-um6576.cpp
 * 说明：6576 定时器的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/timer-um6576/timer-um6576.hpp"

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

constexpr std::uint16_t ADDRESS_CONTROL = 0x4034;
constexpr std::uint16_t ADDRESS_PRELOAD = 0x4035;
constexpr std::uint8_t CONTROL_ENABLE = 0x80;
constexpr std::uint8_t CONTROL_CONTINUOUS = 0x40;
constexpr std::uint8_t CONTROL_LINE_CLOCK = 0x20;
constexpr std::uint8_t CONTROL_PRESCALE_MASK = 0x0F;
/** 中断汇集器里定时器的来源位（$4033 位 7）。 */
constexpr std::uint8_t INTERRUPT_SOURCE = 0x80;

}  // namespace

TimerUm6576::TimerUm6576(hw::Scheduler& scheduler, hw::InterruptSink& sink, hw::MasterTime cpuCycleTicks, hw::MasterTime lineTicks)
    : scheduler_(scheduler), sink_(sink), cpuCycleTicks_(cpuCycleTicks), lineTicks_(lineTicks) {
  if (cpuCycleTicks_ < 1 || lineTicks_ < 1) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "定时器的基准周期必须大于 0");
}

hw::MasterTime TimerUm6576::decrementTicks() const {
  const hw::MasterTime base = (control_ & CONTROL_LINE_CLOCK) != 0 ? lineTicks_ : cpuCycleTicks_;
  return base * ((control_ & CONTROL_PRESCALE_MASK) + 1);
}

std::uint8_t TimerUm6576::currentValue() const {
  if (!enabled() || oneShotDone_) return value_;
  const hw::MasterTime elapsed = scheduler_.now() - anchor_;
  const hw::MasterTime decrements = elapsed > 0 ? elapsed / decrementTicks() : 0;
  return decrements >= value_ ? std::uint8_t{0} : static_cast<std::uint8_t>(value_ - decrements);
}

hw::BusRead TimerUm6576::read(std::uint16_t address) {
  if (address == ADDRESS_CONTROL) return hw::driveBus(control_);
  if (address == ADDRESS_PRELOAD) return hw::driveBus(preload_);
  return hw::driveBus(currentValue());
}

void TimerUm6576::write(std::uint16_t address, std::uint8_t value) {
  // 先把当前值推算到现在，再改变设置。
  value_ = currentValue();
  anchor_ = scheduler_.now();
  oneShotDone_ = false;
  if (address == ADDRESS_CONTROL) {
    control_ = value;
  } else if (address == ADDRESS_PRELOAD) {
    preload_ = value;
    value_ = value;
  } else {
    return;
  }
  reschedule();
}

void TimerUm6576::reset(bool /*power*/) {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  control_ = 0;
  preload_ = 0;
  value_ = 0;
  anchor_ = scheduler_.now();
  oneShotDone_ = false;
}

void TimerUm6576::reschedule() {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  if (!enabled() || oneShotDone_) return;
  eventId_ = scheduler_.scheduleAt(anchor_ + decrementTicks() * (static_cast<hw::MasterTime>(value_) + 1), *this, 0);
}

void TimerUm6576::onEvent(hw::MasterTime when, std::uint32_t /*tag*/) {
  eventId_ = hw::NO_EVENT;
  sink_.raise(INTERRUPT_SOURCE);
  value_ = preload_;
  anchor_ = when;
  if ((control_ & CONTROL_CONTINUOUS) != 0) {
    reschedule();
  } else {
    oneShotDone_ = true;
  }
}

}  // namespace ember::logic::hardware
