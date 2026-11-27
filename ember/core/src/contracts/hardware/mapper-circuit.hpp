/*
 * ------------------------------------------------------------------------
 * 名称：mapper-circuit.hpp
 * 说明：硬件契约：mapper 电路（MMC3 等）。寄存器的读写、对 PPU 地址线的观察、按地址给出的页号与页大小、名称表镜像、IRQ 的复位与关闭，以及内部状态的调试读取。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：mapper 电路可以装在卡带上，也可以在学习机的主板上；电路只给寄存器逻辑与页号，页号落到哪块存储、接了几根页号线由使用它的板子（主机型号或卡带）决定；
 *       页号的单位是 bankSize 给出的页大小，同一电路在不同地址、不同模式下页大小可以不同（如 MMC1 的 16K 与 32K 模式）；
 *       IRQ 线由创建电路的一方传入，电路在线上登记自己的来源；没有 IRQ 的电路不覆盖 resetIrq 与 disableIrq；
 *       后续要给电路加能力（按 CPU 周期计时、扩展声音）时新增可选的方法或另外的接口，不改变已有方法的含义。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ember::contracts::hardware {

/** 页号所属的存储空间：CPU 侧的程序页或 PPU 侧的图案页。 */
enum class BankSpace { Program, Character };

/** 电路自己控制的名称表镜像方式。 */
enum class CircuitMirroring { Vertical, Horizontal, Single0, Single1 };

/** 电路对外公开的一项内部状态（给调试用）。 */
struct CircuitStateField {
  std::string name;
  std::uint32_t value{0};
  std::string description;
};

/** mapper 电路。 */
class MapperCircuit {
 public:
  virtual ~MapperCircuit() = default;

  /** 上电复位：全部寄存器与计数器回到初始值，撤销 IRQ。 */
  virtual void reset() = 0;

  /** 清掉 IRQ 相关的计数器、锁存值与允许位，并撤销 IRQ 请求；没有 IRQ 的电路什么都不做。 */
  virtual void resetIrq() {}

  /** 关闭 IRQ 并撤销请求，其他寄存器不变；没有 IRQ 的电路什么都不做。 */
  virtual void disableIrq() {}

  /** CPU 对寄存器范围内某个地址的一次写入。 */
  virtual void writeRegister(std::uint16_t address, std::uint8_t value) = 0;

  /** CPU 对寄存器范围内某个地址的一次读取：电路驱动数据线时给出值，不驱动时为空（读到总线上最近的值）。 */
  [[nodiscard]] virtual std::optional<std::uint8_t> readRegister(std::uint16_t) { return std::nullopt; }

  /** PPU 对显存总线的一次访问：监视地址线（如 A12）的电路据此计数；不关心的电路忽略。 */
  virtual void onVideoAddress(std::uint16_t) {}

  /** 上一次取走之后，电路因显存访问（如 MMC2 的锁存器）改变过页号时返回 true 并清除标志，板子据此重新设置窗口；页号只随寄存器写入变化的电路始终返回 false。 */
  virtual bool takeBankChange() { return false; }

  /** 这个地址所在的页的大小（字节）。 */
  [[nodiscard]] virtual std::size_t bankSize(BankSpace space, std::uint16_t address) const = 0;

  /** 这个地址所在的页的页号，单位是 bankSize；位数由电路的页号线决定，板子按自己接的线取低几位。 */
  [[nodiscard]] virtual unsigned bank(BankSpace space, std::uint16_t address) const = 0;

  /** 名称表镜像方式；电路不控制镜像时为空。 */
  [[nodiscard]] virtual std::optional<CircuitMirroring> mirroring() const { return std::nullopt; }

  /** 内部状态（寄存器、计数器等），按稳定的顺序，给调试用。 */
  [[nodiscard]] virtual std::vector<CircuitStateField> state() const = 0;
};

}  // namespace ember::contracts::hardware
