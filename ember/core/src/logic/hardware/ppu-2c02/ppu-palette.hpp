/*
 * ------------------------------------------------------------------------
 * 名称：ppu-palette.hpp
 * 说明：2C02 调色板的 RGB 表：64 项固定颜色，按三个颜色强调位的 8 种组合各衰减一份。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>

namespace ember::logic::hardware {

/**
 * 生成调色板的 RGBA 表。
 * @return 512 项：下标为 强调位（$2001 的位 7-5，0 到 7）* 64 + 颜色号（0 到 63）；
 *         每项为内存中依次 R、G、B、A 的 32 位整数（小端机器上为 0xAABBGGRR），A 为 255。
 */
[[nodiscard]] std::array<std::uint32_t, 512> buildPpuPalette();

}  // namespace ember::logic::hardware
