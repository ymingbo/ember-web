/*
 * ------------------------------------------------------------------------
 * 名称：apu-tables.hpp
 * 说明：2A03 APU 用到的常量表：长度计数器表、噪声周期表、DMC 速率表、方波占空比、三角波序列、帧计数器的步进表。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：数值是 NTSC 的；周期的单位是 CPU 周期。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>

namespace ember::logic::hardware::apu {

/** 长度计数器表：由 $4003/$4007/$400B/$400F 的高 5 位索引。 */
inline constexpr std::array<std::uint8_t, 32> LENGTH_TABLE = {10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
                                                              12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

/** 噪声通道的周期表（NTSC，CPU 周期）：由 $400E 的低 4 位索引。 */
inline constexpr std::array<std::uint16_t, 16> NOISE_PERIOD_TABLE = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};

/** DMC 的速率表（NTSC，每个输出时钟的 CPU 周期数）：由 $4010 的低 4 位索引。 */
inline constexpr std::array<std::uint16_t, 16> DMC_RATE_TABLE = {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};

/** 噪声通道的周期表（PAL，CPU 周期）。 */
inline constexpr std::array<std::uint16_t, 16> NOISE_PERIOD_TABLE_PAL = {4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778};

/** DMC 的速率表（PAL）。 */
inline constexpr std::array<std::uint16_t, 16> DMC_RATE_TABLE_PAL = {398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50};

/** 方波的四种占空比序列，8 步一圈。 */
inline constexpr std::array<std::array<std::uint8_t, 8>, 4> PULSE_DUTY = {{
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1},
}};

/** 三角波的 32 步序列。 */
inline constexpr std::array<std::uint8_t, 32> TRIANGLE_SEQUENCE = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

/** 帧计数器的一步对包络、线性计数器（四分之一帧）与长度计数器、扫频（半帧）的时钟。 */
enum class FrameClock : std::uint8_t { None, Quarter, Half };

/** 帧计数器每种模式的步数（4 步模式在 29830 周期处回到 0，也算一步）。 */
inline constexpr unsigned FRAME_STEP_COUNT = 6;

/** 帧计数器各步发生的周期（从 $4017 的写入生效起算）：[模式][步]，模式 0 为 4 步，模式 1 为 5 步。 */
inline constexpr std::array<std::array<std::uint32_t, FRAME_STEP_COUNT>, 2> FRAME_STEP_CYCLES = {{
    {7457, 14913, 22371, 29828, 29829, 29830},
    {7457, 14913, 22371, 29829, 37281, 37282},
}};

/** PAL 的帧计数器步进表（周期数是 APU 周期数乘 2）。 */
inline constexpr std::array<std::array<std::uint32_t, FRAME_STEP_COUNT>, 2> FRAME_STEP_CYCLES_PAL = {{
    {8313, 16627, 24939, 33252, 33253, 33254},
    {8313, 16627, 24939, 33253, 41565, 41566},
}};

/** 帧计数器各步的时钟种类：半帧同时也时钟四分之一帧的部件。 */
inline constexpr std::array<std::array<FrameClock, FRAME_STEP_COUNT>, 2> FRAME_STEP_CLOCKS = {{
    {FrameClock::Quarter, FrameClock::Half, FrameClock::Quarter, FrameClock::None, FrameClock::Half, FrameClock::None},
    {FrameClock::Quarter, FrameClock::Half, FrameClock::Quarter, FrameClock::None, FrameClock::Half, FrameClock::None},
}};

/** 4 步模式里置位帧 IRQ 标志的第一步（之后连续三个周期都置位）。 */
inline constexpr unsigned FRAME_IRQ_FIRST_STEP = 3;

}  // namespace ember::logic::hardware::apu
