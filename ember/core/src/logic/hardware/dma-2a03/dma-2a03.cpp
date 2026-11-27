/*
 * ------------------------------------------------------------------------
 * 名称：dma-2a03.cpp
 * 说明：2A03 精灵 DMA 的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/dma-2a03/dma-2a03.hpp"

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

/** 一页的字节数。 */
constexpr unsigned ROM_PAGE_SIZE = 256;
/** PPU 的 OAMDATA 寄存器地址。 */
constexpr std::uint16_t OAM_DATA_ADDRESS = 0x2004;

}  // namespace

Dma2a03::Dma2a03(hw::CpuBus& bus, hw::Scheduler& scheduler, hw::RdyLine& rdy, hw::ClockDomain cpuDomain)
    : bus_(bus), scheduler_(scheduler), rdy_(rdy), rdySource_(rdy.addSource()), cpuDomain_(cpuDomain) {
  if (cpuDomain_.ticksPerCycle < 1) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "CPU 周期的主时钟周期数必须大于 0");
}

hw::BusRead Dma2a03::read(std::uint16_t /*address*/) { return hw::releaseBus(); }

void Dma2a03::write(std::uint16_t /*address*/, std::uint8_t value) {
  if (active_) return;
  active_ = true;
  source_ = static_cast<std::uint16_t>(value << 8);
  copied_ = 0;
  rdy_.set(rdySource_, true);

  // 写发生在 CPU 周期 c 的中点；周期 c+1 是等待周期，第一次读必须落在偶数周期上：c 为奇数时再加 1 个对齐周期。
  const std::int64_t writeCycle = cpuDomain_.toCycles(scheduler_.now());
  const std::int64_t firstReadCycle = writeCycle + 2 + (writeCycle % 2);
  const hw::MasterTime half = cpuDomain_.ticksPerCycle / 2;
  // 第一次写在第一次读的下一个周期的中点。
  eventId_ = scheduler_.scheduleAt(cpuDomain_.toMaster(firstReadCycle + 1) + half, *this, TAG_COPY);
}

void Dma2a03::reset(bool /*power*/) {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  active_ = false;
  rdy_.set(rdySource_, false);
}

void Dma2a03::onEvent(hw::MasterTime when, std::uint32_t tag) {
  eventId_ = hw::NO_EVENT;
  if (tag == TAG_FINISH) {
    active_ = false;
    rdy_.set(rdySource_, false);
    return;
  }
  // 复制一个字节：读源地址，写 $2004（PPU 的 OAM 地址随之递增）。
  const std::uint8_t byte = bus_.read(static_cast<std::uint16_t>(source_ + copied_));
  bus_.write(OAM_DATA_ADDRESS, byte);
  ++copied_;
  if (copied_ < ROM_PAGE_SIZE) {
    eventId_ = scheduler_.scheduleAt(when + cpuDomain_.toMaster(2), *this, TAG_COPY);
  } else {
    // 最后一个写周期结束时释放 RDY：中点之后还剩半个周期。
    eventId_ = scheduler_.scheduleAt(when + (cpuDomain_.ticksPerCycle - cpuDomain_.ticksPerCycle / 2), *this, TAG_FINISH);
  }
}

}  // namespace ember::logic::hardware
