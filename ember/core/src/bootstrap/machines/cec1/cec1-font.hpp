/*
 * ------------------------------------------------------------------------
 * 名称：cec1-font.hpp
 * 说明：中华学习机 CEC-I 的内置汉字字库声明：没有接 U33、U34 字库芯片时汉字程序读这份字模。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：数据在 cec1-font.cpp，由 tools/dev/gen-cec1-font.mjs 从 GNU Unifont 的点阵生成，不手改；
 *       布局与汉字程序的寻址一致：字模序号 N 占 32 字节（16 行，每行左半、右半各一个字节，
 *       字节的位 0 在最左），序号 N 为 ASCII 码（$20-$7E），或 256 + 94 × 区序号 + 位 - 1
 *       （区 1 到 9 的区序号是 0 到 8，区 16 到 87 的是 9 到 80）；ASCII 只用左半；
 *       数组下标是 N × 32 + 字节号，就是汉字程序经 $4000-$7FFF 读到的内容
 *       （位序已是 CPU 看到的）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ember::bootstrap::cec1_font {

/** 字库的大小：16 个 16K 的页。 */
inline constexpr std::size_t FONT_SIZE = 256 * 1024;

extern const std::array<std::uint8_t, FONT_SIZE> HANZI_FONT;

}  // namespace ember::bootstrap::cec1_font
