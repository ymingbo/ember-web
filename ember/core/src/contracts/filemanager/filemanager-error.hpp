/*
 * ------------------------------------------------------------------------
 * 名称：filemanager-error.hpp
 * 说明：filemanager 模块的错误码：镜像文件读写失败、镜像格式不认识、条目不存在或重名、名称不合法、文件系统不支持的操作、空间不足、撤销，经 shared::CoreError 抛出，说明面向用户。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：错误码以模块名 filemanager 开头；说明会原样显示在管理窗口的提示层里，所以要简短。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::filemanager {

/** 还没有设置目录。 */
inline constexpr std::string_view ERROR_NO_ROOT = "filemanager.noRoot";
/** 路径不合格：不在目录里、含上级目录或不是支持的镜像文件。 */
inline constexpr std::string_view ERROR_INVALID_PATH = "filemanager.invalidPath";
/** 镜像文件不存在。 */
inline constexpr std::string_view ERROR_IMAGE_NOT_FOUND = "filemanager.imageNotFound";
/** 镜像文件无法读取或超过大小上限。 */
inline constexpr std::string_view ERROR_IMAGE_UNREADABLE = "filemanager.imageUnreadable";
/** 镜像的文件系统格式不认识，无法查看。 */
inline constexpr std::string_view ERROR_UNKNOWN_FORMAT = "filemanager.unknownFormat";
/** 镜像只读：位流格式（.nib、.woz）、格式暂不支持修改，或软驱里的盘片写保护。 */
inline constexpr std::string_view ERROR_READ_ONLY = "filemanager.readOnly";
/** 这种文件系统不支持这个操作（如 DOS 3.3 没有子目录）。 */
inline constexpr std::string_view ERROR_UNSUPPORTED = "filemanager.unsupported";
/** 镜像里没有这个条目。 */
inline constexpr std::string_view ERROR_ENTRY_NOT_FOUND = "filemanager.entryNotFound";
/** 同一个目录里已有同名条目。 */
inline constexpr std::string_view ERROR_EXISTS = "filemanager.exists";
/** 名称不合法：太长、含不允许的字符或为空。 */
inline constexpr std::string_view ERROR_INVALID_NAME = "filemanager.invalidName";
/** 磁盘上没有足够的空闲空间，或目录已满。 */
inline constexpr std::string_view ERROR_NO_SPACE = "filemanager.noSpace";
/** 不能把文件夹移动到它自己里面。 */
inline constexpr std::string_view ERROR_MOVE_INTO_SELF = "filemanager.moveIntoSelf";
/** 镜像内容改写失败（写回文件、写进软驱里的盘片）。 */
inline constexpr std::string_view ERROR_SAVE_FAILED = "filemanager.saveFailed";
/** 这个镜像没有可以撤销的操作。 */
inline constexpr std::string_view ERROR_NOTHING_TO_UNDO = "filemanager.nothingToUndo";
/** 镜像内容已被别的程序或机器改动，撤销会覆盖这些改动，所以不撤销。 */
inline constexpr std::string_view ERROR_UNDO_BLOCKED = "filemanager.undoBlocked";
/** 文件系统的数据结构已损坏，操作中止。 */
inline constexpr std::string_view ERROR_CORRUPT = "filemanager.corrupt";
/** 同名文件已存在，不能新建镜像。 */
inline constexpr std::string_view ERROR_IMAGE_EXISTS = "filemanager.imageExists";
/** 拖入的文件已失效，要重新拖入。 */
inline constexpr std::string_view ERROR_DROPPED_NOT_FOUND = "filemanager.droppedNotFound";
/** 无法在文件管理器里显示这个文件。 */
inline constexpr std::string_view ERROR_REVEAL_UNAVAILABLE = "filemanager.revealUnavailable";
/** 镜像正插在软驱里（或所在的文件夹里有镜像插在软驱里），不能改名、移动或删除。 */
inline constexpr std::string_view ERROR_IN_USE = "filemanager.inUse";

}  // namespace ember::contracts::filemanager
