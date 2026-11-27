/*
 * ------------------------------------------------------------------------
 * 名称：cartridge-error.hpp
 * 说明：cartridges 模块的错误码：NES 文件读取与解析失败、卡槽为空时经 shared::CoreError 抛出，说明面向用户。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：错误码以模块名 cartridges 开头。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::cartridges {

/** 文件不是 iNES 或 NES 2.0 格式：开头的魔数不对。 */
inline constexpr std::string_view ERROR_NOT_INES = "cartridges.notInes";
/** 文件被截断：比文件头声明的内容短。 */
inline constexpr std::string_view ERROR_TRUNCATED = "cartridges.truncated";
/** 不支持的格式：UNIF、FDS，或文件头里无法处理的取值。 */
inline constexpr std::string_view ERROR_UNSUPPORTED_FORMAT = "cartridges.unsupportedFormat";
/** 文件头不合法：如 PRG 大小为 0。 */
inline constexpr std::string_view ERROR_INVALID_HEADER = "cartridges.invalidHeader";
/** 文件不存在。 */
inline constexpr std::string_view ERROR_FILE_NOT_FOUND = "cartridges.fileNotFound";
/** 文件无法读取：不是普通文件、没有权限或读取中途失败。 */
inline constexpr std::string_view ERROR_FILE_UNREADABLE = "cartridges.fileUnreadable";
/** 卡槽是空的：没有插入卡带。 */
inline constexpr std::string_view ERROR_EMPTY = "cartridges.empty";

}  // namespace ember::contracts::cartridges
