/*
 * ------------------------------------------------------------------------
 * 名称：mouse-sb2000.cpp
 * 说明：小霸王串口鼠标的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/mouse-sb2000/mouse-sb2000.hpp"

#include <algorithm>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

/** 鼠标的 PnP 标识（微软 PnP 串口鼠标）。 */
constexpr std::uint8_t PNP_ID[] = {0x4D, 0x33, 0x08, 0x01, 0x24, 0x28, 0x34, 0x2B, 0x10, 0x10, 0x10, 0x11, 0x3C, 0x3C, 0x2D, 0x2F,
                                   0x35, 0x33, 0x25, 0x3C, 0x30, 0x2E, 0x30, 0x10, 0x26, 0x10, 0x23, 0x19, 0x12, 0x09};
/** RTS 之后第一个字节的延迟（毫秒）。 */
constexpr hw::MasterTime FIRST_BYTE_DELAY_MS = 15;
/** 相邻字节的间隔（毫秒）。 */
constexpr hw::MasterTime BYTE_INTERVAL_MS = 5;

}  // namespace

MouseSb2000::MouseSb2000(hw::Scheduler& scheduler, hw::MasterTime millisecondTicks) : scheduler_(scheduler), ms_(millisecondTicks) {
  if (ms_ < 1) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "1 毫秒的主时钟周期数必须大于 0");
}

void MouseSb2000::reset(bool power) {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  queue_.clear();
  working_ = false;
  if (power) {
    pendingX_ = 0;
    pendingY_ = 0;
    buttons_ = 0;
  }
  reportedButtons_ = buttons_;
}

void MouseSb2000::enqueue(std::uint8_t value, hw::MasterTime delay) { queue_.push_back(Pending{value, delay}); }

void MouseSb2000::pump() {
  if (eventId_ != hw::NO_EVENT || queue_.empty()) return;
  eventId_ = scheduler_.scheduleAt(scheduler_.now() + queue_.front().delay, *this, 0);
}

void MouseSb2000::pulseControl() {
  // RTS：丢弃进行中的传送，重新发送 PnP 标识。
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  queue_.clear();
  bool first = true;
  for (const std::uint8_t byte : PNP_ID) {
    enqueue(byte, (first ? FIRST_BYTE_DELAY_MS : BYTE_INTERVAL_MS) * ms_);
    first = false;
  }
  working_ = true;
  reportedButtons_ = buttons_;
  pump();
}

void MouseSb2000::move(int dx, int dy, std::uint8_t buttons) {
  pendingX_ += dx;
  pendingY_ += dy;
  buttons_ = buttons;
  reportIfIdle();
}

void MouseSb2000::reportIfIdle() {
  if (!working_ || !queue_.empty() || eventId_ != hw::NO_EVENT) return;
  if (pendingX_ == 0 && pendingY_ == 0 && buttons_ == reportedButtons_) return;
  const int dx = std::clamp(pendingX_, -128, 127);
  const int dy = std::clamp(pendingY_, -128, 127);
  pendingX_ -= dx;
  pendingY_ -= dy;
  reportedButtons_ = buttons_;
  const unsigned ux = static_cast<unsigned>(dx) & 0xFF;
  const unsigned uy = static_cast<unsigned>(dy) & 0xFF;
  const unsigned right = (buttons_ & BUTTON_RIGHT) != 0 ? 1 : 0;
  const unsigned left = (buttons_ & BUTTON_LEFT) != 0 ? 1 : 0;
  enqueue(static_cast<std::uint8_t>(0x40 | (right << 4) | (left << 5) | ((uy & 0xC0) >> 4) | ((ux & 0xC0) >> 6)), 1);
  enqueue(static_cast<std::uint8_t>(ux & 0x3F), BYTE_INTERVAL_MS * ms_);
  enqueue(static_cast<std::uint8_t>(uy & 0x3F), BYTE_INTERVAL_MS * ms_);
  pump();
}

void MouseSb2000::onEvent(hw::MasterTime /*when*/, std::uint32_t /*tag*/) {
  eventId_ = hw::NO_EVENT;
  if (queue_.empty()) return;
  const std::uint8_t value = queue_.front().value;
  queue_.pop_front();
  if (receiver_ != nullptr) receiver_->receiveByte(value);
  if (!queue_.empty()) {
    pump();
  } else {
    reportIfIdle();
  }
}

}  // namespace ember::logic::hardware
