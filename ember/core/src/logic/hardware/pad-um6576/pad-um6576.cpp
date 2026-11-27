/*
 * ------------------------------------------------------------------------
 * 名称：pad-um6576.cpp
 * 说明：6576 手柄口的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/pad-um6576/pad-um6576.hpp"

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

constexpr std::uint16_t ADDRESS_PAD1 = 0x4016;
constexpr std::uint8_t STATUS_ERROR = 0x04;
constexpr std::uint8_t STATUS_BUSY_N = 0x08;

}  // namespace

hw::ControllerPort& PadUm6576::port(unsigned index) {
  if (index >= PORT_COUNT) throw shared::CoreError(hw::ERROR_PORT_NOT_FOUND, "手柄口只有 2 个");
  return ports_[index];
}

hw::BusRead PadUm6576::read(std::uint16_t address) {
  // 真机实测 $4016 的位 2 总是 1。
  if (address == ADDRESS_PAD1) return hw::driveBus(static_cast<std::uint8_t>((ports_[0].clockData() & 1U) | 0x04), 0x05);
  std::uint8_t value = static_cast<std::uint8_t>(ports_[1].clockData() & 1U);
  if (parallel_.error()) value = static_cast<std::uint8_t>(value | STATUS_ERROR);
  if (parallel_.busyN()) value = static_cast<std::uint8_t>(value | STATUS_BUSY_N);
  return hw::driveBus(value, 0x0D);
}

void PadUm6576::write(std::uint16_t address, std::uint8_t value) {
  if (address != ADDRESS_PAD1) return;
  const bool latch = (value & 1) != 0;
  for (auto& socket : ports_) socket.setLatch(latch);
  parallel_.setSelectPrinterN(latch);
  parallel_.setLineFeedN((value & 2) != 0);
  parallel_.setReset((value & 4) != 0);
}

void PadUm6576::reset(bool /*power*/) {
  for (auto& socket : ports_) socket.setLatch(false);
}

}  // namespace ember::logic::hardware
