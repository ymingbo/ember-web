/*
 * ------------------------------------------------------------------------
 * 名称：state-watcher.cpp
 * 说明：状态监视的实现，见 state-watcher.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/inspector/state-watcher.hpp"

#include <algorithm>
#include <optional>

#include "contracts/hardware/hardware-error.hpp"
#include "contracts/inspector/inspector-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::inspector {

namespace api = contracts::inspector;
namespace hw = contracts::hardware;
namespace rt = contracts::runtime;

std::int64_t StateWatcher::read(rt::DebugContext& context, const std::string& path) {
  const auto dot = path.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 == path.size()) {
    throw shared::CoreError(hw::ERROR_INVALID_STATE, "状态路径应写成 <模块>.<状态项>：" + path);
  }
  const std::string module = path.substr(0, dot);
  const std::string field = path.substr(dot + 1);
  if (module == "mapper") {
    const auto value = context.cartridge == nullptr ? std::nullopt : context.cartridge->state(field);
    if (!value.has_value()) throw shared::CoreError(hw::ERROR_INVALID_STATE, "卡带的脚本没有状态项：" + field);
    return static_cast<std::int64_t>(*value);
  }
  for (const hw::StateField& item : context.debugger.readState(module)) {
    if (item.name == field) return item.value;
  }
  throw shared::CoreError(hw::ERROR_INVALID_STATE, "模块 " + module + " 没有状态项：" + field);
}

api::WatchEntry StateWatcher::add(rt::DebugContext& context, const std::string& path) {
  const auto value = read(context, path);
  const auto existing = std::find_if(entries_.begin(), entries_.end(), [&](const api::WatchEntry& entry) { return entry.path == path; });
  if (existing != entries_.end()) return {path, value};
  if (entries_.size() >= MAX_WATCHES) throw shared::CoreError(api::ERROR_WATCH_LIMIT, "监视最多 " + std::to_string(MAX_WATCHES) + " 项");
  entries_.push_back({path, value});
  return entries_.back();
}

void StateWatcher::remove(const std::string& path) {
  const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const api::WatchEntry& entry) { return entry.path == path; });
  if (found == entries_.end()) throw shared::CoreError(api::ERROR_NO_WATCH, "没有监视 " + path);
  entries_.erase(found);
}

std::vector<StateWatcher::Change> StateWatcher::observe(rt::DebugContext& context) {
  std::vector<Change> changes;
  for (api::WatchEntry& entry : entries_) {
    std::int64_t value = 0;
    try {
      value = read(context, entry.path);
    } catch (const shared::CoreError&) {
      continue;
    }
    if (value == entry.value) continue;
    changes.push_back({entry.path, entry.value, value});
    entry.value = value;
  }
  return changes;
}

}  // namespace ember::logic::inspector
