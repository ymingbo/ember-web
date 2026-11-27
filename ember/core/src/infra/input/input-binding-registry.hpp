/*
 * ------------------------------------------------------------------------
 * 名称：input-binding-registry.hpp
 * 说明：手柄绑定服务的实现：校验绑定，保存到 config/input-bindings.json，发出 input.bindingsChanged 事件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：文件格式 { "version": 1, "slots": [ 玩家槽位 ] }；读不到、不是合法 JSON、版本不认识或内容不合格时用出厂默认并写一条错误日志，不影响启动；
 *       先写临时文件再改名；写不进去时绑定仍在内存里生效，写一条错误日志。一把互斥锁保护全部状态，文件在锁内写入（文件很小），事件在锁外发出。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>

#include "contracts/input/input-binding-service.hpp"
#include "platform/events/event-sink.hpp"
#include "platform/logging/error-log.hpp"

namespace ember::infra::input {

/** 手柄绑定登记的参数。 */
struct InputBindingRegistryOptions {
  /** 保存绑定的文件；为空时不读也不写（测试用）。 */
  std::filesystem::path file;
  /** 事件发布；为空时不发事件。 */
  std::shared_ptr<platform::EventSink> events;
  /** 错误日志；为空时不记录。 */
  std::shared_ptr<platform::ErrorLog> errors;
};

/** 手柄绑定服务。 */
class InputBindingRegistry final : public contracts::input::InputBindingService {
 public:
  explicit InputBindingRegistry(InputBindingRegistryOptions options);

  [[nodiscard]] std::shared_ptr<const contracts::input::PadSlots> bindings() const override;
  std::shared_ptr<const contracts::input::PadSlots> set(contracts::input::PadSlots slots) override;
  std::shared_ptr<const contracts::input::PadSlots> reset() override;

  /**
   * 检查绑定是否合格。
   * @param slots 要检查的绑定。
   * @param reason 不合格时写入原因（面向用户）。
   * @return 合格返回 true。
   */
  [[nodiscard]] static bool validate(const contracts::input::PadSlots& slots, std::string& reason);

 private:
  /** 换成新的绑定并保存；没有变化返回 false。已持有锁。 */
  bool assign(contracts::input::PadSlots slots);
  void load();
  void save() const;
  void announce(const std::shared_ptr<const contracts::input::PadSlots>& slots) const;
  void logError(const std::string& code, const std::string& message, const std::string& reason) const;

  InputBindingRegistryOptions options_;
  mutable std::mutex mutex_;
  std::shared_ptr<const contracts::input::PadSlots> current_;
};

}  // namespace ember::infra::input
