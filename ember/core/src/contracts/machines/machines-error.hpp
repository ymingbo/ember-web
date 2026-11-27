/*
 * ------------------------------------------------------------------------
 * 名称：machines-error.hpp
 * 说明：machines 模块的错误码：机型不存在、不需要或尚未支持 ROM、ROM 文件读取与校验失败、没有添加 ROM，经 shared::CoreError 抛出，说明面向用户。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：错误码以模块名 machines 开头。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::machines {

/** 没有这个机型。 */
inline constexpr std::string_view ERROR_NOT_FOUND = "machines.notFound";
/** 这个机型不需要 ROM（fc）。 */
inline constexpr std::string_view ERROR_ROM_NOT_NEEDED = "machines.romNotNeeded";
/** 这个机型程序里还没有实现。 */
inline constexpr std::string_view ERROR_UNSUPPORTED = "machines.unsupported";
/** ROM 源文件不存在。 */
inline constexpr std::string_view ERROR_FILE_NOT_FOUND = "machines.fileNotFound";
/** ROM 源文件无法读取：不是普通文件、没有权限或读取中途失败。 */
inline constexpr std::string_view ERROR_FILE_UNREADABLE = "machines.fileUnreadable";
/** ROM 文件的格式或大小不符合这个机型。 */
inline constexpr std::string_view ERROR_ROM_INVALID = "machines.romInvalid";
/** 无法读写、删除 ROM 目录里的文件。 */
inline constexpr std::string_view ERROR_ROM_ACCESS_FAILED = "machines.romAccessFailed";
/** 这个机型开机必须有卡带（fc），改用 cartridges.load。 */
inline constexpr std::string_view ERROR_CARTRIDGE_REQUIRED = "machines.cartridgeRequired";
/** 这个机型还没有添加 ROM。 */
inline constexpr std::string_view ERROR_NO_ROM = "machines.noRom";

}  // namespace ember::contracts::machines
