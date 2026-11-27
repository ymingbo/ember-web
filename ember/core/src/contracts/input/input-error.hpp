/*
 * ------------------------------------------------------------------------
 * 名称：input-error.hpp
 * 说明：input 契约：错误码。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：错误码以模块名 input 开头，取值与 schema/input.schema.json 登记的一致。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::input {

/** 没有开机，或主机没有这个手柄口。 */
inline constexpr std::string_view ERROR_INVALID_PORT = "input.invalidPort";

/** 手柄绑定不合格：槽位数不对、同一个键或同一个物理键绑定了多次等。 */
inline constexpr std::string_view ERROR_INVALID_BINDINGS = "input.invalidBindings";

}  // namespace ember::contracts::input
