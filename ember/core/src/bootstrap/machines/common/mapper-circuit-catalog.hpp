/*
 * ------------------------------------------------------------------------
 * 名称：mapper-circuit-catalog.hpp
 * 说明：组装根：电路目录的实现。登记主机里已有的 mapper 电路（mmc3、mmc2、mmc4），插件清单按类型名引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-18
 * 备注：电路的实现在 logic/hardware/mapper-chips，这里只负责按类型名创建；增加电路时在这里登记。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <string_view>

#include "contracts/hardware/mapper-circuit-catalog.hpp"

namespace ember::bootstrap {

/** 程序自带的电路目录。 */
class StandardCircuitCatalog final : public contracts::hardware::CircuitCatalog {
 public:
  [[nodiscard]] bool knows(std::string_view type) const override;
  [[nodiscard]] std::unique_ptr<contracts::hardware::MapperCircuit> create(std::string_view type, contracts::hardware::IrqLine& irq, const contracts::hardware::CircuitOptions& options) const override;
};

}  // namespace ember::bootstrap
