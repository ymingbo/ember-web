/*
 * ------------------------------------------------------------------------
 * 名称：io-address.hpp
 * 说明：调试里“寄存器访问”的判定：I/O 记录与事件记录共用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-24
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/access-probe.hpp"

namespace ember::logic::hardware {

/** 芯片、手柄口与卡带扩展寄存器的读写（$2000-$5FFF），以及对 $8000 以上的写（mapper 寄存器）。 */
inline bool isIoAddress(contracts::hardware::AccessKind kind, std::uint16_t address) {
  if (address >= 0x2000 && address <= 0x5FFF) return true;
  return kind == contracts::hardware::AccessKind::Write && address >= 0x8000;
}

}  // namespace ember::logic::hardware
