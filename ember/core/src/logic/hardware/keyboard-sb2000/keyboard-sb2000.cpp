/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-sb2000.cpp
 * 说明：小霸王键盘的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/keyboard-sb2000/keyboard-sb2000.hpp"

#include <algorithm>
#include <cstring>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

constexpr std::uint8_t ACK = 0xFA;
constexpr std::uint8_t SELF_TEST_OK = 0xAA;
constexpr std::uint8_t EXTENDED_PREFIX = 0xE0;
/** 命令应答的延迟（毫秒）。 */
constexpr hw::MasterTime RESPONSE_DELAY_MS = 2;
/** 复位后自检结果的延迟（毫秒）。 */
constexpr hw::MasterTime SELF_TEST_DELAY_MS = 100;
/** 按键变化的检查间隔（毫秒）。 */
constexpr hw::MasterTime REPORT_INTERVAL_MS = 20;

/** 物理键位到 DIK 扫描码。 */
struct KeyEntry {
  const char* code;
  std::uint8_t scanCode;
};

constexpr KeyEntry KEY_TABLE[] = {
    {"Escape", 0x01}, {"Digit1", 0x02}, {"Digit2", 0x03}, {"Digit3", 0x04}, {"Digit4", 0x05}, {"Digit5", 0x06}, {"Digit6", 0x07}, {"Digit7", 0x08},
    {"Digit8", 0x09}, {"Digit9", 0x0A}, {"Digit0", 0x0B}, {"Minus", 0x0C}, {"Equal", 0x0D}, {"Backspace", 0x0E}, {"Tab", 0x0F}, {"KeyQ", 0x10},
    {"KeyW", 0x11}, {"KeyE", 0x12}, {"KeyR", 0x13}, {"KeyT", 0x14}, {"KeyY", 0x15}, {"KeyU", 0x16}, {"KeyI", 0x17}, {"KeyO", 0x18}, {"KeyP", 0x19},
    {"BracketLeft", 0x1A}, {"BracketRight", 0x1B}, {"Enter", 0x1C}, {"ControlLeft", 0x1D}, {"KeyA", 0x1E}, {"KeyS", 0x1F}, {"KeyD", 0x20},
    {"KeyF", 0x21}, {"KeyG", 0x22}, {"KeyH", 0x23}, {"KeyJ", 0x24}, {"KeyK", 0x25}, {"KeyL", 0x26}, {"Semicolon", 0x27}, {"Quote", 0x28},
    {"Backquote", 0x29}, {"ShiftLeft", 0x2A}, {"Backslash", 0x2B}, {"KeyZ", 0x2C}, {"KeyX", 0x2D}, {"KeyC", 0x2E}, {"KeyV", 0x2F}, {"KeyB", 0x30},
    {"KeyN", 0x31}, {"KeyM", 0x32}, {"Comma", 0x33}, {"Period", 0x34}, {"Slash", 0x35}, {"ShiftRight", 0x36}, {"NumpadMultiply", 0x37},
    {"AltLeft", 0x38}, {"Space", 0x39}, {"CapsLock", 0x3A}, {"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E}, {"F5", 0x3F}, {"F6", 0x40},
    {"F7", 0x41}, {"F8", 0x42}, {"F9", 0x43}, {"F10", 0x44}, {"NumLock", 0x45}, {"ScrollLock", 0x46}, {"Numpad7", 0x47}, {"Numpad8", 0x48},
    {"Numpad9", 0x49}, {"NumpadSubtract", 0x4A}, {"Numpad4", 0x4B}, {"Numpad5", 0x4C}, {"Numpad6", 0x4D}, {"NumpadAdd", 0x4E}, {"Numpad1", 0x4F},
    {"Numpad2", 0x50}, {"Numpad3", 0x51}, {"Numpad0", 0x52}, {"NumpadDecimal", 0x53}, {"F11", 0x57}, {"F12", 0x58},
    // 扩展键（DIK 值是码集 1 的低 7 位加 $80）
    {"NumpadEnter", 0x9C}, {"ControlRight", 0x9D}, {"NumpadDivide", 0xB5}, {"AltRight", 0xB8}, {"Home", 0xC7}, {"ArrowUp", 0xC8}, {"PageUp", 0xC9},
    {"ArrowLeft", 0xCB}, {"ArrowRight", 0xCD}, {"End", 0xCF}, {"ArrowDown", 0xD0}, {"PageDown", 0xD1}, {"Insert", 0xD2}, {"Delete", 0xD3},
};

}  // namespace

