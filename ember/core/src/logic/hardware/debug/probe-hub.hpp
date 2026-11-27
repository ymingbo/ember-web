/*
 * ------------------------------------------------------------------------
 * 名称：probe-hub.hpp
 * 说明：总线探针的分发：把总线访问同时交给断点表（读写断点）、记录表（I/O 记录）与事件记录。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：调试访问（单步、调试写内存）期间可以分别暂停断点与记录，被暂停的访问不触发；事件记录属于“记录”，与记录表一起暂停。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/hardware/access-probe.hpp"
#include "logic/hardware/debug/breakpoint-table.hpp"
#include "logic/hardware/debug/event-log.hpp"
#include "logic/hardware/debug/recorder-table.hpp"

namespace ember::logic::hardware {

/** 探针分发。 */
class ProbeHub final : public contracts::hardware::AccessProbe {
 public:
  /**
   * @param events 事件记录：记录在进行时才把访问交给 eventProbe。
   * @param eventProbe 把一次访问写成事件的探针（它知道当前的指令与扫描位置）。
   */
  ProbeHub(BreakpointTable& breakpoints, RecorderTable& recorders, const EventLog& events, contracts::hardware::AccessProbe& eventProbe)
      : breakpoints_(breakpoints), recorders_(recorders), events_(events), eventProbe_(eventProbe) {}

  /** 要不要装到总线上。 */
  [[nodiscard]] bool needed() const { return breakpoints_.hasAccess() || recorders_.recordsIo() || events_.active(); }

  /** 暂停断点与记录；返回暂停之前的状态，用 restore 恢复。 */
  struct Suspension {
    bool breakpoints;
    bool recording;
  };
  Suspension suspend(bool breakpoints, bool recording) {
    const Suspension before{breakpointsSuspended_, recordingSuspended_};
    breakpointsSuspended_ = breakpointsSuspended_ || breakpoints;
    recordingSuspended_ = recordingSuspended_ || recording;
    return before;
  }
  void restore(const Suspension& before) {
    breakpointsSuspended_ = before.breakpoints;
    recordingSuspended_ = before.recording;
  }

  /** 断点现在是否被暂停（调试访问期间）。 */
  [[nodiscard]] bool breakpointsSuspended() const { return breakpointsSuspended_; }

  void onAccess(contracts::hardware::AccessKind kind, std::uint16_t address, std::uint8_t value) override {
    if (!breakpointsSuspended_ && breakpoints_.hasAccess()) breakpoints_.onAccess(kind, address, value);
    if (recordingSuspended_) return;
    if (recorders_.recordsIo()) recorders_.onIo(kind, address, value);
    if (events_.active()) eventProbe_.onAccess(kind, address, value);
  }

 private:
  BreakpointTable& breakpoints_;
  RecorderTable& recorders_;
  const EventLog& events_;
  contracts::hardware::AccessProbe& eventProbe_;
  bool breakpointsSuspended_{false};
  bool recordingSuspended_{false};
};

}  // namespace ember::logic::hardware
