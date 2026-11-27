/*
 * ------------------------------------------------------------------------
 * 名称：ppu-bus.hpp
 * 说明：硬件契约：PPU 总线，14 位地址、8 位数据。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：图案表地址（$0000-$1FFF）转给卡槽；名称表地址（$2000-$3EFF）按卡槽的显存控制线选择主板显存或卡带。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/hardware/bus.hpp"

namespace ember::contracts::hardware {

/** PPU 总线的地址宽度（位）。 */
inline constexpr unsigned PPU_BUS_ADDRESS_BITS = 14;

/** PPU 总线。 */
class PpuBus : public Bus {};

}  // namespace ember::contracts::hardware
