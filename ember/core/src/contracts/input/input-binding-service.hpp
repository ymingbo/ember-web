/*
 * ------------------------------------------------------------------------
 * 名称：input-binding-service.hpp
 * 说明：input 契约：手柄绑定服务。读取、整体替换与恢复默认的绑定，所有机型共用一份，保存在用户数据目录。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：读取返回不可变的快照（共享指针），调用方可以在任意线程长期持有；修改后发出 input.bindingsChanged 事件。
 *       数据类型由 schema/input.schema.json 生成，见 pad-bindings.hpp。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/input/pad-bindings.hpp"

namespace ember::contracts::input {

/** 手柄绑定服务；可在任意线程调用。 */
class InputBindingService {
 public:
  virtual ~InputBindingService() = default;

  /** 当前的绑定（两个玩家槽位）。 */
  [[nodiscard]] virtual std::shared_ptr<const PadSlots> bindings() const = 0;

  /**
   * 整体替换绑定：校验、保存、发出 input.bindingsChanged；内容没有变化时什么也不做。
   * @return 替换之后的绑定。
   * @throws shared::CoreError input.invalidBindings。
   */
  virtual std::shared_ptr<const PadSlots> set(PadSlots slots) = 0;

  /** 恢复出厂默认，其余同 set。 */
  virtual std::shared_ptr<const PadSlots> reset() = 0;
};

}  // namespace ember::contracts::input
