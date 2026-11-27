/*
 * ------------------------------------------------------------------------
 * 名称：dma-2a03.hpp
 * 说明：2A03 片上的精灵 DMA：写 $4014 后暂停 CPU（RDY），把 CPU 内存的一页（$XX00-$XXFF）复制进 PPU 的 OAM（经 $2004）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：时序：写周期之后第 1 个周期是等待周期；若再下一个周期是奇数周期（写发生在奇数周期）再加 1 个对齐周期；
 *       之后 256 组“读源地址、写 $2004”各占 2 个周期，所以总共暂停 513 或 514 个周期；
 *       传输靠调度器事件推进：每个写周期的中点复制一个字节，最后一个写周期结束时释放 RDY。
 *       DMC 的 DMA 与本 DMA 的相互影响不在本阶段实现。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/clock.hpp"
#include "contracts/hardware/cpu-bus.hpp"
#include "contracts/hardware/device.hpp"
#include "contracts/hardware/rdy-line.hpp"
#include "contracts/hardware/scheduler.hpp"

namespace ember::logic::hardware {

/** 精灵 DMA（挂在 $4014）。 */
class Dma2a03 final : public contracts::hardware::Device {
 public:
  /**
   * @param bus CPU 总线：源数据从它读出，字节写到它的 $2004。
   * @param scheduler 调度器，传输靠它的事件推进。
   * @param rdy CPU 的 RDY 线，本对象在其上登记一个来源。
   * @param cpuDomain CPU 的时钟域，用来换算周期与主时钟周期。
   */
  Dma2a03(contracts::hardware::CpuBus& bus, contracts::hardware::Scheduler& scheduler, contracts::hardware::RdyLine& rdy, contracts::hardware::ClockDomain cpuDomain);

  /** 读 $4014：不驱动数据线。 */
  contracts::hardware::BusRead read(std::uint16_t address) override;

  /** 写 $4014：启动一次传输，值是源页的高 8 位；传输进行中的写入被忽略。 */
  void write(std::uint16_t address, std::uint8_t value) override;

  void reset(bool power) override;

  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /** 是否正在传输（含等待与对齐周期）。 */
  [[nodiscard]] bool active() const { return active_; }

 private:
  /** 事件标记：复制一个字节、传输结束。 */
  static constexpr std::uint32_t TAG_COPY = 0;
  static constexpr std::uint32_t TAG_FINISH = 1;

  contracts::hardware::CpuBus& bus_;
  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::RdyLine& rdy_;
  contracts::hardware::SignalSource rdySource_;
  contracts::hardware::ClockDomain cpuDomain_;
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};
  bool active_{false};
  std::uint16_t source_{0};
  unsigned copied_{0};
};

}  // namespace ember::logic::hardware
