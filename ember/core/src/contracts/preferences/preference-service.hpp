/*
 * ------------------------------------------------------------------------
 * 名称：preference-service.hpp
 * 说明：preferences 契约：首选项服务。各模块启动时登记自己的分组与设置项，之后读取、修改、恢复默认，并订阅取值的变化。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：设置项的取值种类有三种：choice（从登记的可选值里选一个字符串）、switch（布尔值）、integer（范围内的整数）；
 *       取值保存在用户数据目录的 config/preferences.json；登记只在核心启动时进行，登记之后的读取、修改可在任意线程调用；
 *       数据类型由 schema/preferences.schema.json 生成到 generated/（界面看到的是生成的类型，核心里的模块用这里的类型）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace ember::contracts::preferences {

/** 设置项的取值：choice 为字符串，switch 为布尔值，integer 为整数。 */
using SettingValue = std::variant<bool, std::int64_t, std::string>;

/** 设置项的种类。 */
enum class SettingKind : std::uint8_t { Choice, Switch, Integer, Text };

/** 一个分组。 */
struct GroupDefinition {
  /** 分组标识，如 sound。 */
  std::string id;
  /** 显示名称。 */
  std::string label;
};

/** choice 的一个可选值。 */
struct OptionDefinition {
  std::string value;
  std::string label;
};

/** 登记一个设置项所需的内容。 */
struct SettingDefinition {
  /** 设置项标识：分组标识加点再加名称，如 sound.buffer；全局不重复。 */
  std::string id;
  /** 所属分组，必须已登记。 */
  std::string group;
  /** 显示名称。 */
  std::string label;
  /** 一两句话的说明。 */
  std::string description;
  SettingKind kind{SettingKind::Switch};
  /** choice 的可选值。 */
  std::vector<OptionDefinition> options;
  /** integer 的范围。 */
  std::int64_t minimum{0};
  std::int64_t maximum{0};
  /** text 的最大字节数（UTF-8）。 */
  std::int64_t maxLength{256};
  /** 首选项页不显示，只保存取值。 */
  bool hidden{false};
  /** text 输入框旁的按钮：取值是文件夹路径时填 directory（“选择”）；取值由核心生成、默认值为空时填 regenerate（“更换”，页面请求恢复默认，核心随后重新生成）；空表示没有。 */
  std::string picker;
  /** 出厂默认值，必须符合种类与范围。 */
  SettingValue defaultValue{false};
};

/** 一个设置项：登记的内容与当前取值。 */
struct SettingEntry {
  SettingDefinition definition;
  SettingValue value;
};

/** 首选项服务。 */
class PreferenceService {
 public:
  /** 取值变化的回调：设置项标识与新的取值；在修改的线程里、不持有锁时调用，不能阻塞；可以再修改首选项（会再通知一次）。 */
  using Listener = std::function<void(const std::string& id, const SettingValue& value)>;

  virtual ~PreferenceService() = default;

  /**
   * 登记一个分组；只在核心启动时调用，分组按登记的顺序排列。
   * @throws std::logic_error 分组标识重复。
   */
  virtual void addGroup(GroupDefinition group) = 0;

  /**
   * 登记一个设置项；只在核心启动时调用。已保存的取值（文件里读到的）合格就采用，否则用默认值。
   * @throws std::logic_error 标识重复、分组没有登记、默认值不合格。
   */
  virtual void addSetting(SettingDefinition setting) = 0;

  /** 订阅取值的变化；只在核心启动时调用，回调一直保留到核心停止。 */
  virtual void subscribe(Listener listener) = 0;

  /**
   * 当前取值。
   * @throws shared::CoreError preferences.unknown。
   */
  [[nodiscard]] virtual SettingValue value(const std::string& id) const = 0;

  /** 全部分组，按登记的顺序。 */
  [[nodiscard]] virtual std::vector<GroupDefinition> groups() const = 0;

  /** 全部设置项与当前取值，按登记的顺序。 */
  [[nodiscard]] virtual std::vector<SettingEntry> entries() const = 0;

  /**
   * 修改取值：校验、保存、发出 preferences.changed 事件并通知订阅者；取值没有变化时什么也不做。
   * @return 通知订阅者之后的取值（订阅者可能又改了它，如令牌置空后重新生成）。
   * @throws shared::CoreError preferences.unknown、preferences.invalidValue。
   */
  virtual SettingValue set(const std::string& id, const SettingValue& value) = 0;

  /**
   * 恢复默认值，其余同 set。
   * @throws shared::CoreError preferences.unknown。
   */
  virtual SettingValue reset(const std::string& id) = 0;
};

}  // namespace ember::contracts::preferences
