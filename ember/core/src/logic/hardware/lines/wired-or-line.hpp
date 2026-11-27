/*
 * ------------------------------------------------------------------------
 * 名称：wired-or-line.hpp
 * 说明：线或信号线的实现：每个来源占一位，任一位有效，线就有效；用作 IRQ、NMI 与 RDY 线。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/hardware-error.hpp"
#include "contracts/hardware/irq-line.hpp"
#include "contracts/hardware/rdy-line.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

/**
 * 线或信号线。
 * @tparam Interface 实现的线契约（IrqLine 或 RdyLine）。
 */
template <typename Interface>
class WiredOrLine final : public Interface {
 public:
  /** 来源数量上限：每个来源占状态字的一位。 */
  static constexpr contracts::hardware::SignalSource MAX_SOURCES = 32;

  contracts::hardware::SignalSource addSource() override {
    if (sourceCount_ >= MAX_SOURCES) throw shared::CoreError(contracts::hardware::ERROR_LINE_SOURCE_LIMIT, "信号线的来源最多 32 个");
    return sourceCount_++;
  }

  void set(contracts::hardware::SignalSource source, bool active) override {
    if (source >= sourceCount_) throw shared::CoreError(contracts::hardware::ERROR_INVALID_ARGUMENT, "信号来源没有登记");
    const std::uint32_t bit = std::uint32_t{1} << source;
    state_ = active ? (state_ | bit) : (state_ & ~bit);
  }

  [[nodiscard]] bool active() const override { return state_ != 0; }

 private:
  contracts::hardware::SignalSource sourceCount_{0};
  std::uint32_t state_{0};
};

/** IRQ 或 NMI 线。 */
using WiredOrIrqLine = WiredOrLine<contracts::hardware::IrqLine>;

/** RDY 线。 */
using WiredOrRdyLine = WiredOrLine<contracts::hardware::RdyLine>;

}  // namespace ember::logic::hardware
