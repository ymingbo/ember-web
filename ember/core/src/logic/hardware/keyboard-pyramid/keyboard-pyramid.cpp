/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-pyramid.cpp
 * 说明：金字塔学习机键盘的实现：行列扫描与键位表。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/keyboard-pyramid/keyboard-pyramid.hpp"

#include <string_view>
#include <unordered_map>
#include <vector>

namespace ember::logic::hardware {

namespace {

/** 写 $4016 的命令字。 */
constexpr std::uint8_t COMMAND_ROW_RESET_LOW = 0;
constexpr std::uint8_t COMMAND_ROW_RESET_HIGH = 1;
constexpr std::uint8_t COMMAND_COLUMN_RESET = 2;
constexpr std::uint8_t COMMAND_ROW_NEXT_A = 6;
constexpr std::uint8_t COMMAND_ROW_NEXT_B = 7;

/** 一个键位：PC 的键位名（KeyboardEvent.code）与它在矩阵里的位置。 */
struct KeyPosition {
  const char* code;
  std::uint8_t row;
  std::uint8_t column;
};

/**
 * 键位表。第 0 行是修饰键与空格、Esc（左右 Shift、Ctrl、Alt 各占同一个位置），第 1 行是功能键，第 2 至 9 行是字母、数字与小键盘，
 * 第 10 行是 F9 至 F12 与小键盘减号，第 11 行是编辑与光标键，第 12 行是 Print Screen、Scroll Lock、Page Up、Home 与 Pause。
 */
const std::vector<KeyPosition>& keyTable() {
  static const std::vector<KeyPosition> table = {
      {"ShiftLeft", 0, 0}, {"ShiftRight", 0, 0}, {"Tab", 0, 1}, {"Backquote", 0, 2}, {"ControlLeft", 0, 3}, {"ControlRight", 0, 3},
      {"CapsLock", 0, 4}, {"AltLeft", 0, 5}, {"AltRight", 0, 5}, {"Space", 0, 6}, {"Escape", 0, 7},
      {"F3", 1, 0}, {"F1", 1, 1}, {"F2", 1, 2}, {"F8", 1, 3}, {"F4", 1, 4}, {"F5", 1, 5}, {"F7", 1, 6}, {"F6", 1, 7},
      {"KeyZ", 2, 0}, {"KeyQ", 2, 1}, {"Digit1", 2, 2}, {"Enter", 2, 3}, {"KeyA", 2, 4}, {"NumpadDecimal", 2, 5}, {"Numpad0", 2, 6}, {"NumpadAdd", 2, 7},
      {"KeyX", 3, 0}, {"KeyW", 3, 1}, {"Digit2", 3, 2}, {"Numpad9", 3, 3}, {"KeyS", 3, 4}, {"Numpad6", 3, 5}, {"Numpad3", 3, 6}, {"NumpadMultiply", 3, 7},
      {"KeyC", 4, 0}, {"KeyE", 4, 1}, {"Digit3", 4, 2}, {"Numpad8", 4, 3}, {"KeyD", 4, 4}, {"Numpad5", 4, 5}, {"Numpad2", 4, 6}, {"NumpadDivide", 4, 7},
      {"KeyV", 5, 0}, {"KeyR", 5, 1}, {"Digit4", 5, 2}, {"Numpad7", 5, 3}, {"KeyF", 5, 4}, {"Numpad4", 5, 5}, {"Numpad1", 5, 6}, {"NumLock", 5, 7},
      {"KeyB", 6, 0}, {"KeyT", 6, 1}, {"Digit5", 6, 2}, {"BracketRight", 6, 3}, {"KeyG", 6, 4}, {"NumpadEnter", 6, 5}, {"Backslash", 6, 6}, {"Backspace", 6, 7},
      {"Comma", 7, 0}, {"KeyI", 7, 1}, {"Digit8", 7, 2}, {"KeyO", 7, 3}, {"KeyK", 7, 4}, {"KeyL", 7, 5}, {"Period", 7, 6}, {"Digit9", 7, 7},
      {"KeyM", 8, 0}, {"KeyU", 8, 1}, {"Digit7", 8, 2}, {"KeyP", 8, 3}, {"KeyJ", 8, 4}, {"Semicolon", 8, 5}, {"Slash", 8, 6}, {"Digit0", 8, 7},
      {"KeyN", 9, 0}, {"KeyY", 9, 1}, {"Digit6", 9, 2}, {"BracketLeft", 9, 3}, {"KeyH", 9, 4}, {"Quote", 9, 5}, {"Equal", 9, 6}, {"Minus", 9, 7},
      {"F9", 10, 2}, {"NumpadSubtract", 10, 3}, {"F10", 10, 5}, {"F11", 10, 6}, {"F12", 10, 7},
      {"Delete", 11, 0}, {"End", 11, 1}, {"Insert", 11, 2}, {"ArrowLeft", 11, 3}, {"PageDown", 11, 4}, {"ArrowDown", 11, 5}, {"ArrowRight", 11, 6}, {"ArrowUp", 11, 7},
      {"PrintScreen", 12, 3}, {"ScrollLock", 12, 4}, {"PageUp", 12, 5}, {"Home", 12, 6}, {"Pause", 12, 7}};
  return table;
}

/** 键位名到矩阵位置的索引（一个键位名可能不止一个位置）。 */
const std::unordered_multimap<std::string_view, const KeyPosition*>& keyIndex() {
  static const auto index = [] {
    std::unordered_multimap<std::string_view, const KeyPosition*> result;
    for (const KeyPosition& position : keyTable()) result.emplace(position.code, &position);
    return result;
  }();
  return index;
}

}  // namespace

void KeyboardPyramid::write(std::uint8_t value) {
  switch (value) {
    case COMMAND_ROW_RESET_LOW:
    case COMMAND_ROW_RESET_HIGH:
      row_ = 0;
      break;
    case COMMAND_COLUMN_RESET:
      column_ = 0;
      break;
    case COMMAND_ROW_NEXT_A:
    case COMMAND_ROW_NEXT_B:
      row_ = row_ + 1 >= ROWS ? 0 : row_ + 1;
      break;
    default:
      break;
  }
}

std::uint8_t KeyboardPyramid::read() {
  const unsigned column = column_;
  column_ = (column_ + 1) & 0xFFU;
  if (column >= COLUMNS) return 0;
  return (rows_[row_] >> column) & 1U ? DATA_MASK : 0;
}

bool KeyboardPyramid::setKey(std::string_view code, bool down) {
  const auto [first, last] = keyIndex().equal_range(code);
  if (first == last) return false;
  for (auto it = first; it != last; ++it) {
    const KeyPosition& position = *it->second;
    const std::uint8_t mask = static_cast<std::uint8_t>(1U << position.column);
    rows_[position.row] = down ? static_cast<std::uint8_t>(rows_[position.row] | mask) : static_cast<std::uint8_t>(rows_[position.row] & ~mask);
  }
  return true;
}

void KeyboardPyramid::reset() {
  row_ = 0;
  column_ = 0;
}

}  // namespace ember::logic::hardware
