/*
 * ------------------------------------------------------------------------
 * 名称：debug-host.hpp
 * 说明：调试服务的宿主：按配置（是否开启、端口、令牌）启动、重启或停止调试服务器，实现状态查询，状态变化时发布 debug.statusChanged 事件，并把核心的事件转给服务器推送。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：配置的来源（首选项或启动参数）由 bootstrap 里的模块决定，这里只按给定的配置执行；apply 可在任意线程调用，同一时刻只有一个 apply 在执行；
 *       停止服务器时不持有状态锁，因为服务器的线程在连接结束时会回调状态观察者。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "contracts/debug/debug-service.hpp"
#include "infra/debug/debug-server.hpp"
#include "platform/events/event-sink.hpp"
#include "platform/logging/error-log.hpp"

namespace ember::infra::debug {

/** 调试服务的配置。 */
struct DebugConfig {
  bool enabled{false};
  std::uint16_t port{0};
  std::string token;
};

/** 宿主的固定参数。 */
struct DebugHostOptions {
  std::filesystem::path endpointFile;
  std::string coreVersion;
  std::string appVersion;
};

/** 调试服务的宿主。 */
class DebugHost final : public contracts::debug::DebugService, public std::enable_shared_from_this<DebugHost> {
 public:
  DebugHost(DebugHostOptions options, Dispatcher dispatch, std::shared_ptr<const nlohmann::json> discovery, std::shared_ptr<platform::EventSink> events, std::shared_ptr<platform::ErrorLog> errors);
  ~DebugHost() override;

  /** 按配置调整：关闭则停止；开启则启动，端口变了就重启，令牌变了就更换令牌。开启失败（如端口被占用）时记录原因，状态里 listening 为 false。 */
  void apply(const DebugConfig& config);

  /** 停止服务（核心停止时调用）。 */
  void shutdown();

  /** 核心发布了一个事件：转给订阅的连接。 */
  void publishEvent(const std::string& name, const nlohmann::json& data);

  [[nodiscard]] contracts::debug::DebugStatusResult status() const override;

 private:
  void publishStatus();

  DebugHostOptions options_;
  Dispatcher dispatch_;
  std::shared_ptr<const nlohmann::json> discovery_;
  std::shared_ptr<platform::EventSink> events_;
  std::shared_ptr<platform::ErrorLog> errors_;

  /** 串行化 apply 与 shutdown。 */
  std::mutex applyMutex_;
  /** 保护下面的状态；只做短暂的读写。 */
  mutable std::mutex mutex_;
  std::shared_ptr<DebugServer> server_;
  DebugConfig config_;
  std::string lastError_;
};

}  // namespace ember::infra::debug
