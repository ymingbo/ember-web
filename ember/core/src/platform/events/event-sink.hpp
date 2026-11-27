/*
 * ------------------------------------------------------------------------
 * 名称：event-sink.hpp
 * 说明：事件发布的抽象：模块经它发布事件，由核心入口转发给 JavaScript。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：事件名为“模块名.事件”，数据结构在模块的 Schema 中定义。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

namespace ember::platform {

/** 事件发布。 */
class EventSink {
 public:
  virtual ~EventSink() = default;

  /** 发布事件；可在任意线程调用。 */
  virtual void publish(std::string_view name, const nlohmann::json& data) = 0;
};

}  // namespace ember::platform
