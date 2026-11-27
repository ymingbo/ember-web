/*
 * ------------------------------------------------------------------------
 * 名称：event-log.hpp
 * 说明：事件记录：把芯片寄存器与 mapper 寄存器的读写、CPU 响应中断，连同发生时 PPU 的帧、扫描线与点记下来，供事件查看使用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-24
 * 备注：只在模拟线程使用，不加锁；各主机型号共用。容量有上限，满了丢弃最早的事件；开始时清空，停止后保留到下一次开始。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

#include "contracts/hardware/machine-debugger.hpp"

namespace ember::logic::hardware {

/** 事件记录。 */
class EventLog {
 public:
  /** 容量上限。 */
  static constexpr std::uint32_t MAX_EVENTS = 1'000'000;

  /** 开始记录：清空已有的事件，之后的事件记进来；容量取 1 到 MAX_EVENTS。 */
  void start(std::uint32_t capacity);

  /** 停止记录，已有的事件保留。 */
  void stop() { active_ = false; }

  [[nodiscard]] bool active() const { return active_; }

  /** 开始过一次记录（内容可以取）。 */
  [[nodiscard]] bool exists() const { return started_; }

  /** 记一个事件；没有在记录时没有效果。 */
  void record(const contracts::hardware::DebugEvent& event);

  /**
   * 取事件。
   * @param fromFrame 只取这个帧序号及以后的事件；为空时取全部。
   * @param maxEvents 最多取的个数；超过时取最新的。
   * @param currentFrame 现在的帧序号。
   */
  [[nodiscard]] contracts::hardware::EventLogOutput output(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents, std::uint64_t currentFrame) const;

 private:
  bool active_{false};
  bool started_{false};
  std::uint32_t capacity_{0};
  std::uint64_t dropped_{0};
  std::deque<contracts::hardware::DebugEvent> events_;
};

}  // namespace ember::logic::hardware
