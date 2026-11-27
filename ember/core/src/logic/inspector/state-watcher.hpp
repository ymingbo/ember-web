/*
 * ------------------------------------------------------------------------
 * 名称：state-watcher.hpp
 * 说明：状态监视：记住监视的状态路径与上一次的取值，每次观察时找出取值变了的项。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：只在模拟线程使用，不加锁；路径写法 <模块>.<状态项>，模块是 cpu、ppu 等，卡带脚本的状态是 mapper。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "contracts/inspector/inspector-service.hpp"
#include "contracts/runtime/runtime-service.hpp"

namespace ember::logic::inspector {

/** 状态监视。 */
class StateWatcher {
 public:
  /** 监视项的上限。 */
  static constexpr std::size_t MAX_WATCHES = 64;

  /** 一项取值的变化。 */
  struct Change {
    std::string path;
    std::int64_t oldValue{0};
    std::int64_t newValue{0};
  };

  /**
   * 读一个状态路径的当前取值。
   * @throws shared::CoreError hardware.invalidState：写法不对、模块或状态项不存在。
   */
  static std::int64_t read(contracts::runtime::DebugContext& context, const std::string& path);

  /**
   * 开始监视并记下当前取值；已在监视时只返回当前取值。
   * @throws shared::CoreError hardware.invalidState、inspector.watchLimit。
   */
  contracts::inspector::WatchEntry add(contracts::runtime::DebugContext& context, const std::string& path);

  /** 取消监视。@throws shared::CoreError inspector.noWatch。 */
  void remove(const std::string& path);

  /** 读全部监视项，返回取值变了的，并记下新值；读不到的项（如换了卡带后没有这个状态）保持原值。 */
  std::vector<Change> observe(contracts::runtime::DebugContext& context);

  /** 全部监视项与记下的取值，按添加的先后。 */
  [[nodiscard]] const std::vector<contracts::inspector::WatchEntry>& list() const { return entries_; }

 private:
  std::vector<contracts::inspector::WatchEntry> entries_;
};

}  // namespace ember::logic::inspector
