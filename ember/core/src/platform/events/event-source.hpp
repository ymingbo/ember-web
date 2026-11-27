/*
 * ------------------------------------------------------------------------
 * 名称：event-source.hpp
 * 说明：事件订阅的抽象：模块经它旁听核心发布的全部事件（调试服务据此把事件推给订阅的连接）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：回调在发布事件的线程里调用，可能是模拟线程，所以不能阻塞；只在核心启动时订阅，核心停止时清除。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace ember::platform {

/** 事件的订阅源。 */
class EventSource {
 public:
  /** 事件回调：事件名与数据。 */
  using Listener = std::function<void(const std::string& name, const nlohmann::json& data)>;

  virtual ~EventSource() = default;

  /** 订阅全部事件；只在核心启动时调用，回调保留到核心停止。 */
  virtual void subscribe(Listener listener) = 0;
};

}  // namespace ember::platform
