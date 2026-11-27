/*
 * ------------------------------------------------------------------------
 * 名称：mmc3.cpp
 * 说明：MMC3 芯片的实现，行为见 mmc3.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/mapper-chips/mmc3/mmc3.hpp"

namespace ember::logic::hardware::mapper_chips {

Mmc3::Mmc3(contracts::hardware::IrqLine& irq, bool revA) : irq_(irq), source_(irq.addSource()), revA_(revA) {}

void Mmc3::reset() {
  selector_ = 0;
  registers_.fill(0);
  horizontalMirroring_ = false;
  ramProtect_ = RAM_ENABLED;
  resetIrq();
}

void Mmc3::resetIrq() {
  latch_ = 0;
  counter_ = 0;
  reload_ = false;
  enabled_ = false;
  a12High_ = false;
  lowAccesses_ = 0;
  irq_.set(source_, false);
}

void Mmc3::disableIrq() {
  enabled_ = false;
  irq_.set(source_, false);
}

void Mmc3::writeRegister(std::uint16_t address, std::uint8_t value) {
  const bool odd = (address & 1U) != 0;
  switch ((address >> 13) & 3U) {
    case 0:
      if (odd) registers_[selector_ & 7U] = value;
      else selector_ = value;
      break;
    case 1:
      if (!odd) horizontalMirroring_ = (value & 1U) != 0;
      else ramProtect_ = value;
      break;
    case 2:
      if (odd) {
        counter_ = 0;
        reload_ = true;
      } else {
        latch_ = value;
      }
      break;
    default:
      if (odd) enabled_ = true;
      else disableIrq();
      break;
  }
}

unsigned Mmc3::programBank(std::uint16_t address) const {
  const bool swapped = (selector_ & 0x40U) != 0;
  switch ((address >> 13) & 3U) {
    case 0:
      return swapped ? SECOND_LAST_PROGRAM_BANK : (registers_[6] & 0x3FU);
    case 1:
      return registers_[7] & 0x3FU;
    case 2:
      return swapped ? (registers_[6] & 0x3FU) : SECOND_LAST_PROGRAM_BANK;
    default:
      return LAST_PROGRAM_BANK;
  }
}

unsigned Mmc3::characterBank(std::uint16_t address) const {
  const unsigned slot = (address >> 10) & 7U;
  // 选择位 7 把两个半区对调：先换算成不反转时的槽位。
  const unsigned index = (selector_ & 0x80U) != 0 ? (slot ^ 4U) : slot;
  // R0、R1 是 2K 页（页号取偶数，奇数的 1K 是它的后一半），R2-R5 是 1K 页。
  if (index < 4) return (registers_[index < 2 ? 0 : 1] & 0xFEU) + (index & 1U);
  return registers_[index - 2];
}

std::size_t Mmc3::bankSize(contracts::hardware::BankSpace space, std::uint16_t) const { return space == contracts::hardware::BankSpace::Program ? PROGRAM_BANK_SIZE : CHARACTER_BANK_SIZE; }

unsigned Mmc3::bank(contracts::hardware::BankSpace space, std::uint16_t address) const { return space == contracts::hardware::BankSpace::Program ? programBank(address) : characterBank(address); }

std::optional<contracts::hardware::CircuitMirroring> Mmc3::mirroring() const {
  return horizontalMirroring_ ? contracts::hardware::CircuitMirroring::Horizontal : contracts::hardware::CircuitMirroring::Vertical;
}

std::vector<contracts::hardware::CircuitStateField> Mmc3::state() const {
  static constexpr const char* NAMES[] = {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7"};
  std::vector<contracts::hardware::CircuitStateField> fields;
  fields.push_back({"selector", selector_, "$8000 的选择寄存器（位 7 反转图案、位 6 程序模式、位 2-0 选 R0-R7）"});
  for (unsigned index = 0; index < registers_.size(); ++index) fields.push_back({NAMES[index], registers_[index], "MMC3 的页寄存器"});
  fields.push_back({"horizontalMirroring", horizontalMirroring_ ? 1U : 0U, "$A000 的镜像位（1 水平，0 垂直）"});
  fields.push_back({"irqLatch", latch_, "$C000 的 IRQ 锁存值"});
  fields.push_back({"irqCounter", counter_, "IRQ 计数器"});
  fields.push_back({"irqEnabled", enabled_ ? 1U : 0U, "IRQ 是否允许"});
  fields.push_back({"ramProtect", ramProtect_, "$A001 的 PRG RAM 保护（位 7 允许、位 6 写保护）"});
  return fields;
}

void Mmc3::onVideoAddress(std::uint16_t address) {
  if ((address & 0x1000U) != 0) {
    if (!a12High_ && lowAccesses_ >= A12_LOW_ACCESSES) clockCounter();
    a12High_ = true;
    lowAccesses_ = 0;
  } else {
    a12High_ = false;
    if (lowAccesses_ < A12_LOW_ACCESSES) ++lowAccesses_;
  }
}

void Mmc3::clockCounter() {
  const bool wasActive = counter_ != 0 || reload_;
  if (counter_ == 0 || reload_) {
    counter_ = latch_;
    reload_ = false;
  } else {
    --counter_;
  }
  if (counter_ == 0 && enabled_ && (!revA_ || wasActive)) irq_.set(source_, true);
}

}  // namespace ember::logic::hardware::mapper_chips
