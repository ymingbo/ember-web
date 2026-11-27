/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-fc.hpp
 * 说明：Family BASIC 键盘（HVC-007）：72 键的矩阵键盘，接在经典 FC 的扩展口上，经 $4016 选行选列、$4017 读键状态；键位按 JIS 键盘的物理位置对应 PC 的键位名。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：接口：写 $4016：位 0 把行计数器复位到第 0 行，位 1 选列（由高变低时行加 1，第 10 行之后回到第 0 行），位 2 使能矩阵
 *       （为 0 时读出恒为 0）；读 $4017 位 1-4：当前行与列的 4 个键的状态，按下为 0；矩阵 9 行，第 10 行没有键，使能时读出 $1E；
 *       Family BASIC 每次选通列之后等约 50 个周期再读，这里不模拟电容延迟，写入后立即有效；
 *       本模块只管矩阵与键位，不挂总线，由主机装配里的 FcExpansionPort 接到 $4016、$4017；键位只属于这个键盘，不随卡带的 mapper 变化，学习机的键盘是另一个设备。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace ember::logic::hardware {

/** Family BASIC 键盘。 */
class KeyboardFc final {
 public:
  /** 矩阵的行数（不含没有键的第 10 行）。 */
  static constexpr unsigned ROWS = 9;
  /** 读 $4017 时本键盘驱动的位（位 1-4）。 */
  static constexpr std::uint8_t DATA_MASK = 0x1E;

  /** 写 $4016。 */
  void write(std::uint8_t value);

  /** 读 $4017 的位 1-4（其余位为 0）：按下的键为 0；没有使能时全为 0。 */
  [[nodiscard]] std::uint8_t read() const;

  /**
   * 按下或松开一个键。
   * @param code 物理键位，取 KeyboardEvent.code，如 KeyA、Enter、F8。
   * @param down 是否按下。
   * @return 键盘上有这个键位返回 true；没有时什么也不做，返回 false。
   */
  bool setKey(std::string_view code, bool down);

  /** 松开全部按键。 */
  void releaseAll() { rows_.fill(0); }

  /** 复位行列计数器与使能（上电与复位时）；按键保持。 */
  void reset();

  /** 当前选中的行（0 到 9），供测试。 */
  [[nodiscard]] unsigned row() const { return row_; }

 private:
  /** 每行 8 位：位 0-3 是列 0 的 $4017 位 1-4，位 4-7 是列 1 的。 */
  std::array<std::uint8_t, ROWS> rows_{};
  unsigned row_{0};
  bool column_{false};
  bool enabled_{false};
};

}  // namespace ember::logic::hardware
