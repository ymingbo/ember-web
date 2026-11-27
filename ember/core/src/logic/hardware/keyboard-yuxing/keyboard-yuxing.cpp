/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-yuxing.cpp
 * 说明：裕兴学习机键盘的实现：矩阵读取与键位表。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/keyboard-yuxing/keyboard-yuxing.hpp"

#include <unordered_map>

namespace ember::logic::hardware {

namespace {

/** 一个键位：PC 的键位名（KeyboardEvent.code）与它在矩阵里的位置。 */
struct KeyPosition {
  const char* code;
  std::uint8_t row;
  std::uint8_t column;
};

/**
 * 键位表。每行 8 列：
 * 0 行 Esc、F9、7、R、A、（空）、（空）、Shift；1 行小键盘回车、乘、除、上、退格、F12；2 行小键盘加、NumLock、左、下、右；3 行小键盘 7、8、减、9，F11、End、PageDown；
 * 4 至 8 行是 F8 至 F4 与各行的字母、数字、标点，9 行 F3、1、0、U、F、CapsLock、左 Win；10 行 F2、`、9、Y、D、Ctrl、Alt、ScrollLock；11 行 F1、回车、8、T、S、右 Win、菜单键；
 * 12 行小键盘小数点、F10、小键盘 4、6、5，Insert、Home、PageUp；13 行小键盘 0、1、3、2，PrintScreen、Tab、Pause。
 */
const std::vector<KeyPosition>& keyTable() {
  static const std::vector<KeyPosition> table = {
      {"Escape", 0, 0}, {"F9", 0, 1}, {"Digit7", 0, 2}, {"KeyR", 0, 3}, {"KeyA", 0, 4}, {"ShiftLeft", 0, 7}, {"ShiftRight", 0, 7},
      {"NumpadEnter", 1, 1}, {"NumpadMultiply", 1, 3}, {"NumpadDivide", 1, 4}, {"ArrowUp", 1, 5}, {"Backspace", 1, 6}, {"F12", 1, 7},
      {"NumpadAdd", 2, 3}, {"NumLock", 2, 4}, {"ArrowLeft", 2, 5}, {"ArrowDown", 2, 6}, {"ArrowRight", 2, 7},
      {"Numpad7", 3, 1}, {"Numpad8", 3, 2}, {"NumpadSubtract", 3, 3}, {"Numpad9", 3, 4}, {"F11", 3, 5}, {"End", 3, 6}, {"PageDown", 3, 7},
      {"F8", 4, 0}, {"Digit6", 4, 1}, {"KeyE", 4, 2}, {"BracketRight", 4, 3}, {"KeyL", 4, 4}, {"KeyZ", 4, 5}, {"KeyN", 4, 6}, {"Space", 4, 7},
      {"F7", 5, 0}, {"Digit5", 5, 1}, {"KeyW", 5, 2}, {"BracketLeft", 5, 3}, {"KeyK", 5, 4}, {"Delete", 5, 5}, {"KeyB", 5, 6}, {"Slash", 5, 7},
      {"F6", 6, 0}, {"Digit4", 6, 1}, {"KeyQ", 6, 2}, {"KeyP", 6, 3}, {"KeyJ", 6, 4}, {"Backslash", 6, 5}, {"KeyV", 6, 6}, {"Period", 6, 7},
      {"F5", 7, 0}, {"Digit3", 7, 1}, {"Equal", 7, 2}, {"KeyO", 7, 3}, {"KeyH", 7, 4}, {"Quote", 7, 5}, {"KeyC", 7, 6}, {"Comma", 7, 7},
      {"F4", 8, 0}, {"Digit2", 8, 1}, {"Minus", 8, 2}, {"KeyI", 8, 3}, {"KeyG", 8, 4}, {"Semicolon", 8, 5}, {"KeyX", 8, 6}, {"KeyM", 8, 7},
      {"F3", 9, 0}, {"Digit1", 9, 1}, {"Digit0", 9, 2}, {"KeyU", 9, 3}, {"KeyF", 9, 4}, {"CapsLock", 9, 5}, {"MetaLeft", 9, 6},
      {"F2", 10, 0}, {"Backquote", 10, 1}, {"Digit9", 10, 2}, {"KeyY", 10, 3}, {"KeyD", 10, 4}, {"ControlLeft", 10, 5}, {"ControlRight", 10, 5}, {"AltLeft", 10, 6}, {"AltRight", 10, 6}, {"ScrollLock", 10, 7},
      {"F1", 11, 0}, {"Enter", 11, 1}, {"Digit8", 11, 2}, {"KeyT", 11, 3}, {"KeyS", 11, 4}, {"MetaRight", 11, 6}, {"ContextMenu", 11, 7},
      {"NumpadDecimal", 12, 0}, {"F10", 12, 1}, {"Numpad4", 12, 2}, {"Numpad6", 12, 3}, {"Numpad5", 12, 4}, {"Insert", 12, 5}, {"Home", 12, 6}, {"PageUp", 12, 7},
      {"Numpad0", 13, 1}, {"Numpad1", 13, 2}, {"Numpad3", 13, 3}, {"Numpad2", 13, 4}, {"PrintScreen", 13, 5}, {"Tab", 13, 6}, {"Pause", 13, 7}};
  return table;
}

/** 键位名到键位表下标的索引。 */
const std::unordered_map<std::string_view, std::size_t>& keyIndex() {
  static const auto index = [] {
    std::unordered_map<std::string_view, std::size_t> result;
    const std::vector<KeyPosition>& table = keyTable();
    for (std::size_t position = 0; position < table.size(); ++position) result.emplace(table[position].code, position);
    return result;
  }();
  return index;
}

}  // namespace

KeyboardYuxing::KeyboardYuxing() : down_(keyTable().size(), false) {}

std::uint8_t KeyboardYuxing::read() const {
  std::uint8_t result = 0;
  for (unsigned row = 0; row < ROWS; ++row) {
    if ((rowSelect_ & (1U << row)) != 0) result = static_cast<std::uint8_t>(result | rows_[row]);
  }
  return result;
}

bool KeyboardYuxing::setKey(std::string_view code, bool down) {
  const auto found = keyIndex().find(code);
  if (found == keyIndex().end()) return false;
  down_[found->second] = down;
  rebuildRows();
  return true;
}

void KeyboardYuxing::releaseAll() {
  down_.assign(down_.size(), false);
  rows_.fill(0);
}

void KeyboardYuxing::rebuildRows() {
  rows_.fill(0);
  const std::vector<KeyPosition>& table = keyTable();
  for (std::size_t position = 0; position < table.size(); ++position) {
    if (down_[position]) rows_[table[position].row] = static_cast<std::uint8_t>(rows_[table[position].row] | (1U << table[position].column));
  }
}

}  // namespace ember::logic::hardware
