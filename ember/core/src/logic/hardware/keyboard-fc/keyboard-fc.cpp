/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-fc.cpp
 * 说明：Family BASIC 键盘的实现：矩阵扫描与键位表。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/keyboard-fc/keyboard-fc.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace ember::logic::hardware {

namespace {

/** 写 $4016 的位。 */
constexpr std::uint8_t BIT_RESET = 0x01;
constexpr std::uint8_t BIT_COLUMN = 0x02;
constexpr std::uint8_t BIT_ENABLE = 0x04;
/** 行计数器的循环长度：9 行有键加一行没有键。 */
constexpr unsigned ROW_CYCLE = 10;

/** 一个键位：PC 的键位名（KeyboardEvent.code）与它在矩阵里的位置。 */
struct KeyPosition {
  const char* code;
  std::uint8_t row;
  std::uint8_t column;
  /** 读 $4017 时的位号，1 到 4。 */
  std::uint8_t bit;
};

/**
 * 键位表：矩阵每行两列、每列从 $4017 位 4 到位 1，
 * 键位名按 JIS 键盘的物理位置：@ 在 BracketLeft、[ 在 BracketRight、] 在 Backslash、: 在 Quote、^ 在 Equal、¥ 在 IntlYen、_ 在 IntlRo；
 * 没有 JIS 专有键的键盘上，¥ 另放在 Backquote；STOP 是 Pause，DEL 是 Backspace，CLR/HOME 是 Home，GRPH 是左 Alt，KANA 是右 Alt。
 */
const std::vector<KeyPosition>& keyTable() {
  static const std::vector<KeyPosition> table = {
      // 第 0 行：列 0 为 ] [ RETURN F8，列 1 为 STOP ¥ RSHIFT KANA
      {"Backslash", 0, 0, 4}, {"BracketRight", 0, 0, 3}, {"Enter", 0, 0, 2}, {"NumpadEnter", 0, 0, 2}, {"F8", 0, 0, 1},
      {"Pause", 0, 1, 4}, {"IntlYen", 0, 1, 3}, {"Backquote", 0, 1, 3}, {"ShiftRight", 0, 1, 2}, {"AltRight", 0, 1, 1}, {"KanaMode", 0, 1, 1},
      // 第 1 行：; : @ F7，^ - / _
      {"Semicolon", 1, 0, 4}, {"Quote", 1, 0, 3}, {"BracketLeft", 1, 0, 2}, {"F7", 1, 0, 1},
      {"Equal", 1, 1, 4}, {"Minus", 1, 1, 3}, {"Slash", 1, 1, 2}, {"IntlRo", 1, 1, 1},
      // 第 2 行：K L O F6，0 P , .
      {"KeyK", 2, 0, 4}, {"KeyL", 2, 0, 3}, {"KeyO", 2, 0, 2}, {"F6", 2, 0, 1},
      {"Digit0", 2, 1, 4}, {"KeyP", 2, 1, 3}, {"Comma", 2, 1, 2}, {"Period", 2, 1, 1},
      // 第 3 行：J U I F5，8 9 N M
      {"KeyJ", 3, 0, 4}, {"KeyU", 3, 0, 3}, {"KeyI", 3, 0, 2}, {"F5", 3, 0, 1},
      {"Digit8", 3, 1, 4}, {"Digit9", 3, 1, 3}, {"KeyN", 3, 1, 2}, {"KeyM", 3, 1, 1},
      // 第 4 行：H G Y F4，6 7 V B
      {"KeyH", 4, 0, 4}, {"KeyG", 4, 0, 3}, {"KeyY", 4, 0, 2}, {"F4", 4, 0, 1},
      {"Digit6", 4, 1, 4}, {"Digit7", 4, 1, 3}, {"KeyV", 4, 1, 2}, {"KeyB", 4, 1, 1},
      // 第 5 行：D R T F3，4 5 C F
      {"KeyD", 5, 0, 4}, {"KeyR", 5, 0, 3}, {"KeyT", 5, 0, 2}, {"F3", 5, 0, 1},
      {"Digit4", 5, 1, 4}, {"Digit5", 5, 1, 3}, {"KeyC", 5, 1, 2}, {"KeyF", 5, 1, 1},
      // 第 6 行：A S W F2，3 E Z X
      {"KeyA", 6, 0, 4}, {"KeyS", 6, 0, 3}, {"KeyW", 6, 0, 2}, {"F2", 6, 0, 1},
      {"Digit3", 6, 1, 4}, {"KeyE", 6, 1, 3}, {"KeyZ", 6, 1, 2}, {"KeyX", 6, 1, 1},
      // 第 7 行：CTR Q ESC F1，2 1 GRPH LSHIFT
      {"ControlLeft", 7, 0, 4}, {"ControlRight", 7, 0, 4}, {"KeyQ", 7, 0, 3}, {"Escape", 7, 0, 2}, {"F1", 7, 0, 1},
      {"Digit2", 7, 1, 4}, {"Digit1", 7, 1, 3}, {"AltLeft", 7, 1, 2}, {"ShiftLeft", 7, 1, 1},
      // 第 8 行：LEFT RIGHT UP CLR/HOME，INS DEL SPACE DOWN
      {"ArrowLeft", 8, 0, 4}, {"ArrowRight", 8, 0, 3}, {"ArrowUp", 8, 0, 2}, {"Home", 8, 0, 1},
      {"Insert", 8, 1, 4}, {"Backspace", 8, 1, 3}, {"Delete", 8, 1, 3}, {"Space", 8, 1, 2}, {"ArrowDown", 8, 1, 1}};
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

void KeyboardFc::write(std::uint8_t value) {
  if ((value & BIT_ENABLE) == 0) {
    enabled_ = false;
    return;
  }
  const bool column = (value & BIT_COLUMN) != 0;
  // 选列线由高变低时，行计数器加 1。
  if (enabled_ && column_ && !column) row_ = (row_ + 1) % ROW_CYCLE;
  column_ = column;
  if ((value & BIT_RESET) != 0) row_ = 0;
  enabled_ = true;
}

std::uint8_t KeyboardFc::read() const {
  if (!enabled_) return 0;
  if (row_ >= ROWS) return DATA_MASK;
  const std::uint8_t pressed = static_cast<std::uint8_t>((rows_[row_] >> (column_ ? 4 : 0)) & 0x0F);
  return static_cast<std::uint8_t>(~(pressed << 1) & DATA_MASK);
}

bool KeyboardFc::setKey(std::string_view code, bool down) {
  const auto [first, last] = keyIndex().equal_range(code);
  if (first == last) return false;
  for (auto it = first; it != last; ++it) {
    const KeyPosition& position = *it->second;
    const std::uint8_t mask = static_cast<std::uint8_t>(1U << ((position.column != 0 ? 4 : 0) + position.bit - 1));
    rows_[position.row] = down ? static_cast<std::uint8_t>(rows_[position.row] | mask) : static_cast<std::uint8_t>(rows_[position.row] & ~mask);
  }
  return true;
}

void KeyboardFc::reset() {
  row_ = 0;
  column_ = false;
  enabled_ = false;
}

}  // namespace ember::logic::hardware