KeyboardSb2000::KeyboardSb2000(hw::Scheduler& scheduler, hw::MasterTime millisecondTicks) : scheduler_(scheduler), ms_(millisecondTicks) {
  if (ms_ < 1) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "1 毫秒的主时钟周期数必须大于 0");
}

std::optional<std::uint8_t> KeyboardSb2000::scanCodeFor(std::string_view code) {
  for (const KeyEntry& entry : KEY_TABLE) {
    if (code == entry.code) return entry.scanCode;
  }
  return std::nullopt;
}

void KeyboardSb2000::setKey(std::uint8_t scanCode, bool down) { status_[scanCode] = down; }

void KeyboardSb2000::reset(bool power) {
  if (sendEvent_ != hw::NO_EVENT) scheduler_.cancel(sendEvent_);
  if (reportEvent_ != hw::NO_EVENT) scheduler_.cancel(reportEvent_);
  sendEvent_ = hw::NO_EVENT;
  reportEvent_ = hw::NO_EVENT;
  queue_.clear();
  working_ = false;
  waitingOption_ = false;
  command_ = 0;
  ledStatus_ = 0;
  scanCodeSet_ = 2;
  if (power) {
    status_.fill(false);
  }
  reported_ = status_;
}

void KeyboardSb2000::enqueue(std::uint8_t value, hw::MasterTime delay) {
  queue_.push_back(Pending{value, delay});
  pump();
}

void KeyboardSb2000::pump() {
  if (sendEvent_ != hw::NO_EVENT || queue_.empty()) return;
  sendEvent_ = scheduler_.scheduleAt(scheduler_.now() + queue_.front().delay, *this, TAG_SEND);
}

void KeyboardSb2000::handleCommand(std::uint8_t value) {
  if (waitingOption_) {
    waitingOption_ = false;
    if (command_ == 0xED) {
      ledStatus_ = value;
    } else if (command_ == 0xF0 && value != 0) {
      scanCodeSet_ = value;
      if (!working_) {
        working_ = true;
        reportEvent_ = scheduler_.scheduleAt(scheduler_.now() + REPORT_INTERVAL_MS * ms_, *this, TAG_REPORT);
      }
    }
    enqueue(ACK, RESPONSE_DELAY_MS * ms_);
    return;
  }
  switch (value) {
    case 0xED:
    case 0xF0:
    case 0xF3:
    case 0xFB:
    case 0xFC:
    case 0xFD:
      command_ = value;
      waitingOption_ = true;
      enqueue(ACK, RESPONSE_DELAY_MS * ms_);
      break;
    case 0xF2:
      enqueue(ACK, RESPONSE_DELAY_MS * ms_);
      enqueue(0x12, ms_);
      enqueue(0x34, ms_);
      break;
    case 0xFF:
      enqueue(ACK, RESPONSE_DELAY_MS * ms_);
      enqueue(SELF_TEST_OK, SELF_TEST_DELAY_MS * ms_);
      break;
    default:
      enqueue(ACK, RESPONSE_DELAY_MS * ms_);
      break;
  }
}

void KeyboardSb2000::sendByte(std::uint8_t value) { handleCommand(value); }

void KeyboardSb2000::onEvent(hw::MasterTime /*when*/, std::uint32_t tag) {
  if (tag == TAG_SEND) {
    sendEvent_ = hw::NO_EVENT;
    if (queue_.empty()) return;
    const std::uint8_t value = queue_.front().value;
    queue_.pop_front();
    if (receiver_ != nullptr) receiver_->receiveByte(value);
    pump();
    return;
  }
  reportEvent_ = scheduler_.scheduleAt(scheduler_.now() + REPORT_INTERVAL_MS * ms_, *this, TAG_REPORT);
  report();
}

void KeyboardSb2000::report() {
  // 上一批字节还没有发完时不报告新的按键。
  if (!working_ || !queue_.empty() || sendEvent_ != hw::NO_EVENT) return;
  for (unsigned code = 0; code < status_.size(); ++code) {
    if (status_[code] == reported_[code]) continue;
    reported_[code] = status_[code];
    const bool pressed = status_[code];
    if (code < 0x80) {
      enqueue(static_cast<std::uint8_t>(pressed ? code : (code | 0x80)), 1);
    } else {
      enqueue(EXTENDED_PREFIX, 1);
      enqueue(static_cast<std::uint8_t>(pressed ? (code & 0x7F) : code), ms_);
    }
    return;
  }
}

}  // namespace ember::logic::hardware
