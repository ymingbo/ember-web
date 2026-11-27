/*
 * ------------------------------------------------------------------------
 * 名称：pad-2a03.cpp
 * 说明：2A03 片上手柄口的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/pad-2a03/pad-2a03.hpp"

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

/** 手柄只驱动数据总线的低 5 位。 */
constexpr std::uint8_t DRIVEN_MASK = 0x1F;

hw::ControllerPort& Pad2a03::port(unsigned index) {
  if (index >= PORT_COUNT) throw shared::CoreError(hw::ERROR_PORT_NOT_FOUND, "手柄口只有 2 个，编号为 0 与 1");
  return ports_[index];
}

hw::BusRead Pad2a03::read(std::uint16_t address) {
  // $4016 读手柄口 1，$4017 读手柄口 2。
  const std::uint8_t data = ports_[address & 1U].clockData();
  return hw::driveBus(static_cast<std::uint8_t>(data & DRIVEN_MASK), DRIVEN_MASK);
}

void Pad2a03::write(std::uint16_t address, std::uint8_t value) {
  // 只有 $4016 的位 0 是锁存线，两个手柄口共用。
  if ((address & 1U) != 0) return;
  const bool high = (value & 1U) != 0;
  for (ControllerSocket& socket : ports_) socket.setLatch(high);
}

}  // namespace ember::logic::hardware
