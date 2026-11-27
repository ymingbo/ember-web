/*
 * ------------------------------------------------------------------------
 * 名称：pad-bindings.hpp
 * 说明：input 契约：手柄绑定的约定：手柄的 10 个键、两个玩家槽位、出厂默认绑定。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：绑定的数据类型由 schema/input.schema.json 生成（PadSlot 等）；这里只放不属于数据类型的常量与出厂默认值。
 *       手柄的 10 个键：标准的 8 个键加连发 A、连发 B；连发由输入层按帧翻转 A、B 位实现，机器收到的仍是 8 位的按键掩码。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "contracts/input/generated/input.hpp"

namespace ember::contracts::input {

/** 玩家槽位：全部机型一共两个，第 1 个对应手柄口 1。 */
using PadSlots = std::vector<PadSlot>;

/** 玩家槽位的个数。 */
inline constexpr std::size_t PAD_SLOT_COUNT = 2;

/** 手柄的键的名字，顺序同界面显示的顺序。 */
inline constexpr std::array<std::string_view, 10> PAD_BUTTON_NAMES = {"a", "b", "select", "start", "up", "down", "left", "right", "turboA", "turboB"};

/** 出厂默认绑定：玩家 1 用键盘（方向键、Z 是 B、X 是 A、C 是连发 B、V 是连发 A、右 Shift 是选择、回车是开始），玩家 2 没有绑定；没有默认的实体手柄。 */
inline PadSlots defaultPadSlots() {
  PadSlots slots(PAD_SLOT_COUNT);
  slots[0].keys = {{"up", "ArrowUp"}, {"down", "ArrowDown"}, {"left", "ArrowLeft"}, {"right", "ArrowRight"}, {"b", "KeyZ"},
                   {"a", "KeyX"},     {"turboB", "KeyC"},    {"turboA", "KeyV"},     {"select", "ShiftRight"}, {"start", "Enter"}};
  return slots;
}

}  // namespace ember::contracts::input
