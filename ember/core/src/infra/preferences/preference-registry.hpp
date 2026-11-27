/*
 * ------------------------------------------------------------------------
 * 名称：preference-registry.hpp
 * 说明：首选项服务的实现：登记分组与设置项，校验取值，保存到 config/preferences.json，发出 preferences.changed 事件并通知核心里的订阅者。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：文件格式 { "version": 1, "values": { 设置项标识: 取值 } }；读不到、不是合法 JSON 或版本不认识时当作没有保存过，写一条错误日志，不影响启动；
 *       某个取值不符合登记的种类或范围时只丢弃这一项，改用默认值；先写临时文件再改名，避免写到一半留下半个文件；写不进去时取值仍在内存里生效，写一条错误日志。
 *       一把互斥锁保护全部状态，文件在锁内写入（文件很小），事件与订阅者的回调在锁外调用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "contracts/preferences/preference-service.hpp"
#include "platform/events/event-sink.hpp"
#include "platform/logging/error-log.hpp"

namespace ember::infra::preferences {

/** 首选项登记的参数。 */
struct PreferenceRegistryOptions {
  /** 保存取值的文件；为空时不读也不写（测试用）。 */
  std::filesystem::path file;
  /** 事件发布；为空时不发事件。 */
  std::shared_ptr<platform::EventSink> events;
  /** 错误日志；为空时不记录。 */
  std::shared_ptr<platform::ErrorLog> errors;
};

/** 首选项服务。 */
class PreferenceRegistry final : public contracts::preferences::PreferenceService {
 public:
  explicit PreferenceRegistry(PreferenceRegistryOptions options);

  void addGroup(contracts::preferences::GroupDefinition group) override;
  void addSetting(contracts::preferences::SettingDefinition setting) override;
  void subscribe(Listener listener) override;
  [[nodiscard]] contracts::preferences::SettingValue value(const std::string& id) const override;
  [[nodiscard]] std::vector<contracts::preferences::GroupDefinition> groups() const override;
  [[nodiscard]] std::vector<contracts::preferences::SettingEntry> entries() const override;
  contracts::preferences::SettingValue set(const std::string& id, const contracts::preferences::SettingValue& value) override;
  contracts::preferences::SettingValue reset(const std::string& id) override;

 private:
  /** 把取值改成 value 并保存；没有变化返回 false。已持有锁。 */
  bool assign(const std::string& id, const contracts::preferences::SettingValue& value, contracts::preferences::SettingValue& result);
  /** 取值变了之后在锁外通知：事件与订阅者。 */
  void announce(const std::string& id, const contracts::preferences::SettingValue& value, const std::vector<Listener>& listeners) const;
  void load();
  void save();
  void logError(const std::string& code, const std::string& message, const std::string& reason) const;

  PreferenceRegistryOptions options_;
  mutable std::mutex mutex_;
  std::vector<contracts::preferences::GroupDefinition> groups_;
  std::vector<contracts::preferences::SettingEntry> items_;
  std::vector<Listener> listeners_;
  /** 文件里读到的取值，登记设置项时按标识取用。 */
  nlohmann::json stored_ = nlohmann::json::object();
};

}  // namespace ember::infra::preferences
