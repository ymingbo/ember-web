/*
 * ------------------------------------------------------------------------
 * 名称：dma-um6576.cpp
 * 说明：6576 DMA 的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/dma-um6576/dma-um6576.hpp"

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

constexpr unsigned ROM_PAGE_SIZE = 256;
constexpr std::uint16_t OAM_DATA_ADDRESS = 0x2004;
constexpr std::uint16_t ADDRESS_SPRITE = 0x4014;
constexpr std::uint16_t FIRST_GENERAL = 0x4048;
constexpr std::uint8_t ENABLE_BIT = 0x80;
constexpr std::uint8_t DEST_WORK_RAM_BIT = 0x20;
/** $404B 的位 7：源地址 A15，真机要求为 1。 */
constexpr std::uint8_t SOURCE_A15_BIT = 0x80;
constexpr std::uint8_t SOURCE_EXTENDED_RAM_BIT = 0x10;
/** 程序窗口的起点：目的在它之上时写不进去。 */
constexpr std::uint32_t PROGRAM_WINDOW = 0x8000;

}  // namespace

DmaUm6576::DmaUm6576(hw::CpuBus& bus, hw::Scheduler& scheduler, hw::RdyLine& rdy, hw::ClockDomain cpuDomain, hw::DmaMemory& memory)
    : bus_(bus), scheduler_(scheduler), rdy_(rdy), rdySource_(rdy.addSource()), cpuDomain_(cpuDomain), memory_(memory) {
  if (cpuDomain_.ticksPerCycle < 1) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "CPU 周期的主时钟周期数必须大于 0");
}

hw::BusRead DmaUm6576::read(std::uint16_t address) {
  if (address == ADDRESS_SPRITE) return hw::releaseBus();
  return hw::driveBus(registers_[address - FIRST_GENERAL]);
}

void DmaUm6576::write(std::uint16_t address, std::uint8_t value) {
  if (address == ADDRESS_SPRITE) {
    if (active_) return;
    active_ = true;
    source_ = static_cast<std::uint16_t>(value << 8);
    copied_ = 0;
    rdy_.set(rdySource_, true);
    // 写发生在 CPU 周期 c 的中点；周期 c+1 是等待周期，第一次读必须落在偶数周期上：c 为奇数时再加 1 个对齐周期。
    const std::int64_t writeCycle = cpuDomain_.toCycles(scheduler_.now());
    const std::int64_t firstReadCycle = writeCycle + 2 + (writeCycle % 2);
    const hw::MasterTime half = cpuDomain_.ticksPerCycle / 2;
    eventId_ = scheduler_.scheduleAt(cpuDomain_.toMaster(firstReadCycle + 1) + half, *this, TAG_COPY);
    return;
  }
  const unsigned index = address - FIRST_GENERAL;
  registers_[index] = value;
  // 真机实测：$404B 位 7（源地址 A15）为 0 时 DMA 全部失败，所以不传输。
  if (index == 0 && (value & ENABLE_BIT) != 0 && (registers_[3] & SOURCE_A15_BIT) != 0) transfer();
}

void DmaUm6576::transfer() {
  const std::uint32_t source = (static_cast<std::uint32_t>(registers_[1] & 0x0F) << 15) | (static_cast<std::uint32_t>(registers_[3] & 0x7F) << 8) | registers_[2];
  const std::uint32_t destination = (static_cast<std::uint32_t>(registers_[5]) << 8) | registers_[4];
  const std::uint32_t length = ((static_cast<std::uint32_t>(registers_[7]) << 8) | registers_[6]) + 1;
  const bool extended = (registers_[1] & SOURCE_EXTENDED_RAM_BIT) != 0;
  const bool toWorkRam = timingControlOn && timingControlOn() && (registers_[0] & DEST_WORK_RAM_BIT) != 0;
  for (std::uint32_t index = 0; index < length; ++index) {
    const std::uint8_t byte = memory_.readSource(extended, source + index);
    if (toWorkRam) {
      if (destination + index < PROGRAM_WINDOW) memory_.writeWork(static_cast<std::uint16_t>(destination + index), byte);
    } else {
      memory_.writeVideo(static_cast<std::uint16_t>(destination + index), byte);
    }
  }
}

void DmaUm6576::reset(bool /*power*/) {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  active_ = false;
  rdy_.set(rdySource_, false);
  registers_.fill(0);
}

void DmaUm6576::onEvent(hw::MasterTime when, std::uint32_t tag) {
  eventId_ = hw::NO_EVENT;
  if (tag == TAG_FINISH) {
    active_ = false;
    rdy_.set(rdySource_, false);
    return;
  }
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
