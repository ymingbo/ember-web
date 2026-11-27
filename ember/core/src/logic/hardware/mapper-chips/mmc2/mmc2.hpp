/*
 * ------------------------------------------------------------------------
 * 名称：mmc2.hpp
 * 说明：MMC2 与 MMC4 芯片（实现 MapperCircuit 契约）：mapper 电路（可装在卡带上），$A000-$FFFF 的 6 个寄存器、程序页与图案页的页号、镜像位，以及按 PPU 取指地址切换图案页的两个锁存器；不含存储。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-18
 * 备注：寄存器按地址位 15-12 区分：$A 程序页、$B、$C 左半区（$0000-$0FFF）两个图案页、$D、$E 右半区（$1000-$1FFF）两个图案页、$F 镜像位 0（1 为水平，0 为垂直）；
 *       锁存器：左半区在取到地址 $0FD8（MMC4 为 $0FD8-$0FDF）时选第一个图案页、取到 $0FE8（MMC4 为 $0FE8-$0FEF）时选第二个，右半区在 $1FD8-$1FDF、$1FE8-$1FEF；
 *       锁存器变化之后的下一次显存访问才生效（所以图块自己的那一行还用旧页），生效时置“页号已变”标志，由使用电路的板子取走后重新设置窗口；
 *       MMC2 的程序页是 8K（$8000 可选，其余三个固定为倒数第 3、2、1 页），MMC4 是 16K（$8000 可选，$C000 固定为最后一页）；固定页的页号是 6 位的高页号，板子按自己的页数取低几位；图案页的单位是 4K。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "contracts/hardware/mapper-circuit.hpp"

namespace ember::logic::hardware::mapper_chips {

/** MMC2 / MMC4 芯片（不含存储）。 */
class Mmc2 final : public contracts::hardware::MapperCircuit {
 public:
  /** 型号：MMC2 的程序页是 8K，MMC4 是 16K，锁存地址的范围也不同。 */
  enum class Model { Mmc2, Mmc4 };

  explicit Mmc2(Model model);

  /** 上电：两个锁存器选第二个图案页，寄存器清零，镜像未设置。 */
  void reset() override;

  /** 对 $A000-$FFFF 的写入：按地址位 15-12 分到各寄存器。 */
  void writeRegister(std::uint16_t address, std::uint8_t value) override;

  /** PPU 取指：先让待生效的锁存器生效，再按这次地址更新锁存器。 */
  void onVideoAddress(std::uint16_t address) override;

  /** MMC2 的程序页 8K、MMC4 的 16K；图案页 4K。 */
  [[nodiscard]] std::size_t bankSize(contracts::hardware::BankSpace space, std::uint16_t address) const override;

  /** 程序页：$8000 起的可选页，其余固定为高页号（MMC2 的 $3D、$3E、$3F，MMC4 的 $3F）；图案页：当前生效的左、右半区页号。 */
  [[nodiscard]] unsigned bank(contracts::hardware::BankSpace space, std::uint16_t address) const override;

  /** 镜像：写过 $F000 之后才有。 */
  [[nodiscard]] std::optional<contracts::hardware::CircuitMirroring> mirroring() const override;

  /** 锁存器生效使图案页变化后返回 true 并清除标志。 */
  bool takeBankChange() override;

  /** 内部状态：prg、left0、left1、right0、right1、leftLatch、rightLatch、horizontalMirroring。 */
  [[nodiscard]] std::vector<contracts::hardware::CircuitStateField> state() const override;

 private:
  Model model_;
  std::uint8_t prg_{0};
  std::array<std::uint8_t, 2> left_{};
  std::array<std::uint8_t, 2> right_{};
  /** 锁存器当前选的是第几个图案页（0 或 1）。 */
  std::uint8_t leftLatch_{1};
  std::uint8_t rightLatch_{1};
  /** 图案窗口现在用的页（锁存器要等下一次访问才生效，寄存器写入则立即按当前锁存器重新选）。 */
  std::uint8_t shownLeft_{1};
  std::uint8_t shownRight_{1};
  bool needUpdate_{false};
  bool bankChanged_{false};
  bool mirrorSet_{false};
  bool horizontal_{false};
};

}  // namespace ember::logic::hardware::mapper_chips
