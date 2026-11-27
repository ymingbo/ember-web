/*
 * ------------------------------------------------------------------------
 * 名称：mmc3.hpp
 * 说明：MMC3 芯片（实现 MapperCircuit 契约）：mapper 电路（可装在卡带上，也可在学习机主板上），$8000-$FFFF 的 8 个寄存器、程序页与图案页的页号、名称表镜像位，以及按图案地址线 A12 上升沿计数的扫描线 IRQ 计数器；不含存储。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：只给出页号，页号怎么落到存储里由使用它的板子决定（板子接了几根页号线就取页号的低几位，窗口基址也是板子的）；
 *       程序页号是 6 位（A18-A13），$C000 与 $E000 固定的倒数第 2、第 1 页的页号线全为高，所以是 $3E、$3F；图案页号是 8 位的 1K 页号（A17-A10）；
 *       寄存器：$8000 偶地址写选择寄存器（位 7 反转图案、位 6 程序模式、位 2-0 选 R0-R7），奇地址写所选寄存器；$A000 偶地址写镜像（位 0 为 1 水平，为 0 垂直），奇地址（PRG RAM 保护）忽略；
 *       $C000 偶地址写 IRQ 锁存值，奇地址使计数器在下一次计数时重载；$E000 偶地址关闭 IRQ 并撤销，奇地址允许 IRQ；
 *       IRQ：A12 先连续低过 A12_LOW_ACCESSES 次显存访问，下一次变高才算一次上升沿，用来滤掉每个图块取名称与属性时的短暂低电平；
 *       每次上升沿：计数器为 0 或有重载请求时装入锁存值，否则减 1；减到 0 且允许 IRQ 时拉低 IRQ 线（线上新增一个来源）；A 版（revA）只有计数器原来不为 0 或有重载请求、且减到 0 时才发 IRQ；
 *       $A001 偶地址以外的奇地址写入（PRG RAM 保护）保存下来，位 7 为 RAM 允许、位 6 为写保护，由使用电路的板子决定怎么用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "contracts/hardware/irq-line.hpp"
#include "contracts/hardware/mapper-circuit.hpp"

namespace ember::logic::hardware::mapper_chips {

/** MMC3 芯片（不含存储）。 */
class Mmc3 final : public contracts::hardware::MapperCircuit {
 public:
  /** A12 要先低过这么多次显存访问，下一次上升沿才计数。 */
  static constexpr unsigned A12_LOW_ACCESSES = 4;
  /** $C000 与 $E000 固定的程序页的页号：倒数第 2 页、最后一页。 */
  static constexpr unsigned SECOND_LAST_PROGRAM_BANK = 0x3E;
  static constexpr unsigned LAST_PROGRAM_BANK = 0x3F;
  /** 程序页与图案页的大小（字节）。 */
  static constexpr std::size_t PROGRAM_BANK_SIZE = 8 * 1024;
  static constexpr std::size_t CHARACTER_BANK_SIZE = 1024;
  /** $A001 上电值：PRG RAM 允许、可写（真机上电值不确定，不写 $A001 的游戏也能用 RAM）。 */
  static constexpr std::uint8_t RAM_ENABLED = 0x80;

  /** @param irq IRQ 线，本芯片在其上登记一个来源。 */
  explicit Mmc3(contracts::hardware::IrqLine& irq, bool revA = false);

  /** 上电：全部寄存器清零，垂直镜像，IRQ 关闭并撤销。 */
  void reset() override;

  /** 清 IRQ 计数器、锁存值与允许位，撤销 IRQ。 */
  void resetIrq() override;

  /** 关闭 IRQ 并撤销（等同写 $E000 偶地址）。 */
  void disableIrq() override;

  /** 对 $8000-$FFFF 的写入：按地址的位 14-13 与位 0 分到各寄存器。 */
  void writeRegister(std::uint16_t address, std::uint8_t value) override;

  /** 程序页 8K、图案页 1K。 */
  [[nodiscard]] std::size_t bankSize(contracts::hardware::BankSpace space, std::uint16_t address) const override;

  /** 按存储空间取 programBank 或 characterBank。 */
  [[nodiscard]] unsigned bank(contracts::hardware::BankSpace space, std::uint16_t address) const override;

  /** 名称表镜像：$A000 的位 0 为 1 水平，为 0 垂直。 */
  [[nodiscard]] std::optional<contracts::hardware::CircuitMirroring> mirroring() const override;

  /** PPU 对显存总线的一次访问：A12 先低够次数再变高算一次时钟。 */
  void onVideoAddress(std::uint16_t address) override;

  /** 内部状态：selector、r0-r7、horizontalMirroring、irqLatch、irqCounter、irqEnabled、ramProtect。 */
  [[nodiscard]] std::vector<contracts::hardware::CircuitStateField> state() const override;

  /** CPU 地址（不小于 $8000）所在 8K 程序页的页号，0 到 $3F。 */
  [[nodiscard]] unsigned programBank(std::uint16_t address) const;

  /** 图案地址（$0000-$1FFF）所在 1K 图案页的页号，0 到 $FF。 */
  [[nodiscard]] unsigned characterBank(std::uint16_t address) const;

  /** 名称表是否水平镜像（$A000 的位 0）。 */
  [[nodiscard]] bool horizontalMirroring() const { return horizontalMirroring_; }

  // 测试与调试用
  [[nodiscard]] std::uint8_t selector() const { return selector_; }
  [[nodiscard]] std::uint8_t registerValue(unsigned index) const { return registers_.at(index); }
  [[nodiscard]] std::uint8_t irqLatch() const { return latch_; }
  [[nodiscard]] std::uint8_t irqCounter() const { return counter_; }
  [[nodiscard]] bool irqEnabled() const { return enabled_; }

 private:
  /** 一次 A12 上升沿。 */
  void clockCounter();

  contracts::hardware::IrqLine& irq_;
  contracts::hardware::SignalSource source_;
  bool revA_{false};
  std::uint8_t selector_{0};
  std::array<std::uint8_t, 8> registers_{};
  bool horizontalMirroring_{false};
  std::uint8_t ramProtect_{RAM_ENABLED};
  std::uint8_t latch_{0};
  std::uint8_t counter_{0};
  bool reload_{false};
  bool enabled_{false};
  bool a12High_{false};
  unsigned lowAccesses_{0};
};

}  // namespace ember::logic::hardware::mapper_chips
