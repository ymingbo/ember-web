/*
 * ------------------------------------------------------------------------
 * 名称：pad-input-router.cpp
 * 说明：手柄输入路由器的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/input/pad-input-router.hpp"

#include <algorithm>
#include <utility>
#include <variant>

#include "contracts/hardware/button-input.hpp"

namespace ember::logic::input {

namespace hw = contracts::hardware;
namespace api = contracts::input;

namespace {

/** 手柄的键对应的 8 位掩码；连发键与不认识的名字返回 0。 */
std::uint8_t maskOf(const std::string& button) {
  if (button == "a") return hw::pad_button::A;
  if (button == "b") return hw::pad_button::B;
  if (button == "select") return hw::pad_button::SELECT;
  if (button == "start") return hw::pad_button::START;
  if (button == "up") return hw::pad_button::UP;
  if (button == "down") return hw::pad_button::DOWN;
  if (button == "left") return hw::pad_button::LEFT;
  if (button == "right") return hw::pad_button::RIGHT;
  return 0;
}

/** 一次按下的结果：普通键的位，以及连发 A、连发 B 是否被按着。 */
struct Pressed {
  std::uint8_t mask{0};
  bool turboA{false};
  bool turboB{false};

  void add(const std::string& button) {
    if (button == "turboA") turboA = true;
    else if (button == "turboB") turboB = true;
    else mask = static_cast<std::uint8_t>(mask | maskOf(button));
  }
};

}  // namespace

PadInputRouter::PadInputRouter(std::shared_ptr<api::InputBindingService> bindings) : bindings_(std::move(bindings)) {}

bool PadInputRouter::handle(const hw::InputEvent& event, hw::Machine& machine) {
  if (const auto* key = std::get_if<hw::KeyInput>(&event)) {
    if (key->down) heldKeys_.insert(key->code);
    else heldKeys_.erase(key->code);
    apply(machine);
    if (!key->down || machine.keyboardIsMachineInput()) return false;
    const auto slots = bindings_->bindings();
    return std::any_of(slots->begin(), slots->end(), [&](const api::PadSlot& slot) {
      return std::any_of(slot.keys.begin(), slot.keys.end(), [&](const api::PadKeyBinding& binding) { return binding.code == key->code; });
    });
  }
  if (const auto* pad = std::get_if<hw::GamepadInput>(&event)) {
    if (pad->index < MAX_GAMEPADS) {
      GamepadState& state = gamepads_[pad->index];
      state.connected = pad->connected;
      state.id = pad->id;
      state.buttons = pad->buttons;
      state.axes = pad->axes;
    }
    apply(machine);
    return true;
  }
  return false;
}

void PadInputRouter::frameCompleted(hw::Machine& machine) {
  ++frameTick_;
  apply(machine);
}

void PadInputRouter::machineChanged() { appliedValid_.fill(false); }

const PadInputRouter::GamepadState* PadInputRouter::gamepadOf(const api::PadGamepadAssignment& assignment) const {
  std::int64_t seen = 0;
  for (const GamepadState& state : gamepads_) {
    if (!state.connected || state.id != assignment.id) continue;
    if (seen == assignment.occurrence) return &state;
    ++seen;
  }
  return nullptr;
}

void PadInputRouter::apply(hw::Machine& machine) {
  const auto slots = bindings_->bindings();
  const bool keyboardForPads = !machine.keyboardIsMachineInput();
  const std::size_t ports = std::min<std::size_t>({machine.padPortCount(), slots->size(), api::PAD_SLOT_COUNT});
  const bool turboOn = (frameTick_ / TURBO_HALF_PERIOD_FRAMES) % 2 == 0;
  for (std::size_t port = 0; port < ports; ++port) {
    const api::PadSlot& slot = (*slots)[port];
    Pressed pressed;
    if (keyboardForPads) {
      for (const api::PadKeyBinding& binding : slot.keys) {
        if (heldKeys_.count(binding.code) != 0) pressed.add(binding.button);
      }
    }
    if (slot.gamepad.has_value()) {
      if (const GamepadState* state = gamepadOf(*slot.gamepad)) {
        for (const api::PadGamepadBinding& binding : slot.gamepad->bindings) {
          const auto index = static_cast<std::size_t>(binding.index);
          bool down = false;
          if (binding.kind == "button") {
            down = index < state->buttons.size() && state->buttons[index];
          } else if (index < state->axes.size()) {
            const double value = state->axes[index];
            down = binding.direction == std::optional<std::string>("negative") ? value < -AXIS_THRESHOLD : value > AXIS_THRESHOLD;
          }
          if (down) pressed.add(binding.button);
        }
      }
    }
    std::uint8_t mask = pressed.mask;
    if (turboOn && pressed.turboA) mask = static_cast<std::uint8_t>(mask | hw::pad_button::A);
    if (turboOn && pressed.turboB) mask = static_cast<std::uint8_t>(mask | hw::pad_button::B);
    if (appliedValid_[port] && applied_[port] == mask) continue;
    machine.setButtons(static_cast<unsigned>(port), mask);
    applied_[port] = mask;
    appliedValid_[port] = true;
  }
}

}  // namespace ember::logic::input
