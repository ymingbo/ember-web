/*
 * ------------------------------------------------------------------------
 * 名称：paging-um6576.hpp
 * 说明：6576 芯片组的程序存储器分页：$4040-$4047 各选一个 4K 页，对应 CPU 地址 $8000-$FFFF 的 8 个窗口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：页号小于 $80 是 BIOS ROM 页（最多 128 页，共 512KB），不小于 $80 是扩展 RAM 页（512KB，页号减 $80）；
 *       上电后窗口 i 对应第 i 页；寄存器可读回写入的值；本模块只给出“窗口对应哪个存储的哪个位置”，不访问存储。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>

#include "contracts/hardware/device.hpp"

namespace ember::logic::hardware {

/** 6576 的分页寄存器（$4040-$4047）。 */
class PagingUm6576 final : public contracts::hardware::Device {
 public:
  /** 窗口个数。 */
  static constexpr unsigned WINDOW_COUNT = 8;
  /** 页大小。 */
  static constexpr std::uint32_t ROM_PAGE_SIZE = 0x1000;
  /** 第一个扩展 RAM 页号。 */
  static constexpr std::uint8_t FIRST_RAM_PAGE = 0x80;
  /** 第一个窗口的 CPU 地址。 */
  static constexpr std::uint16_t FIRST_ADDRESS = 0x8000;
  /** 第一个寄存器的地址。 */
  static constexpr std::uint16_t FIRST_REGISTER = 0x4040;

  /** CPU 地址对应的存储位置。 */
  struct Location {
    /** 为 true 时在扩展 RAM 里，否则在 BIOS ROM 里。 */
    bool ram;
    /** 存储里的字节偏移。 */
    std::uint32_t offset;
  };

  PagingUm6576() { reset(true); }

  contracts::hardware::BusRead read(std::uint16_t address) override { return contracts::hardware::driveBus(banks_[(address - FIRST_REGISTER) % WINDOW_COUNT]); }

  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }

  void write(std::uint16_t address, std::uint8_t value) override { banks_[(address - FIRST_REGISTER) % WINDOW_COUNT] = value; }

  void reset(bool /*power*/) override {
    for (unsigned index = 0; index < WINDOW_COUNT; ++index) banks_[index] = static_cast<std::uint8_t>(index);
  }

  void onEvent(contracts::hardware::MasterTime /*when*/, std::uint32_t /*tag*/) override {}

  /** 窗口寄存器的值。 */
  [[nodiscard]] std::uint8_t bank(unsigned window) const { return banks_[window % WINDOW_COUNT]; }

  /**
   * 把 $8000 以上的 CPU 地址换算成存储位置。
   * @param address CPU 地址，必须不小于 $8000。
   */
  [[nodiscard]] Location resolve(std::uint16_t address) const {
    const std::uint8_t page = banks_[(address >> 12) & 7];
    const std::uint32_t inPage = address & (ROM_PAGE_SIZE - 1);
    if (page >= FIRST_RAM_PAGE) return Location{true, static_cast<std::uint32_t>(page - FIRST_RAM_PAGE) * ROM_PAGE_SIZE + inPage};
    return Location{false, static_cast<std::uint32_t>(page) * ROM_PAGE_SIZE + inPage};
  }

 private:
  std::array<std::uint8_t, WINDOW_COUNT> banks_{};
};

}  // namespace ember::logic::hardware
