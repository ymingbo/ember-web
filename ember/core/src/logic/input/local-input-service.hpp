/*
 * ------------------------------------------------------------------------
 * 名称：local-input-service.hpp
 * 说明：输入注入服务的实现：把事件交给运行服务，记住按下的键、设置过的手柄口与注入的手柄，供 releaseAll 一次松开。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "contracts/input/input-service.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "platform/events/event-sink.hpp"

namespace ember::logic::input {

/** 输入注入服务。 */
class LocalInputService final : public contracts::input::InputService {
 public:
  LocalInputService(std::shared_ptr<contracts::runtime::RuntimeService> runtime, std::shared_ptr<platform::EventSink> events);

  void key(const std::string& code, bool down) override;
  void mouse(const contracts::hardware::MouseInput& event) override;
  void gamepad(const contracts::hardware::GamepadInput& event) override;
  void pad(unsigned port, std::uint8_t buttons) override;
  void releaseAll() override;
  [[nodiscard]] bool familyKeyboard() const override;
  bool setFamilyKeyboard(bool connected) override;

 private:
  std::shared_ptr<contracts::runtime::RuntimeService> runtime_;
  std::shared_ptr<platform::EventSink> events_;
  mutable std::mutex mutex_;
  std::set<std::string> downKeys_;
  std::set<unsigned> usedPorts_;
  std::set<std::uint8_t> gamepads_;
  bool familyKeyboard_{false};
};

}  // namespace ember::logic::input
