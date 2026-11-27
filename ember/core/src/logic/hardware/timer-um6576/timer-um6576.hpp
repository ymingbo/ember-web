/*
 * ------------------------------------------------------------------------
 * 名称：timer-um6576.hpp
 * 说明：6576 芯片组的定时器：$4034 控制，$4035 预置值，$4036 当前值；下溢时向中断汇集器发出定时器中断。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：控制字节：位 7 允许，位 6 连续重载（0 只计一次），位 5 基准时钟（0 为 CPU 周期，1 为行消隐时钟），位 3-0 预分频 PS；
 *       周期 = 基准周期 × (PS + 1) × (预置值 + 1)；
 *       写 $4035 同时把当前值设为预置值并重新开始计时；写 $4034 保留当前值，只重新开始预分频的计数；
 *       真机实测：$4035 可读（读回预置值）；一次计时下溢一次之后，当前值保持预置值，不再计时、不再产生中断；
 *       当前值按读取时刻即时推算，下溢由调度器事件触发，所以不需要逐周期推进。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/device.hpp"
#include "contracts/hardware/interrupt-sink.hpp"
#include "contracts/hardware/scheduler.hpp"

namespace ember::logic::hardware {

/** 6576 定时器（$4034-$4036）。 */
class TimerUm6576 final : public contracts::hardware::Device {
 public:
  /**
   * @param scheduler 调度器，下溢靠它的事件触发。
   * @param sink 中断汇集器。
   * @param cpuCycleTicks 一个 CPU 周期的主时钟周期数。
   * @param lineTicks 一个扫描行（行消隐时钟的一个周期）的主时钟周期数。
   */
  TimerUm6576(contracts::hardware::Scheduler& scheduler, contracts::hardware::InterruptSink& sink, contracts::hardware::MasterTime cpuCycleTicks, contracts::hardware::MasterTime lineTicks);

  contracts::hardware::BusRead read(std::uint16_t address) override;
  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }
  void write(std::uint16_t address, std::uint8_t value) override;
  void reset(bool power) override;
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /** 控制寄存器。 */
  [[nodiscard]] std::uint8_t control() const { return control_; }

  /** 预置值。 */
  [[nodiscard]] std::uint8_t preload() const { return preload_; }

  /** 当前时刻的当前值。 */
  [[nodiscard]] std::uint8_t currentValue() const;

  /** 是否允许计时（控制字节位 7）。 */
  [[nodiscard]] bool enabled() const { return (control_ & 0x80) != 0; }

  /** 一次完整下溢的周期（主时钟周期数）：基准周期 × (PS + 1) × (预置值 + 1)。 */
  [[nodiscard]] contracts::hardware::MasterTime periodTicks() const { return decrementTicks() * (preload_ + 1); }

 private:
  /** 当前值减 1 所需的主时钟周期数：基准周期 × (PS + 1)。 */
  [[nodiscard]] contracts::hardware::MasterTime decrementTicks() const;

  /** 按当前状态重新登记下一次下溢的事件。 */
  void reschedule();

  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::InterruptSink& sink_;
  contracts::hardware::MasterTime cpuCycleTicks_;
  contracts::hardware::MasterTime lineTicks_;
  std::uint8_t control_{0};
  std::uint8_t preload_{0};
  /** 在 anchor_ 时刻的当前值。 */
  std::uint8_t value_{0};
  /** 预分频重新开始计数的时刻。 */
  contracts::hardware::MasterTime anchor_{0};
  /** 一次计时已经下溢过。 */
  bool oneShotDone_{false};
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};
};

}  // namespace ember::logic::hardware
