/*
 * ------------------------------------------------------------------------
 * 名称：mapper-circuit-catalog.hpp
 * 说明：硬件契约：mapper 电路目录。按类型名创建主机里已有的 mapper 电路（如 MMC3、MMC2），供插件清单的 circuits 引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-18
 * 备注：电路的实现在 logic/hardware/mapper-chips，只在组装根（bootstrap/machines/common）登记到目录里；插件只能按类型名选择电路，不能改电路内部。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <string_view>

#include "contracts/hardware/irq-line.hpp"
#include "contracts/hardware/mapper-circuit.hpp"

namespace ember::contracts::hardware {

/** 目录没有这种类型的电路。 */
inline constexpr std::string_view ERROR_CIRCUIT_UNKNOWN = "hardware.circuitUnknown";

/** 清单里给电路的可选项；电路不认识的项被忽略。 */
struct CircuitOptions {
  /** MMC3 的 IRQ 按 A 版行为：计数器从 0 重载到 0 时不发 IRQ。 */
  bool revA{false};
};

/** 电路目录。 */
class CircuitCatalog {
 public:
  virtual ~CircuitCatalog() = default;

  /** 是否有这种类型的电路。 */
  [[nodiscard]] virtual bool knows(std::string_view type) const = 0;

  /**
   * 创建一个电路。
   * @param type 电路类型，必须是 knows 为真的类型。
   * @param irq 电路登记 IRQ 来源的中断线，必须活到电路销毁。
   * @param options 清单给的可选项。
   */
  [[nodiscard]] virtual std::unique_ptr<MapperCircuit> create(std::string_view type, IrqLine& irq, const CircuitOptions& options) const = 0;
};

}  // namespace ember::contracts::hardware
