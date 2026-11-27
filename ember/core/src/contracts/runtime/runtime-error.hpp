/*
 * ------------------------------------------------------------------------
 * 名称：runtime-error.hpp
 * 说明：runtime 模块的错误码：运行控制与卡带装入失败时经 shared::CoreError 抛出，说明面向用户。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：错误码以模块名 runtime 开头，取值与 schema/runtime.schema.json 登记的一致。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::runtime {

/** 还没有装入卡带，没有主机可以上电或复位。 */
inline constexpr std::string_view ERROR_NO_CARTRIDGE = "runtime.noCartridge";
/** 主机没有通电，操作只在通电时有效。 */
inline constexpr std::string_view ERROR_NOT_POWERED = "runtime.notPowered";
/** 插件要求的主机型号在程序里没有实现。 */
inline constexpr std::string_view ERROR_HOST_UNSUPPORTED = "runtime.hostUnsupported";
/** 当前主机型号不支持调试。 */
inline constexpr std::string_view ERROR_DEBUG_UNSUPPORTED = "runtime.debugUnsupported";

}  // namespace ember::contracts::runtime
