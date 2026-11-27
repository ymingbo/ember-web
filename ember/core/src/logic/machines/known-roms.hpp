/*
 * ------------------------------------------------------------------------
 * 名称：known-roms.hpp
 * 说明：已知的整机机型 ROM：按 ROM 内容的 CRC-32 认出一个 ROM 属于哪个机型、是哪个版本，供“从目录添加”与状态栏显示的 ROM 版本使用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：同一个机型有多个版本时（pec586、yuxing）版本名区分它们；每个机型只有一种 ROM 时版本名就是型号名（如 98型）；
 *       不在表里的 ROM 仍可以手动添加，只是认不出机型与版本。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ember::logic::machines {

/** 表里的一个 ROM。 */
struct KnownRom {
  /** ROM（纯 bin 文件）内容的 CRC-32。 */
  std::uint32_t crc32;
  /** 机型标识。 */
  std::string_view machine;
  /** 版本名，如 V9.0-98；这个机型只有一种 ROM 时是型号名，如 98型。 */
  std::string_view version;
};

/** 按 PRG 的 CRC-32 查表；认不出时没有值。 */
[[nodiscard]] std::optional<KnownRom> findKnownRom(std::uint32_t crc32);

/** 界面显示的 ROM 版本：认得出的 ROM 是版本名，认不出时是文件名。 */
[[nodiscard]] std::string romLabel(std::uint32_t crc32, const std::string& fileName);

}  // namespace ember::logic::machines
