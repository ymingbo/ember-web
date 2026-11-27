/*
 * ------------------------------------------------------------------------
 * 名称：cpu-bus.hpp
 * 说明：硬件契约：CPU 总线，16 位地址、8 位数据。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：主机型号规定固定的译码：片上寄存器与主板 RAM 挂在主机内，其余地址转给卡槽。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/hardware/bus.hpp"

namespace ember::contracts::hardware {

/** CPU 总线的地址宽度（位）。 */
inline constexpr unsigned CPU_BUS_ADDRESS_BITS = 16;

/** CPU 总线。 */
class CpuBus : public Bus {};

}  // namespace ember::contracts::hardware
