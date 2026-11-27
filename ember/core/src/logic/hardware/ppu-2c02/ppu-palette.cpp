/*
 * ------------------------------------------------------------------------
 * 名称：ppu-palette.cpp
 * 说明：2C02 调色板 RGB 表的生成：64 项固定颜色，再按颜色强调位对三个通道各乘一个衰减系数。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/ppu-2c02/ppu-palette.hpp"

namespace ember::logic::hardware {

namespace {

/** 64 项颜色，每项为 0xRRGGBB，下标是颜色号。 */
constexpr std::array<std::uint32_t, 64> BASE_COLORS = {
    0x7F7F7F, 0x2000B0, 0x2800B8, 0x6010A0, 0x982078, 0xB01030, 0xA03000, 0x784000, 0x485800, 0x386800, 0x386C00, 0x306040, 0x305080, 0x000000, 0x000000, 0x000000,
    0xBCBCBC, 0x4060F8, 0x4040FF, 0x9040F0, 0xD840C0, 0xD84060, 0xE05000, 0xC07000, 0x888800, 0x50A000, 0x48A810, 0x48A068, 0x4090C0, 0x000000, 0x000000, 0x000000,
    0xDFDFDF, 0x60A0FF, 0x5080FF, 0xA070FF, 0xF060FF, 0xFF60B0, 0xFF7830, 0xFFA000, 0xE8D020, 0x98E800, 0x70F040, 0x70E090, 0x60D0E0, 0x606060, 0x000000, 0x000000,
    0xFFFFFF, 0x90D0FF, 0xA0B8FF, 0xC0B0FF, 0xE0B0FF, 0xFFB8E8, 0xFFC8B8, 0xFFD8A0, 0xFFF090, 0xC8F080, 0xA0F0A0, 0xA0FFC8, 0x80D0FF, 0xA0A0A0, 0x000000, 0x000000,
};

/** 颜色强调位的 8 种组合（下标是 $2001 的位 7-5）对 R、G、B 的衰减系数。 */
constexpr float EMPHASIS_FACTORS[8][3] = {
    {1.00F, 1.00F, 1.00F}, {1.00F, 0.80F, 0.73F}, {0.73F, 1.00F, 0.70F}, {0.76F, 0.78F, 0.58F},
    {0.86F, 0.80F, 1.00F}, {0.83F, 0.68F, 0.85F}, {0.67F, 0.77F, 0.83F}, {0.68F, 0.68F, 0.68F},
};

}  // namespace

std::array<std::uint32_t, 512> buildPpuPalette() {
  std::array<std::uint32_t, 512> table{};
  for (std::size_t emphasis = 0; emphasis < 8; ++emphasis) {
    for (std::size_t entry = 0; entry < 64; ++entry) {
      const std::uint32_t color = BASE_COLORS[entry];
      const auto red = static_cast<std::uint32_t>(EMPHASIS_FACTORS[emphasis][0] * static_cast<float>((color >> 16) & 0xFFU));
      const auto green = static_cast<std::uint32_t>(EMPHASIS_FACTORS[emphasis][1] * static_cast<float>((color >> 8) & 0xFFU));
      const auto blue = static_cast<std::uint32_t>(EMPHASIS_FACTORS[emphasis][2] * static_cast<float>(color & 0xFFU));
      table[emphasis * 64 + entry] = red | (green << 8) | (blue << 16) | 0xFF000000U;
    }
  }
  return table;
}

}  // namespace ember::logic::hardware
