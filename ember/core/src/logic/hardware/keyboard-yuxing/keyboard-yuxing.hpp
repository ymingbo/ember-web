/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-yuxing.hpp
 * 说明：裕兴学习机的键盘：14 行 8 列的矩阵，行选择由两个寄存器给出，读数据口得到所有被选中行的键状态之和；键位按 PC 键盘的物理位置给出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：接口：行选择的低 8 位（selectLow）与高 6 位（selectHigh，只取低 6 位），位 n 为 1 表示选中第 n 行；
 *       读（read）：被选中的各行里按下的键按列取或，位 n 是列 n，按下为 1；没有选中任何行时读出 0；
 *       本模块只管矩阵与键位，不挂总线，由裕兴主机装配把它接到数据口与两个行选择口；
 *       左右 Shift、Ctrl、Alt 各占同一个矩阵位置：两个键都按下时松开一个，位置仍然是按下的。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ember::logic::hardware {

/** 裕兴学习机的键盘。 */
class KeyboardYuxing final {
 public:
  /** 矩阵的行数。 */
  static constexpr unsigned ROWS = 14;
  /** 矩阵的列数。 */
  static constexpr unsigned COLUMNS = 8;

  KeyboardYuxing();

  /** 行选择的低 8 位（第 0 到 7 行）。 */
  void selectLow(std::uint8_t mask) { rowSelect_ = static_cast<std::uint16_t>((rowSelect_ & 0x3F00U) | mask); }
  /** 行选择的高位（第 8 到 13 行，只取低 6 位）。 */
  void selectHigh(std::uint8_t mask) { rowSelect_ = static_cast<std::uint16_t>((rowSelect_ & 0x00FFU) | ((mask & 0x3FU) << 8)); }

  /** 被选中的各行里按下的键，按列取或。 */
  [[nodiscard]] std::uint8_t read() const;

  /** 按下或松开一个键，返回键位表里有没有这个键。 */
  bool setKey(std::string_view code, bool down);
  void releaseAll();
  /** 行选择清零（上电与复位时）；按键状态不动。 */
  void reset() { rowSelect_ = 0; }

  /** 当前的行选择，供测试。 */
  [[nodiscard]] std::uint16_t rowSelect() const { return rowSelect_; }

 private:
  /** 重新由各键的状态算出矩阵。 */
  void rebuildRows();

  /** 键位表里每个键位名是否按下。 */
  std::vector<bool> down_;
  /** 每行 8 位，位 n 是列 n 的键。 */
  std::array<std::uint8_t, ROWS> rows_{};
  std::uint16_t rowSelect_{0};
};

}  // namespace ember::logic::hardware
