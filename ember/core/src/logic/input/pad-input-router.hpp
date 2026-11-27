/*
 * ------------------------------------------------------------------------
 * 名称：pad-input-router.hpp
 * 说明：手柄输入路由器：运行服务的输入钩子的实现。把游戏手柄的原始状态与（不属于机器的）键盘按键，按手柄绑定换算成手柄口的按键掩码，交给主机；连发键按帧翻转 A、B 位。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：路由器的方法只在模拟线程调用，状态不加锁；绑定从绑定服务取不可变的快照，改绑定后下一次事件或下一帧生效。
 *       键盘按键：键盘属于机器（学习机在 DOS 里）时全部交给机器；否则被绑定的按下事件由路由器截下，松开事件总是同时交给机器，避免机器那边留下按着的键。
 *       游戏手柄的事件全部由路由器截下，不交给机器。掩码只在变化时才设置到主机，所以调试请求 input.pad 直接设置的掩码会保持到手柄状态下一次变化。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "contracts/input/input-binding-service.hpp"
#include "contracts/runtime/input-hook.hpp"

namespace ember::logic::input {

/** 手柄输入路由器。 */
class PadInputRouter final : public contracts::runtime::InputHook {
 public:
  /** 连发：每隔这么多帧翻转一次（按下、松开各占这么多帧）。 */
  static constexpr unsigned TURBO_HALF_PERIOD_FRAMES = 2;
  /** 摇杆算作按下的偏移量。 */
  static constexpr double AXIS_THRESHOLD = 0.5;
  /** 同时记录的实体手柄个数（Gamepad.index 的上限加 1）。 */
  static constexpr std::size_t MAX_GAMEPADS = 16;

  explicit PadInputRouter(std::shared_ptr<contracts::input::InputBindingService> bindings);

  bool handle(const contracts::hardware::InputEvent& event, contracts::hardware::Machine& machine) override;
  void frameCompleted(contracts::hardware::Machine& machine) override;
  void machineChanged() override;

 private:
  /** 一个实体手柄最近上报的状态。 */
  struct GamepadState {
    bool connected{false};
    std::string id;
    std::vector<bool> buttons;
    std::vector<double> axes;
  };

  /** 按当前状态重算各手柄口的按键，有变化时设置到主机。 */
  void apply(contracts::hardware::Machine& machine);
  /** 绑定给手柄口 slot 的那个实体手柄的状态；没有或没连着时为空指针。 */
  [[nodiscard]] const GamepadState* gamepadOf(const contracts::input::PadGamepadAssignment& assignment) const;

  std::shared_ptr<contracts::input::InputBindingService> bindings_;
  std::set<std::string> heldKeys_;
  std::array<GamepadState, MAX_GAMEPADS> gamepads_;
  /** 上一次设置到各手柄口的掩码，及是否设置过。 */
  std::array<std::uint8_t, contracts::input::PAD_SLOT_COUNT> applied_{};
  std::array<bool, contracts::input::PAD_SLOT_COUNT> appliedValid_{};
  unsigned frameTick_{0};
};

}  // namespace ember::logic::input
