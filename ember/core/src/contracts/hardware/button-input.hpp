/*
 * ------------------------------------------------------------------------
 * 名称：button-input.hpp
 * 说明：硬件契约：手柄按键的输入接口。外部（界面、测试、调试服务）经它设置标准手柄当前按下的按键。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：按键用一个 8 位掩码表示，位序与标准手柄串行输出的顺序一致：A 最先、右最后。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/scheduler.hpp"

namespace ember::contracts::hardware {

class TapeLine;

/** 标准手柄的按键掩码位。 */
namespace pad_button {
inline constexpr std::uint8_t A = 0x01;
inline constexpr std::uint8_t B = 0x02;
inline constexpr std::uint8_t SELECT = 0x04;
inline constexpr std::uint8_t START = 0x08;
inline constexpr std::uint8_t UP = 0x10;
inline constexpr std::uint8_t DOWN = 0x20;
inline constexpr std::uint8_t LEFT = 0x40;
inline constexpr std::uint8_t RIGHT = 0x80;
}  // namespace pad_button

/** 按键输入：设置手柄当前按下的按键。 */
class ButtonInput {
 public:
  virtual ~ButtonInput() = default;

  /**
   * 设置当前按下的按键。
   * @param buttons 按键掩码，位见 pad_button；1 表示按下。上下、左右同时按下不做限制，由游戏自己处理。
   */
  virtual void setButtons(std::uint8_t buttons) = 0;
};

/** 主机给卡带脚本用的接口：手柄口的集合（mapper 脚本经 ctx.pad 设置各口的按键）、磁带机的数据线（ctx.device('tape')），以及调度器与 CPU 时钟域（ctx.cycle、ctx.timer）。 */
class PadSink {
 public:
  virtual ~PadSink() = default;

  /** 手柄口的个数。 */
  [[nodiscard]] virtual unsigned padPortCount() const = 0;

  /**
   * 设置某个手柄口上标准手柄的按键。
   * @param port 手柄口编号，从 0 开始；没有这个口时报错。
   * @param buttons 按键掩码，位见 pad_button。
   */
  virtual void setButtons(unsigned port, std::uint8_t buttons) = 0;

  /** 主机上的磁带机数据线；没有磁带机时为空。 */
  [[nodiscard]] virtual TapeLine* tapeLine() { return nullptr; }

  /** 主机的调度器，卡带的计时（ctx.timer）与周期计数（ctx.cycle）用；主机没有时为空。 */
  [[nodiscard]] virtual Scheduler* cartridgeScheduler() { return nullptr; }

  /** CPU 的时钟域：卡带按 CPU 周期计时；没有调度器时不使用。 */
  [[nodiscard]] virtual ClockDomain cartridgeCpuClock() const { return ClockDomain{1}; }
};

}  // namespace ember::contracts::hardware
