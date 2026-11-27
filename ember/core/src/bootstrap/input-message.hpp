/*
 * ------------------------------------------------------------------------
 * 名称：input-message.hpp
 * 说明：输入消息的解析：把界面发来的键盘、鼠标、游戏手柄消息（JSON 文本）校验后转成输入事件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：核心是最后一道校验：渲染进程与核心宿主各检查过一遍，这里不信任任何一侧。字段与范围见 src/contracts/stream-ports/stream-messages.ts。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string>

#include "contracts/hardware/input-event.hpp"

namespace ember::bootstrap {

/**
 * 解析输入消息。
 * @param messageJson 消息的 JSON 文本，如 {"type":"key","code":"KeyW","down":true}。
 * @throws shared::SchemaError core.invalidParams：不是合格的消息。
 */
contracts::hardware::InputEvent parseInputMessage(const std::string& messageJson);

}  // namespace ember::bootstrap
