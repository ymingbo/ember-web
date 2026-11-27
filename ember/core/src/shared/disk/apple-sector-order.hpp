/*
 * ------------------------------------------------------------------------
 * 名称：apple-sector-order.hpp
 * 说明：苹果 16 扇区软盘的扇区顺序表：DOS 3.3 顺序与 ProDOS 顺序各一张，把镜像里的扇区位置（逻辑扇区号）换成磁盘上的物理扇区号。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：软驱控制器按物理扇区编码磁道，媒体库按逻辑扇区读写文件系统，两边共用这两张表；
 *       .dsk 与 .do 镜像按 DOS 顺序排列扇区，.po 镜像按 ProDOS 顺序排列扇区。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>

namespace ember::shared {

/** 苹果软盘每道的扇区数。 */
inline constexpr unsigned APPLE_SECTORS_PER_TRACK = 16;

/** DOS 3.3 顺序：逻辑扇区号到物理扇区号。 */
inline constexpr std::array<std::uint8_t, APPLE_SECTORS_PER_TRACK> APPLE_DOS_ORDER = {0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15};

/** ProDOS 顺序：逻辑扇区号到物理扇区号。 */
inline constexpr std::array<std::uint8_t, APPLE_SECTORS_PER_TRACK> APPLE_PRODOS_ORDER = {0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15};

}  // namespace ember::shared
