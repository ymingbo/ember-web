/*
 * ------------------------------------------------------------------------
 * 名称：local-input-service.cpp
 * 说明：输入注入服务的实现，见 local-input-service.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/input/local-input-service.hpp"

#include <utility>
#include <vector>

#include "contracts/input/generated/input.hpp"
#include "contracts/input/input-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::input {

namespace hw = contracts::hardware;

LocalInputService::LocalInputService(std::shared_ptr<contracts::runtime::RuntimeService> runtime, std::shared_ptr<platform::EventSink> events) : runtime_(std::move(runtime)), events_(std::move(events)) {}

void LocalInputService::key(const std::string& code, bool down) {
  {
    std::lock_guard lock(mutex_);
    if (down) downKeys_.insert(code);
    else downKeys_.erase(code);
  }
  runtime_->sendInput(hw::KeyInput{code, down});
}

void LocalInputService::mouse(const hw::MouseInput& event) { runtime_->sendInput(event); }

void LocalInputService::gamepad(const hw::GamepadInput& event) {
  {
    std::lock_guard lock(mutex_);
    if (event.connected) gamepads_.insert(event.index);
    else gamepads_.erase(event.index);
  }
  runtime_->sendInput(event);
}

void LocalInputService::pad(unsigned port, std::uint8_t buttons) {
  if (port >= runtime_->padPortCount()) throw shared::CoreError(contracts::input::ERROR_INVALID_PORT, "主机没有开机，或没有手柄口 " + std::to_string(port));
  {
    std::lock_guard lock(mutex_);
    if (buttons != 0) usedPorts_.insert(port);
    else usedPorts_.erase(port);
  }
  runtime_->setPadButtons(port, buttons);
}

void LocalInputService::releaseAll() {
  std::set<std::string> keys;
  std::set<unsigned> ports;
  std::set<std::uint8_t> gamepads;
  {
    std::lock_guard lock(mutex_);
    keys.swap(downKeys_);
    ports.swap(usedPorts_);
    gamepads.swap(gamepads_);
  }
  for (const auto& code : keys) runtime_->sendInput(hw::KeyInput{code, false});
  for (const auto port : ports) runtime_->setPadButtons(port, 0);
  for (const auto index : gamepads) runtime_->sendInput(hw::GamepadInput{index, false, {}, {}});
}

bool LocalInputService::familyKeyboard() const {
  std::lock_guard lock(mutex_);
  return familyKeyboard_;
}

bool LocalInputService::setFamilyKeyboard(bool connected) {
  {
    std::lock_guard lock(mutex_);
    if (familyKeyboard_ == connected) return connected;
    familyKeyboard_ = connected;
  }
  runtime_->setFamilyKeyboard(connected);
  if (events_ != nullptr) {
    contracts::input::InputFamilyKeyboardChangedData data;
    data.connected = connected;
    events_->publish(contracts::input::INPUT_FAMILY_KEYBOARD_CHANGED_EVENT, nlohmann::json(data));
  }
  return connected;
}

}  // namespace ember::logic::input
