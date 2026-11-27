/*
 * ------------------------------------------------------------------------
 * 名称：dma-um6576.hpp
 * 说明：6576 芯片组的 DMA：$4014 精灵 DMA（暂停 CPU，把一页复制进 PPU 的 OAM），$4048-$404F 通用 DMA（把 ROM 或扩展 RAM 的数据搬进显存或工作 RAM）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：精灵 DMA 的时序同 2A03（暂停 513 或 514 个周期），传输靠调度器事件推进；
 *       通用 DMA 写 $4048 的位 7 启动，瞬时完成（不暂停 CPU，不等垂直消隐）；寄存器：
 *       $4048 位 7 使能、位 6 速率、位 5 目的（1 为工作 RAM，0 为显存）；$4049 源 bank（低 4 位是 A19-A16，位 4 选扩展 RAM）；
 *       $404A、$404B 源地址（$404B 位 7 为 A15，真机要求为 1，为 0 时不传输）；$404C、$404D 目的地址；$404E、$404F 长度减 1，
 *       真机实测长度按“寄存器值 + 1”个字节搬；
 *       真机实测：$4020 位 7 为 0 时模式位 5 不起作用，目的总是显存（该位由键盘接口模块保存，经回调 timingControlOn 取得）；
 *       目的是工作 RAM 时 $8000 以上的地址写不进去；目的是显存时地址按 16 位回绕。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "contracts/hardware/clock.hpp"
#include "contracts/hardware/cpu-bus.hpp"
#include "contracts/hardware/device.hpp"
#include "contracts/hardware/dma-memory.hpp"
#include "contracts/hardware/rdy-line.hpp"
#include "contracts/hardware/scheduler.hpp"

namespace ember::logic::hardware {

/** 6576 的 DMA（$4014、$4048-$404F）。 */
class DmaUm6576 final : public contracts::hardware::Device {
 public:
  /**
   * @param bus CPU 总线：精灵 DMA 的源数据从它读出，字节写到它的 $2004。
   * @param scheduler 调度器，精灵 DMA 的传输靠它的事件推进。
   * @param rdy CPU 的 RDY 线，本对象在其上登记一个来源。
   * @param cpuDomain CPU 的时钟域。
   * @param memory 通用 DMA 的源与目的。
   */
  DmaUm6576(contracts::hardware::CpuBus& bus, contracts::hardware::Scheduler& scheduler, contracts::hardware::RdyLine& rdy, contracts::hardware::ClockDomain cpuDomain, contracts::hardware::DmaMemory& memory);

  contracts::hardware::BusRead read(std::uint16_t address) override;
  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }
  void write(std::uint16_t address, std::uint8_t value) override;
  void reset(bool power) override;
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /** $4020 的位 7（时序设置）是否为 1；没有设置回调时视为 0。 */
  std::function<bool()> timingControlOn;

  /** 精灵 DMA 是否在进行（含等待与对齐周期）。 */
  [[nodiscard]] bool spriteDmaActive() const { return active_; }

  /** 通用 DMA 寄存器 $4048-$404F 的值。 */
  [[nodiscard]] std::uint8_t generalRegister(unsigned index) const { return registers_[index % 8]; }

 private:
  static constexpr std::uint32_t TAG_COPY = 0;
  static constexpr std::uint32_t TAG_FINISH = 1;

  /** 按寄存器的内容执行一次通用 DMA。 */
  void transfer();

  contracts::hardware::CpuBus& bus_;
  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::RdyLine& rdy_;
  contracts::hardware::SignalSource rdySource_;
  contracts::hardware::ClockDomain cpuDomain_;
  contracts::hardware::DmaMemory& memory_;
  std::array<std::uint8_t, 8> registers_{};
  bool active_{false};
  std::uint16_t source_{0};
  unsigned copied_{0};
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};
};

}  // namespace ember::logic::hardware
