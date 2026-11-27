/*
 * ------------------------------------------------------------------------
 * 名称：known-roms.cpp
 * 说明：已知的整机机型 ROM 表与查询的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "logic/machines/known-roms.hpp"

#include <algorithm>
#include <array>

namespace ember::logic::machines {

namespace {

/** 已知 ROM 表：校验值取自各机型的真实 ROM 文件。 */
constexpr std::array<KnownRom, 16> KNOWN_ROMS{{
    {0x40A4C574U, "sb97", "SB97"},
    {0xE07BD2C3U, "sb2000", "SB2000"},
    {0xB3D11A3CU, "bbg1", "软驱一号"},
    {0x8C8FE800U, "bbg98", "98型"},
    {0xFE31765BU, "pec9588", "PEC-9588"},
    {0x6EEF8BB7U, "pec586", "PEC-586"},
    {0x05F958C4U, "pec586", "PEC-686F"},
    {0x3DF65263U, "yuxing", "V7.0"},
    {0x6BA6BD80U, "yuxing", "V8.0-C"},
    {0x2B1B969EU, "yuxing", "V8.2-D"},
    {0xA70FD0F3U, "yuxing", "V8.3-D"},
    {0x6085FEE8U, "yuxing", "V9.0-98"},
    {0x2A1E4D89U, "yuxing", "V9.2-98"},
    {0x5C6CE13EU, "yuxing", "V9.2-F"},
    {0x23C31D9FU, "cec1", "V1.1a"},
    {0xD6F01F26U, "cec1", "V1.21"},
}};

}  // namespace

std::optional<KnownRom> findKnownRom(std::uint32_t crc32) {
  const auto found = std::find_if(KNOWN_ROMS.begin(), KNOWN_ROMS.end(), [crc32](const KnownRom& rom) { return rom.crc32 == crc32; });
  if (found == KNOWN_ROMS.end()) return std::nullopt;
  return *found;
}

std::string romLabel(std::uint32_t crc32, const std::string& fileName) {
  const auto rom = findKnownRom(crc32);
  if (rom) return std::string(rom->version);
  return fileName;
}

}  // namespace ember::logic::machines
