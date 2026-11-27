/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-pyramid.hpp
 * 说明：金字塔学习机的键盘：13 行 8 列的矩阵键盘，经 $4016 选行选列、$4017 位 1 逐列读键状态；键位按 PC 键盘的物理位置给出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：接口：写 $4016：0 或 1 回到第 0 行，2 回到第 0 列，6 或 7 使行加 1（超过第 12 行回到第 0 行），其他值不起作用；
 *       读 $4017 位 1：当前行与列的键按下为 1，随后列加 1；列超过 7 时没有键，读出 0；
 *       本模块只管矩阵与键位，不挂总线，由金字塔主机装配里的输入口接到 $4016、$4017。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ember::logic::hardware {

/** 金字塔学习机的键盘。 */
class KeyboardPyramid final {
 public:
  /** 矩阵的行数。 */
  static constexpr unsigned ROWS = 13;
  /** 矩阵的列数。 */
  static constexpr unsigned COLUMNS = 8;
  /** 读 $4017 时本键盘驱动的位（位 1）。 */
  static constexpr std::uint8_t DATA_MASK = 0x02;

  /** 写 $4016：选行选列的命令字。 */
  void write(std::uint8_t value);
  /** 读 $4017 位 1：当前行与列的键状态，随后列加 1。 */
  [[nodiscard]] std::uint8_t read();
  /** 按下或松开一个键，返回键位表里有没有这个键。 */
  bool setKey(std::string_view code, bool down);
  void releaseAll() { rows_.fill(0); }
  /** 行列计数器回到第 0 行第 0 列。 */
  void reset();

  /** 当前选中的行（0 到 12），供测试。 */
  [[nodiscard]] unsigned row() const { return row_; }
  /** 下一次读取的列，供测试。 */
  [[nodiscard]] unsigned column() const { return column_; }

 private:
  /** 每行 8 位，位 n 是列 n 的键。 */
  std::array<std::uint8_t, ROWS> rows_{};
  unsigned row_{0};
  unsigned column_{0};
};

}  // namespace ember::logic::hardware
