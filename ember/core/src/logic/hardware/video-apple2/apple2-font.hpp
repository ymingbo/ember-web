/*
 * ------------------------------------------------------------------------
 * 名称：apple2-font.hpp
 * 说明：苹果 II 类文本屏的字模表声明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：字模数据在 apple2-font.cpp，由 tools/dev/gen-apple2-font.mjs 生成，不手改。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ember::logic::hardware::apple2 {

/** 字模表里的字符个数：ASCII $20 到 $7F。 */
inline constexpr std::size_t FONT_GLYPH_COUNT = 96;
/** 一个字符的行数。 */
inline constexpr std::size_t FONT_GLYPH_HEIGHT = 8;

/**
 * 字模：每个字符 8 行，每行的低 7 位是点阵（位 0 在最左，1 为亮点）；下标是（字符码 - $20）
 * × 8 + 行。
 */
extern const std::array<std::uint8_t, FONT_GLYPH_COUNT * FONT_GLYPH_HEIGHT> TEXT_FONT;

}  // namespace ember::logic::hardware::apple2
