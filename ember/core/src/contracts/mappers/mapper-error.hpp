/*
 * ------------------------------------------------------------------------
 * 名称：mapper-error.hpp
 * 说明：mapper 模块的错误码：插件宿主与插件管理共用，经 shared::CoreError 抛出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：错误码以模块名 mappers 开头，说明面向用户；前六个见 docs/03-mapper插件规范.md 第 7 节，后两个是插件管理增加的。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::mappers {

/** 没有任何已启用的插件匹配这个卡带。 */
inline constexpr std::string_view ERROR_NO_MATCH = "mappers.noMatch";
/** 多个已启用的插件同样匹配这个卡带，无法选择。 */
inline constexpr std::string_view ERROR_CONFLICT = "mappers.conflict";
/** 插件包不合格：包格式、清单或脚本导出不符合规范。 */
inline constexpr std::string_view ERROR_INVALID_PACKAGE = "mappers.invalidPackage";
/** 清单的 host 里没有任何已有的主机型号。 */
inline constexpr std::string_view ERROR_HOST_UNKNOWN = "mappers.hostUnknown";
/** 脚本抛出异常或返回了不合法的值。 */
inline constexpr std::string_view ERROR_SCRIPT_ERROR = "mappers.scriptError";
/** 脚本超出内存、栈或单次回调时长的限制。 */
inline constexpr std::string_view ERROR_LIMIT_EXCEEDED = "mappers.limitExceeded";
/** 没有这个标识的插件。 */
inline constexpr std::string_view ERROR_NOT_FOUND = "mappers.notFound";
/** 预装的插件只能停用，不能卸载。 */
inline constexpr std::string_view ERROR_BUNDLED_READONLY = "mappers.bundledReadonly";
/** 开发目录里的插件不能卸载，从开发目录里删除它的目录。 */
inline constexpr std::string_view ERROR_DEVELOPMENT_READONLY = "mappers.developmentReadonly";

}  // namespace ember::contracts::mappers
