/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-bbg.hpp
 * 说明：步步高键盘：矩阵键盘，接在 $4016、$4017 上，经 $4016 选扫描位置与半边、$4017 读 4 个键的状态；键位按 PC 键盘的物理位置对应键位名。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：接口：写 $4016 时只有位 2 为 1 才是扫描控制：$05 回到起点（位置 0，取“后半边”），$04 位置加 1（超过 13 回到 0）并翻转半边，$06 只翻转半边；
 *       位置 1-13 每个位置有两个半边（前半边在位置刚加 1 之后，后半边再翻转一次之后），每个半边 4 个键，读 $4017 位 1-4，按下为 0，没有键按下时为 1；
 *       位置 0 没有键；位置 10 的前半边没有键，后半边的第 1 个键恒为按下（硬件上这个位置没有读取，固定值只为保持矩阵完整）；
 *       固件每个位置先写 $04 读前半边，再写 $06 读后半边，位置 9 之后多发一对 $04、$06 跳过位置 10；
 *       本模块只管矩阵与键位，不挂总线，由主机装配里的输入口接到 $4016、$4017；键位名取 KeyboardEvent.code，一个键位名只对应一个矩阵位置，左右两个 Shift、Alt、Ctrl 各对应同一个键。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ember::logic::hardware {

/** 步步高键盘。 */
class KeyboardBbg final {
 public:
  /** 有键的扫描位置个数（位置 1 到 13）。 */
  static constexpr unsigned POSITIONS = 13;
  /** 读 $4017 时本键盘驱动的位（位 1-4）。 */
  static constexpr std::uint8_t DATA_MASK = 0x1E;

  KeyboardBbg();

  /** 写 $4016：只处理位 2 为 1 的写入。 */
  void write(std::uint8_t value);

  /** 读 $4017 的位 1-4（其余位为 0）：按下的键为 0，没有键按下为 1。 */
  [[nodiscard]] std::uint8_t read() const;

  /**
   * 按下或松开一个键。
   * @param code 物理键位，取 KeyboardEvent.code，如 KeyA、Enter、F8。
   * @param down 是否按下。
   * @return 键盘上有这个键位返回 true；没有时什么也不做，返回 false。
   */
  bool setKey(std::string_view code, bool down);

  /** 松开全部按键。 */
  void releaseAll();

  /** 复位扫描位置与半边（上电与复位时）；按键保持。 */
  void reset();

  /** 当前扫描位置（0 到 13），供测试。 */
  [[nodiscard]] unsigned position() const { return position_; }
  /** 当前是否在前半边，供测试。 */
  [[nodiscard]] bool firstHalf() const { return firstHalf_; }

  /** 键位名对应的矩阵位置，没有这个键位返回 false；供测试。 */
  static bool locate(std::string_view code, unsigned& position, bool& firstHalf, unsigned& bit);

 private:
  /** 重新由按下的键位算出矩阵。 */
  void rebuildMatrix();

  /** 每个键位是否按下，下标同键位表。 */
  std::vector<bool> pressed_;
  /** 矩阵：下标 [位置][0 前半边 1 后半边]，每个半边 4 位，1 表示按下。 */
  std::array<std::array<std::uint8_t, 2>, POSITIONS + 1> matrix_{};
  unsigned position_{0};
  bool firstHalf_{false};
};

}  // namespace ember::logic::hardware
