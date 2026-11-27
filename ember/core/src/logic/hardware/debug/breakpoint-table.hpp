/*
 * ------------------------------------------------------------------------
 * 名称：breakpoint-table.hpp
 * 说明：断点表：执行、读、写三种断点，每个带地址范围、启用开关、条件表达式与命中计数；作为总线访问探针记录读写命中，执行断点由主机在指令之前查询。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：同一次运行里只记录第一个命中，取走（take）之后才记录下一个；各主机型号共用；
 *       条件表达式在命中的地址范围满足之后求值（经 setContext 给的上下文），结果为 0 或求值失败都当作没有命中，命中计数只算条件成立的命中。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "contracts/hardware/access-probe.hpp"
#include "contracts/hardware/machine-debugger.hpp"
#include "logic/hardware/debug/expression.hpp"

namespace ember::logic::hardware {

/** 断点表。 */
class BreakpointTable final : public contracts::hardware::AccessProbe {
 public:
  /** 断点数量上限。 */
  static constexpr std::size_t MAX_BREAKPOINTS = 256;

  /** 设置求值条件表达式用的主机状态；上下文必须活到本对象销毁。 */
  void setContext(const ExpressionContext* context) { context_ = context; }

  /**
   * 添加断点，规则见 MachineDebugger::addBreakpoint。
   * @throws shared::CoreError hardware.invalidBreakpoint、hardware.breakpointLimit、hardware.invalidCondition。
   */
  contracts::hardware::Breakpoint add(contracts::hardware::BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition);

  /** 修改启用状态或条件；没有这个编号时返回空。@throws shared::CoreError hardware.invalidCondition。 */
  std::optional<contracts::hardware::Breakpoint> update(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition);

  /** 删除断点；没有这个编号时返回 false。 */
  bool remove(std::uint32_t id);

  /** 删除全部断点，返回删除的个数；同时丢弃没有取走的命中。 */
  std::size_t clear();

  /** 全部断点，按编号升序。 */
  [[nodiscard]] std::vector<contracts::hardware::Breakpoint> list() const;

  /** 有没有启用的执行断点。 */
  [[nodiscard]] bool hasExecute() const { return executeCount_ > 0; }

  /** 有没有启用的读写断点（需要在总线上安装探针）。 */
  [[nodiscard]] bool hasAccess() const { return accessCount_ > 0; }

  /** 有没有启用的 irq、nmi、brk、reset 断点。 */
  [[nodiscard]] bool hasEvent() const { return eventCount_ > 0; }

  /** 有没有没取走的命中。 */
  [[nodiscard]] bool hasPending() const { return pending_.has_value(); }

  /**
   * 查询执行断点：pc 落在某个启用的执行断点的范围里、条件也成立就记录命中。
   * @return 命中返回 true。
   */
  bool hitsExecute(std::uint16_t pc);

  /**
   * 查询 irq、nmi、brk、reset 断点：有启用的同种断点、条件也成立就记录命中；已经有没取走的命中时不记。
   * @param kind irq、nmi、brk 或 reset。
   * @param address 处理程序的地址（reset 是复位后的 PC），条件里的 address 取它。
   * @return 命中返回 true。
   */
  bool hitsEvent(contracts::hardware::BreakpointKind kind, std::uint16_t address);

  /** 取走命中；没有时为空。 */
  std::optional<contracts::hardware::BreakpointHit> take();

  /** 丢弃没有取走的命中。 */
  void discardPending() { pending_.reset(); }

  void onAccess(contracts::hardware::AccessKind kind, std::uint16_t address, std::uint8_t value) override;

 private:
  struct Entry {
    contracts::hardware::Breakpoint info;
    /** 解析好的条件；没有条件时为空。 */
    std::optional<Expression> expression;
  };

  /** 条件成立（没有条件视为成立）。 */
  [[nodiscard]] bool conditionHolds(const Entry& entry, const AccessValues& access) const;

  /** 按启用的断点重新统计数量。 */
  void recount();

  std::vector<Entry> entries_;
  std::uint32_t nextId_{1};
  std::size_t executeCount_{0};
  std::size_t accessCount_{0};
  std::size_t eventCount_{0};
  const ExpressionContext* context_{nullptr};
  std::optional<contracts::hardware::BreakpointHit> pending_;
};

}  // namespace ember::logic::hardware
