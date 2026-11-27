/*
 * ------------------------------------------------------------------------
 * 名称：input-service.hpp
 * 说明：input 契约：经调试服务注入输入的服务：键盘、鼠标、游戏手柄的原始事件，以及直接设置手柄口的按键。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：事件与界面发来的输入同一入口（交给卡带的 mapper 脚本）；服务记住注入过的键与手柄，releaseAll 一次性松开。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <string>

#include "contracts/hardware/input-event.hpp"

namespace ember::contracts::input {

/** 输入注入服务；可在任意线程调用。 */
class InputService {
 public:
  virtual ~InputService() = default;

  /** 按下或松开一个物理键。 */
  virtual void key(const std::string& code, bool down) = 0;

  /** 鼠标事件。 */
  virtual void mouse(const hardware::MouseInput& event) = 0;

  /** 游戏手柄的当前状态。 */
  virtual void gamepad(const hardware::GamepadInput& event) = 0;

  /**
   * 直接设置手柄口的按键。
   * @throws shared::CoreError input.invalidPort：没有开机或没有这个口。
   */
  virtual void pad(unsigned port, std::uint8_t buttons) = 0;

  /** 松开注入的全部键、手柄口清零、注入的手柄断开。 */
  virtual void releaseAll() = 0;

  /** 是否给经典 FC 接上了 Family BASIC 键盘；每次启动都是未接入，不保存。 */
  [[nodiscard]] virtual bool familyKeyboard() const = 0;

  /** 接入或断开 Family BASIC 键盘：对当前主机立即生效，状态有变化时发出 input.familyKeyboardChanged；返回设置之后的状态。 */
  virtual bool setFamilyKeyboard(bool connected) = 0;
};

}  // namespace ember::contracts::input
