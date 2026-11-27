/*
 * ------------------------------------------------------------------------
 * 名称：breakpoint-table.cpp
 * 说明：断点表的实现，见 breakpoint-table.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/debug/breakpoint-table.hpp"

#include <algorithm>
#include <string>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

hw::Breakpoint BreakpointTable::add(hw::BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition) {
  const bool ranged = hw::breakpointHasAddress(kind);
  if (ranged && first > last) throw shared::CoreError(hw::ERROR_INVALID_BREAKPOINT, "断点地址范围的起点大于终点");
  if (entries_.size() >= MAX_BREAKPOINTS) throw shared::CoreError(hw::ERROR_BREAKPOINT_LIMIT, "断点最多 " + std::to_string(MAX_BREAKPOINTS) + " 个");
  Entry entry;
  if (!condition.empty()) entry.expression = Expression::parse(condition);
  entry.info.id = nextId_++;
  entry.info.kind = kind;
  entry.info.first = ranged ? first : 0;
  entry.info.last = ranged ? last : 0;
  entry.info.condition = condition;
  entries_.push_back(std::move(entry));
  recount();
  return entries_.back().info;
}

std::optional<hw::Breakpoint> BreakpointTable::update(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition) {
  const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const Entry& entry) { return entry.info.id == id; });
  if (found == entries_.end()) return std::nullopt;
  // 先解析新条件：写法不对时一项也不改。
  std::optional<Expression> expression;
  if (condition.has_value() && !condition->empty()) expression = Expression::parse(*condition);
  if (condition.has_value()) {
    found->expression = std::move(expression);
    found->info.condition = *condition;
  }
  if (enabled.has_value()) found->info.enabled = *enabled;
  recount();
  return found->info;
}

bool BreakpointTable::remove(std::uint32_t id) {
  const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const Entry& entry) { return entry.info.id == id; });
  if (found == entries_.end()) return false;
  entries_.erase(found);
  recount();
  return true;
}

std::size_t BreakpointTable::clear() {
  const std::size_t removed = entries_.size();
  entries_.clear();
  recount();
  pending_.reset();
  return removed;
}

std::vector<hw::Breakpoint> BreakpointTable::list() const {
  std::vector<hw::Breakpoint> list;
  list.reserve(entries_.size());
  for (const Entry& entry : entries_) list.push_back(entry.info);
  return list;
}

void BreakpointTable::recount() {
  executeCount_ = 0;
  accessCount_ = 0;
  eventCount_ = 0;
  for (const Entry& entry : entries_) {
    if (!entry.info.enabled) continue;
    switch (entry.info.kind) {
      case hw::BreakpointKind::Execute:
        ++executeCount_;
        break;
      case hw::BreakpointKind::Read:
      case hw::BreakpointKind::Write:
        ++accessCount_;
        break;
      default:
        ++eventCount_;
        break;
    }
  }
}

bool BreakpointTable::conditionHolds(const Entry& entry, const AccessValues& access) const {
  if (!entry.expression.has_value()) return true;
  if (context_ == nullptr) return false;
  const auto result = entry.expression->evaluate(*context_, access);
  return result.has_value() && *result != 0;
}

bool BreakpointTable::hitsExecute(std::uint16_t pc) {
  for (Entry& entry : entries_) {
    hw::Breakpoint& info = entry.info;
    if (!info.enabled || info.kind != hw::BreakpointKind::Execute || pc < info.first || pc > info.last) continue;
    if (!conditionHolds(entry, AccessValues{pc, 0, false})) continue;
    ++info.hits;
    pending_ = hw::BreakpointHit{info.id, info.kind, pc, 0};
    return true;
  }
  return false;
}

bool BreakpointTable::hitsEvent(hw::BreakpointKind kind, std::uint16_t address) {
  if (pending_.has_value()) return false;
  for (Entry& entry : entries_) {
    hw::Breakpoint& info = entry.info;
    if (!info.enabled || info.kind != kind) continue;
    if (!conditionHolds(entry, AccessValues{address, 0, false})) continue;
    ++info.hits;
    pending_ = hw::BreakpointHit{info.id, info.kind, address, 0};
    return true;
  }
  return false;
}

std::optional<hw::BreakpointHit> BreakpointTable::take() {
  auto hit = pending_;
  pending_.reset();
  return hit;
}

void BreakpointTable::onAccess(hw::AccessKind kind, std::uint16_t address, std::uint8_t value) {
  if (pending_.has_value()) return;
  const hw::BreakpointKind wanted = kind == hw::AccessKind::Read ? hw::BreakpointKind::Read : hw::BreakpointKind::Write;
  for (Entry& entry : entries_) {
    hw::Breakpoint& info = entry.info;
    if (!info.enabled || info.kind != wanted || address < info.first || address > info.last) continue;
    if (!conditionHolds(entry, AccessValues{address, value, kind == hw::AccessKind::Write})) continue;
    ++info.hits;
    pending_ = hw::BreakpointHit{info.id, info.kind, address, value};
    return;
  }
}

}  // namespace ember::logic::hardware
