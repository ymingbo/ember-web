/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-sb97.cpp
 * 说明：小霸王 97 键盘的实现：扫描控制与键位表。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/keyboard-sb97/keyboard-sb97.hpp"

namespace ember::logic::hardware {

namespace {

/** 写 $4016 的值。 */
constexpr std::uint8_t BIT_SCAN = 0x04;
constexpr std::uint8_t SCAN_RESET = 0x05;
constexpr std::uint8_t SCAN_NEXT_POSITION = 0x04;
constexpr std::uint8_t SCAN_TOGGLE_HALF = 0x06;

/** 一个键位：PC 的键位名（KeyboardEvent.code）与它在矩阵里的位置。 */
struct KeyPosition {
  const char* code;
  /** 扫描位置，1 到 13。 */
  std::uint8_t position;
  /** 0 前半边，1 后半边。 */
  std::uint8_t half;
  /** 半边里的键序号，0 到 3，对应 $4017 的位 1 到 4。 */
  std::uint8_t bit;
};

/** 键位表：每个位置两个半边、每个半边 4 个键。 */
const std::vector<KeyPosition>& keyTable() {
  static const std::vector<KeyPosition> table = {
      // 位置 1
      {"Digit4", 1, 0, 0}, {"KeyG", 1, 0, 1}, {"KeyF", 1, 0, 2}, {"KeyC", 1, 0, 3},
      {"F2", 1, 1, 0}, {"KeyE", 1, 1, 1}, {"Digit5", 1, 1, 2}, {"KeyV", 1, 1, 3},
      // 位置 2
      {"Digit2", 2, 0, 0}, {"KeyD", 2, 0, 1}, {"KeyS", 2, 0, 2}, {"End", 2, 0, 3},
      {"F1", 2, 1, 0}, {"KeyW", 2, 1, 1}, {"Digit3", 2, 1, 2}, {"KeyX", 2, 1, 3},
      // 位置 3
      {"Insert", 3, 0, 0}, {"Backspace", 3, 0, 1}, {"PageDown", 3, 0, 2}, {"ArrowRight", 3, 0, 3},
      {"F8", 3, 1, 0}, {"PageUp", 3, 1, 1}, {"Delete", 3, 1, 2}, {"Home", 3, 1, 3},
      // 位置 4
      {"Digit9", 4, 0, 0}, {"KeyI", 4, 0, 1}, {"KeyL", 4, 0, 2}, {"Comma", 4, 0, 3},
      {"F5", 4, 1, 0}, {"KeyO", 4, 1, 1}, {"Digit0", 4, 1, 2}, {"Period", 4, 1, 3},
      // 位置 5
      {"BracketRight", 5, 0, 0}, {"Enter", 5, 0, 1}, {"ArrowUp", 5, 0, 2}, {"ArrowLeft", 5, 0, 3},
      {"F7", 5, 1, 0}, {"BracketLeft", 5, 1, 1}, {"Backslash", 5, 1, 2}, {"ArrowDown", 5, 1, 3},
      // 位置 6
      {"KeyQ", 6, 0, 0}, {"CapsLock", 6, 0, 1}, {"KeyZ", 6, 0, 2}, {"Tab", 6, 0, 3},
      {"Escape", 6, 1, 0}, {"KeyA", 6, 1, 1}, {"Digit1", 6, 1, 2}, {"ControlLeft", 6, 1, 3},
      // 位置 7
      {"Digit7", 7, 0, 0}, {"KeyY", 7, 0, 1}, {"KeyK", 7, 0, 2}, {"KeyM", 7, 0, 3},
      {"F4", 7, 1, 0}, {"KeyU", 7, 1, 1}, {"Digit8", 7, 1, 2}, {"KeyJ", 7, 1, 3},
      // 位置 8
      {"Minus", 8, 0, 0}, {"Semicolon", 8, 0, 1}, {"Quote", 8, 0, 2}, {"Slash", 8, 0, 3},
      {"F6", 8, 1, 0}, {"KeyP", 8, 1, 1}, {"Equal", 8, 1, 2}, {"ShiftLeft", 8, 1, 3}, {"ShiftRight", 8, 1, 3},
      // 位置 9
      {"KeyT", 9, 0, 0}, {"KeyH", 9, 0, 1}, {"KeyN", 9, 0, 2}, {"Space", 9, 0, 3},
      {"F3", 9, 1, 0}, {"KeyR", 9, 1, 1}, {"Digit6", 9, 1, 2}, {"KeyB", 9, 1, 3},
      // 位置 10：没有键位名，见 KeyboardSb97 的矩阵初值
      // 位置 11
      {"AltLeft", 11, 0, 0}, {"Numpad4", 11, 0, 1}, {"Numpad7", 11, 0, 2}, {"F11", 11, 0, 3},
      {"F12", 11, 1, 0}, {"Numpad1", 11, 1, 1}, {"Numpad2", 11, 1, 2}, {"Numpad8", 11, 1, 3},
      // 位置 12
      {"NumpadSubtract", 12, 0, 0}, {"NumpadAdd", 12, 0, 1}, {"NumpadMultiply", 12, 0, 2}, {"Numpad9", 12, 0, 3},
      {"F10", 12, 1, 0}, {"Numpad5", 12, 1, 1}, {"NumpadDivide", 12, 1, 2}, {"NumLock", 12, 1, 3},
      // 位置 13：空格键在这里也有一份
      {"Backquote", 13, 0, 0}, {"Numpad6", 13, 0, 1}, {"Pause", 13, 0, 2}, {"Space", 13, 0, 3},
      {"F9", 13, 1, 0}, {"Numpad3", 13, 1, 1}, {"NumpadDecimal", 13, 1, 2}, {"Numpad0", 13, 1, 3}};
  return table;
}

}  // namespace

KeyboardSb97::KeyboardSb97() : pressed_(keyTable().size(), false) { rebuildMatrix(); }

void KeyboardSb97::reset() {
  position_ = 0;
  firstHalf_ = false;
}

void KeyboardSb97::write(std::uint8_t value) {
  if ((value & BIT_SCAN) == 0) return;
  if (value == SCAN_RESET) {
    firstHalf_ = false;
    position_ = 0;
  } else if (value == SCAN_NEXT_POSITION) {
    if (++position_ > POSITIONS) position_ = 0;
    firstHalf_ = !firstHalf_;
  } else if (value == SCAN_TOGGLE_HALF) {
    firstHalf_ = !firstHalf_;
  }
}

std::uint8_t KeyboardSb97::read() const {
  std::uint8_t value = DATA_MASK;
  if (position_ >= 1 && position_ <= POSITIONS) {
    const std::uint8_t down = matrix_[position_][firstHalf_ ? 0 : 1];
    value = static_cast<std::uint8_t>(value & ~(down << 1));
  }
  return value;
}

bool KeyboardSb97::setKey(std::string_view code, bool down) {
  bool found = false;
  const auto& table = keyTable();
  for (std::size_t index = 0; index < table.size(); ++index) {
    if (code == table[index].code) {
      pressed_[index] = down;
      found = true;
    }
  }
  if (found) rebuildMatrix();
  return found;
}

void KeyboardSb97::releaseAll() {
  pressed_.assign(pressed_.size(), false);
  rebuildMatrix();
}

void KeyboardSb97::rebuildMatrix() {
  for (auto& position : matrix_) position.fill(0);
  // 位置 10 后半边的第 1 个键恒为按下。
  matrix_[10][1] = 0x01;
  const auto& table = keyTable();
  for (std::size_t index = 0; index < table.size(); ++index) {
    if (pressed_[index]) matrix_[table[index].position][table[index].half] = static_cast<std::uint8_t>(matrix_[table[index].position][table[index].half] | (1U << table[index].bit));
  }
}

bool KeyboardSb97::locate(std::string_view code, unsigned& position, bool& firstHalf, unsigned& bit) {
  for (const KeyPosition& key : keyTable()) {
    if (code == key.code) {
      position = key.position;
      firstHalf = key.half == 0;
      bit = key.bit;
      return true;
    }
  }
  return false;
}

}  // namespace ember::logic::hardware
