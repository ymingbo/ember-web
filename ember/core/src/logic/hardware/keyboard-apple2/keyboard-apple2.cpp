/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-apple2.cpp
 * 说明：苹果 II 类机器键盘的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/keyboard-apple2/keyboard-apple2.hpp"

#include <algorithm>
#include <array>

namespace ember::logic::hardware {

namespace {

/** 一个键位：不按 Shift 与按 Shift 的 ASCII 码。 */
struct KeyEntry {
  std::string_view code;
  std::uint8_t plain;
  std::uint8_t shifted;
};

constexpr std::array<KeyEntry, 32> SYMBOL_KEYS{{
    {"Digit1", '1', '!'},        {"Digit2", '2', '@'},     {"Digit3", '3', '#'},      {"Digit4", '4', '$'},     {"Digit5", '5', '%'},         {"Digit6", '6', '^'},
    {"Digit7", '7', '&'},        {"Digit8", '8', '*'},     {"Digit9", '9', '('},      {"Digit0", '0', ')'},     {"Minus", '-', '_'},          {"Equal", '=', '+'},
    {"BracketLeft", '[', '{'},   {"BracketRight", ']', '}'}, {"Backslash", '\\', '|'}, {"Semicolon", ';', ':'}, {"Quote", '\'', '"'},         {"Comma", ',', '<'},
    {"Period", '.', '>'},        {"Slash", '/', '?'},      {"Backquote", '`', '~'},   {"Space", ' ', ' '},      {"Enter", 0x0D, 0x0D},        {"NumpadEnter", 0x0D, 0x0D},
    {"Escape", 0x1B, 0x1B},      {"Tab", 0x09, 0x09},      {"Backspace", 0x08, 0x08}, {"Delete", 0x7F, 0x7F},   {"ArrowLeft", 0x08, 0x08},    {"ArrowRight", 0x15, 0x15},
    {"ArrowUp", 0x0B, 0x0B},     {"ArrowDown", 0x0A, 0x0A},
}};

/** 是 KeyA 到 KeyZ 就返回字母下标（0 到 25），否则 -1。 */
int letterIndex(std::string_view code) {
  if (code.size() == 4 && code.substr(0, 3) == "Key" && code[3] >= 'A' && code[3] <= 'Z') return code[3] - 'A';
  return -1;
}

/** 功能键：键码固定，不随 Shift 与 Ctrl 变化。 */
struct FunctionKey {
  std::string_view code;
  std::uint8_t value;
};

constexpr std::array<FunctionKey, 9> FUNCTION_KEYS{{
    {"F1", 0x01},  // 字母输入方式
    {"F2", 0x0C},  // 拼音输入方式
    {"F3", 0x17},  // 区位输入方式
    {"F4", 0x14},  // 用户扩充输入方式
    {"F5", 0x06},  // 用户扩充输入方式
    {"F6", 0x1F},  // 中文
    {"F7", 0x11},  // 西文
    {"F8", 0x16},  // 自检（TEST）
    {"Pause", 0x03},  // 退出（QUIT）
}};

const FunctionKey* findFunction(std::string_view code) {
  const auto found = std::find_if(FUNCTION_KEYS.begin(), FUNCTION_KEYS.end(), [code](const FunctionKey& entry) { return entry.code == code; });
  return found == FUNCTION_KEYS.end() ? nullptr : &*found;
}

const KeyEntry* findSymbol(std::string_view code) {
  const auto found = std::find_if(SYMBOL_KEYS.begin(), SYMBOL_KEYS.end(), [code](const KeyEntry& entry) { return entry.code == code; });
  return found == SYMBOL_KEYS.end() ? nullptr : &*found;
}

}  // namespace

int KeyboardApple2::asciiFor(std::string_view code) const {
  const bool control = controlLeft_ || controlRight_;
  if (const FunctionKey* function = findFunction(code)) return function->value;
  const int letter = letterIndex(code);
  if (letter >= 0) {
    if (control) return letter + 1;
    const bool upper = capsLock_ || shiftDown();
    return (upper ? 'A' : 'a') + letter;
  }
  const KeyEntry* entry = findSymbol(code);
  if (entry == nullptr) return -1;
  int value = shiftDown() ? entry->shifted : entry->plain;
  if (control) {
    // 数字 6 与 Ctrl 产生 $1E；括号、反斜线与（Shift 的）减号产生 $1B-$1D、$1F；
    // 其余数字与符号不变。
    if (code == "Digit6") return 0x1E;
    if (value == '[' || value == '\\' || value == ']' || value == '_') value &= 0x1F;
  }
  return value;
}

bool KeyboardApple2::setKey(std::string_view code, bool down) {
  if (code == "ShiftLeft") {
    shiftLeft_ = down;
    return true;
  }
  if (code == "ShiftRight") {
    shiftRight_ = down;
    return true;
  }
  if (code == "ControlLeft") {
    controlLeft_ = down;
    return true;
  }
  if (code == "ControlRight") {
    controlRight_ = down;
    return true;
  }
  if (code == "AltLeft") {
    openApple_ = down;
    return true;
  }
  if (code == "AltRight") {
    solidApple_ = down;
    return true;
  }
  if (code == "CapsLock") {
    if (down) capsLock_ = !capsLock_;
    return true;
  }
  const int value = asciiFor(code);
  if (value < 0) return false;
  const auto position = std::find(down_.begin(), down_.end(), code);
  if (!down) {
    if (position != down_.end()) down_.erase(position);
    return true;
  }
  if (position == down_.end()) down_.emplace_back(code);
  latch_ = static_cast<std::uint8_t>(value & 0x7F);
  strobe_ = true;
  return true;
}

bool KeyboardApple2::westernKeyDown() const { return std::find(down_.begin(), down_.end(), "F7") != down_.end(); }

void KeyboardApple2::releaseAll() {
  down_.clear();
  shiftLeft_ = false;
  shiftRight_ = false;
  controlLeft_ = false;
  controlRight_ = false;
  openApple_ = false;
  solidApple_ = false;
}

void KeyboardApple2::powerOn() {
  releaseAll();
  latch_ = 0;
  strobe_ = false;
  capsLock_ = true;
}

}  // namespace ember::logic::hardware
