/*
 * ------------------------------------------------------------------------
 * 名称：crc32.hpp
 * 说明：CRC-32（IEEE 802.3，与 zip、PNG 相同）的计算，用于卡带 PRG 与 PRG 加 CHR 的校验值、整机机型 ROM 的识别。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace ember::shared {

namespace detail {

/** 反射多项式 0xEDB88320 的查表。 */
constexpr std::array<std::uint32_t, 256> makeCrc32Table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t index = 0; index < 256; ++index) {
    std::uint32_t value = index;
    for (int bit = 0; bit < 8; ++bit) value = (value & 1U) != 0 ? (value >> 1) ^ 0xEDB88320U : value >> 1;
    table[index] = value;
  }
  return table;
}

inline constexpr std::array<std::uint32_t, 256> CRC32_TABLE = makeCrc32Table();

}  // namespace detail

/**
 * 在已有的校验值之后继续计算。
 * @param previous 前一段数据的校验值；从头开始时传 0。
 * @param data 后续的数据。
 * @return 前后两段数据拼起来的校验值。
 */
[[nodiscard]] constexpr std::uint32_t crc32Continue(std::uint32_t previous, std::span<const std::uint8_t> data) noexcept {
  std::uint32_t crc = ~previous;
  for (const std::uint8_t byte : data) crc = detail::CRC32_TABLE[(crc ^ byte) & 0xFFU] ^ (crc >> 8);
  return ~crc;
}

/** 一段数据的校验值。 */
[[nodiscard]] constexpr std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept { return crc32Continue(0, data); }

}  // namespace ember::shared
