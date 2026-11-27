/*
 * ------------------------------------------------------------------------
 * 名称：input-hook.hpp
 * 说明：runtime 契约：输入钩子。运行服务在把输入事件交给主机之前先交给它，它可以截下事件（如游戏手柄、当手柄用的键盘按键）改成手柄口的按键。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：钩子的全部方法都只在模拟线程调用，实现里的状态不需要加锁；由 input 模块实现并挂到运行服务上（运行服务不认识 input 模块）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/hardware/input-event.hpp"
#include "contracts/hardware/machine.hpp"

namespace ember::contracts::runtime {

/** 输入钩子；方法只在模拟线程调用。 */
class InputHook {
 public:
  virtual ~InputHook() = default;

  /**
   * 处理一个输入事件。
   * @param event 输入事件。
   * @param machine 当前的主机。
   * @return true 表示事件已处理，不再交给主机；false 表示交给主机。
   */
  virtual bool handle(const hardware::InputEvent& event, hardware::Machine& machine) = 0;

  /** 主机每完成一帧调用一次（连发按帧翻转）。 */
  virtual void frameCompleted(hardware::Machine& machine) = 0;

  /** 主机换了（装入卡带、开机、取出）：钩子丢弃对旧主机的记忆。 */
  virtual void machineChanged() = 0;
};

}  // namespace ember::contracts::runtime
