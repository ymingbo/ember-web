/*
 * ------------------------------------------------------------------------
 * 名称：pyramid-text.hpp
 * 说明：金字塔学习机的汉字显示电路：打开后，背景图块的图案按“一格 16 字节、左右两列各取一半”的单色字模读取，不含存储。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：汉字模式开关由主板寄存器的位 7 给出；
 *       背景图块的取指顺序是名称表字节、属性字节、图案低位平面、图案高位平面，电路据此认出“紧跟在属性字节之后的两次图案表读取”是背景的，精灵的图案读取之前没有属性字节，不改写；
 *       汉字模式下背景的这两次读取改读同一个字节：图案地址的位 12 取名称表地址的位 9（屏幕上半 16 行用图案表 0，下半用图案表 1），位 3 取名称表地址的位 0（偶数列取每格的前 8 字节，奇数列取后 8 字节），
 *       其余位照旧（图块号与图块内的行），所以两个位平面得到同一个字节，每个像素只有颜色 0 与 3；
 *       改写只影响 PPU 的取指，不改变图案存储区里的内容；本模块不挂总线，由金字塔主板在 PPU 侧每次读之前先 observe、再 remap。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::logic::hardware {

/** 金字塔学习机的汉字显示电路。 */
class PyramidText final {
 public:
  /** 上电：汉字模式关闭。 */
  void reset();

  /** 汉字模式开关。 */
  void setTextMode(bool on) { textMode_ = on; }
  [[nodiscard]] bool textMode() const { return textMode_; }

  /** PPU 取指：记住最近的名称表地址，并在属性字节之后标记接下来两次图案表读取是背景的。 */
  void observe(std::uint16_t address);

  /** 汉字模式下，背景的图案表读取改读由名称表地址决定的字节；其他情况原样返回。 */
  [[nodiscard]] std::uint16_t remap(std::uint16_t address) const;

 private:
  bool textMode_{false};
  /** 最近一次名称表字节的地址。 */
  std::uint16_t tileAddress_{0};
  /** 属性字节之后还没有读的背景图案读取次数（0 到 2）。 */
  std::uint8_t pendingPatterns_{0};
  /** 当前这次图案表读取是不是背景的。 */
  bool backgroundFetch_{false};
};

}  // namespace ember::logic::hardware
