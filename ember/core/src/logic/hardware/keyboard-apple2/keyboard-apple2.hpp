/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-apple2.hpp
 * 说明：苹果 II 类机器的 ASCII 键盘：按键产生 7 位 ASCII 码锁存下来，
 *       程序读 $C000 取走（位 7 为“还没读过”），访问 $C010 清除这个标志；
 *       键位按 PC 键盘的物理位置对应键位名。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：字母键：大小写锁定打开时（上电默认打开）总是大写，关闭时按住 Shift 为大写；Ctrl 与字母组合
 *       产生控制码，Ctrl 与 6、[ \\ ] 和 Shift 的减号产生 $1E、$1B-$1D、$1F，其余数字与符号不变；
 *       Backspace 与左方向键都是 $08，右方向键 $15，上 $0B、下 $0A，Delete $7F，Enter $0D，
 *       Esc $1B，Tab $09；
 *       功能键的键码固定：F1 $01（字母方式）、F2 $0C（拼音）、F3 $17（区位）、F4 $14、F5 $06
 *       （用户扩充）、F6 $1F（中文）、F7 $11（西文）、F8 $16（自检 TEST）、Pause $03（QUIT）；
 *       F7 也可以经 westernKeyDown 读到；
 *       Alt（左）与 Alt（右）是按键输入 0 与 1（开苹果与关苹果），Shift 也可以经 shiftDown 读到；
 *       按键按着不放时，输入事件重复到来，每次都重新锁存一次，起到自动重复的作用；
 *       本模块不挂总线，由主机装配里的主板接到 $C000 一带。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ember::logic::hardware {

/** 苹果 II 类机器的键盘。 */
class KeyboardApple2 final {
 public:
  KeyboardApple2() = default;

  /**
   * 按下或松开一个键。
   * @param code 物理键位，取 KeyboardEvent.code，如 KeyA、Enter。
   * @param down 是否按下。
   * @return 键盘上有这个键位返回 true；没有时什么也不做，返回 false。
   */
  bool setKey(std::string_view code, bool down);

  /** $C000 读出的值：低 7 位是最后一个按键的 ASCII 码，位 7 为 1 表示还没被读过（按键标志）。 */
  [[nodiscard]] std::uint8_t data() const { return static_cast<std::uint8_t>(latch_ | (strobe_ ? 0x80U : 0U)); }

  /** 访问 $C010：清除按键标志。 */
  void clearStrobe() { strobe_ = false; }

  /** 现在是否有按键按着（不含 Shift、Ctrl、Alt、大小写锁定）。 */
  [[nodiscard]] bool anyKeyDown() const { return !down_.empty(); }

  /** 按键输入 0 与 1（开苹果与关苹果，对应左 Alt 与右 Alt）是否按下。 */
  [[nodiscard]] bool openApple() const { return openApple_; }
  [[nodiscard]] bool solidApple() const { return solidApple_; }

  /** Shift 是否按着。 */
  [[nodiscard]] bool shiftDown() const { return shiftLeft_ || shiftRight_; }

  /** “西文”键（F7）是否按着：复位时连着 $C063 读到，1.21 的 ROM 据此冷启动。 */
  [[nodiscard]] bool westernKeyDown() const;

  /** 大小写锁定是否打开。 */
  [[nodiscard]] bool capsLock() const { return capsLock_; }

  /** 松开全部按键（大小写锁定不变）。 */
  void releaseAll();

  /** 上电：清掉锁存的按键与标志，大小写锁定回到打开。 */
  void powerOn();

  /**
   * 键位名对应的 ASCII 码（用当前的 Shift、Ctrl 与大小写锁定状态）；没有这个键位返回 -1，供测试。
   */
  [[nodiscard]] int asciiFor(std::string_view code) const;

 private:
  std::uint8_t latch_{0};
  bool strobe_{false};
  bool capsLock_{true};
  bool shiftLeft_{false};
  bool shiftRight_{false};
  bool controlLeft_{false};
  bool controlRight_{false};
  bool openApple_{false};
  bool solidApple_{false};
  /** 按着的非修饰键。 */
  std::vector<std::string> down_;
};

}  // namespace ember::logic::hardware
