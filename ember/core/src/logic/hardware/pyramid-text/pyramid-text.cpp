/*
 * ------------------------------------------------------------------------
 * 名称：pyramid-text.cpp
 * 说明：金字塔学习机汉字显示电路的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/pyramid-text/pyramid-text.hpp"

namespace ember::logic::hardware {

namespace {

/** 图案表与名称表的分界：$2000 以下是图案表。 */
constexpr std::uint16_t PATTERN_END = 0x2000;
/** 名称表页内属性表的起点：偏移 $3C0 及以上是属性字节。 */
constexpr std::uint16_t ATTRIBUTE_OFFSET = 0x3C0;
constexpr std::uint16_t PAGE_MASK = 0x3FF;
/** 一个图块背景要读的图案字节数：低位平面与高位平面。 */
constexpr std::uint8_t PATTERNS_PER_TILE = 2;
/** 图案地址的位 12（图案表选择）与位 3（图块内低位平面与高位平面的分界）。 */
constexpr std::uint16_t TABLE_BIT = 0x1000;
constexpr std::uint16_t PLANE_BIT = 0x0008;
/** 名称表地址的位 9（第 16 行起为屏幕下半）与位 0（列的奇偶）。 */
constexpr std::uint16_t LOWER_HALF_BIT = 0x0200;
constexpr std::uint16_t ODD_COLUMN_BIT = 0x0001;

}  // namespace

void PyramidText::reset() {
  textMode_ = false;
  tileAddress_ = 0;
  pendingPatterns_ = 0;
  backgroundFetch_ = false;
}

void PyramidText::observe(std::uint16_t address) {
  if (address < PATTERN_END) {
    backgroundFetch_ = pendingPatterns_ > 0;
    if (pendingPatterns_ > 0) --pendingPatterns_;
    return;
  }
  backgroundFetch_ = false;
  if ((address & PAGE_MASK) >= ATTRIBUTE_OFFSET) {
    pendingPatterns_ = PATTERNS_PER_TILE;
  } else {
    tileAddress_ = address;
    pendingPatterns_ = 0;
  }
}

std::uint16_t PyramidText::remap(std::uint16_t address) const {
  if (!textMode_ || !backgroundFetch_ || address >= PATTERN_END) return address;
  std::uint16_t remapped = static_cast<std::uint16_t>(address & ~(TABLE_BIT | PLANE_BIT));
  if ((tileAddress_ & LOWER_HALF_BIT) != 0) remapped = static_cast<std::uint16_t>(remapped | TABLE_BIT);
  if ((tileAddress_ & ODD_COLUMN_BIT) != 0) remapped = static_cast<std::uint16_t>(remapped | PLANE_BIT);
  return remapped;
}

}  // namespace ember::logic::hardware
