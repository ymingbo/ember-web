/*
 * ------------------------------------------------------------------------
 * 名称：mmc2.cpp
 * 说明：MMC2 与 MMC4 芯片的实现，行为见 mmc2.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-18
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/mapper-chips/mmc2/mmc2.hpp"

namespace ember::logic::hardware::mapper_chips {

namespace {

constexpr std::size_t PROGRAM_BANK_SIZE_MMC2 = 8 * 1024;
constexpr std::size_t PROGRAM_BANK_SIZE_MMC4 = 16 * 1024;
constexpr std::size_t CHARACTER_BANK_SIZE = 4 * 1024;

}  // namespace

Mmc2::Mmc2(Model model) : model_(model) {}

void Mmc2::reset() {
  prg_ = 0;
  left_.fill(0);
  right_.fill(0);
  leftLatch_ = 1;
  rightLatch_ = 1;
  shownLeft_ = 1;
  shownRight_ = 1;
  needUpdate_ = false;
  bankChanged_ = false;
  mirrorSet_ = false;
  horizontal_ = false;
}

void Mmc2::writeRegister(std::uint16_t address, std::uint8_t value) {
  switch (address >> 12) {
    case 0xA:
      prg_ = value & 0x0F;
      break;
    case 0xB:
      left_[0] = value & 0x1F;
      shownLeft_ = leftLatch_;
      break;
    case 0xC:
      left_[1] = value & 0x1F;
      shownLeft_ = leftLatch_;
      break;
    case 0xD:
      right_[0] = value & 0x1F;
      shownRight_ = rightLatch_;
      break;
    case 0xE:
      right_[1] = value & 0x1F;
      shownRight_ = rightLatch_;
      break;
    case 0xF:
      mirrorSet_ = true;
      horizontal_ = (value & 0x01) != 0;
      break;
    default:
      break;
  }
}

void Mmc2::onVideoAddress(std::uint16_t address) {
  if (needUpdate_) {
    shownLeft_ = leftLatch_;
    shownRight_ = rightLatch_;
    needUpdate_ = false;
    bankChanged_ = true;
  }
  const bool mmc4 = model_ == Model::Mmc4;
  if (mmc4 ? (address >= 0x0FD8 && address <= 0x0FDF) : address == 0x0FD8) {
    leftLatch_ = 0;
    needUpdate_ = true;
  } else if (mmc4 ? (address >= 0x0FE8 && address <= 0x0FEF) : address == 0x0FE8) {
    leftLatch_ = 1;
    needUpdate_ = true;
  } else if (address >= 0x1FD8 && address <= 0x1FDF) {
    rightLatch_ = 0;
    needUpdate_ = true;
  } else if (address >= 0x1FE8 && address <= 0x1FEF) {
    rightLatch_ = 1;
    needUpdate_ = true;
  }
}

std::size_t Mmc2::bankSize(contracts::hardware::BankSpace space, std::uint16_t) const {
  if (space == contracts::hardware::BankSpace::Character) return CHARACTER_BANK_SIZE;
  return model_ == Model::Mmc4 ? PROGRAM_BANK_SIZE_MMC4 : PROGRAM_BANK_SIZE_MMC2;
}

unsigned Mmc2::bank(contracts::hardware::BankSpace space, std::uint16_t address) const {
  if (space == contracts::hardware::BankSpace::Character) return address < 0x1000U ? left_[shownLeft_] : right_[shownRight_];
  if (model_ == Model::Mmc4) return address < 0xC000U ? prg_ : 0x3FU;
  switch ((address >> 13) & 3U) {
    case 0:
      return prg_;
    case 1:
      return 0x3DU;
    case 2:
      return 0x3EU;
    default:
      return 0x3FU;
  }
}

std::optional<contracts::hardware::CircuitMirroring> Mmc2::mirroring() const {
  if (!mirrorSet_) return std::nullopt;
  return horizontal_ ? contracts::hardware::CircuitMirroring::Horizontal : contracts::hardware::CircuitMirroring::Vertical;
}

bool Mmc2::takeBankChange() {
  const bool changed = bankChanged_;
  bankChanged_ = false;
  return changed;
}

std::vector<contracts::hardware::CircuitStateField> Mmc2::state() const {
  return {{"prg", prg_, "$A000 的程序页"},
          {"left0", left_[0], "$B000 的左半区图案页（锁存器为 0 时用）"},
          {"left1", left_[1], "$C000 的左半区图案页（锁存器为 1 时用）"},
          {"right0", right_[0], "$D000 的右半区图案页（锁存器为 0 时用）"},
          {"right1", right_[1], "$E000 的右半区图案页（锁存器为 1 时用）"},
          {"leftLatch", leftLatch_, "左半区的锁存器"},
          {"rightLatch", rightLatch_, "右半区的锁存器"},
          {"horizontalMirroring", horizontal_ ? 1U : 0U, "$F000 的镜像位（1 水平，0 垂直）"}};
}

}  // namespace ember::logic::hardware::mapper_chips
