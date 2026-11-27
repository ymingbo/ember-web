/*
 * ------------------------------------------------------------------------
 * 名称：debug-host.cpp
 * 说明：调试服务宿主的实现，见 debug-host.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/debug/debug-host.hpp"

#include <utility>

namespace ember::infra::debug {

namespace api = contracts::debug;

DebugHost::DebugHost(DebugHostOptions options, Dispatcher dispatch, std::shared_ptr<const nlohmann::json> discovery, std::shared_ptr<platform::EventSink> events, std::shared_ptr<platform::ErrorLog> errors)
    : options_(std::move(options)), dispatch_(std::move(dispatch)), discovery_(std::move(discovery)), events_(std::move(events)), errors_(std::move(errors)) {}

DebugHost::~DebugHost() { shutdown(); }

void DebugHost::apply(const DebugConfig& config) {
  const std::lock_guard applying(applyMutex_);
  std::shared_ptr<DebugServer> current;
  DebugConfig previous;
  {
    const std::lock_guard lock(mutex_);
    current = server_;
    previous = config_;
  }

  std::shared_ptr<DebugServer> next = current;
  std::string error;
  const bool keep = config.enabled && current != nullptr && current->listening() && previous.port == config.port;
  if (keep) {
    current->setToken(config.token);
  } else {
    next.reset();
    if (config.enabled) {
      DebugServerOptions serverOptions;
      serverOptions.port = config.port;
      serverOptions.token = config.token;
      serverOptions.endpointFile = options_.endpointFile;
      serverOptions.coreVersion = options_.coreVersion;
      serverOptions.appVersion = options_.appVersion;
      next = std::make_shared<DebugServer>(std::move(serverOptions), dispatch_, discovery_, errors_);
    }
  }

  // 旧服务器要先停下（释放端口、关闭连接），再启动新的；停止时不能持有状态锁。
  if (current != nullptr && !keep) current->stop();
  if (next != nullptr && next != current) {
    next->setStatusObserver([weak = weak_from_this()] {
      if (const auto host = weak.lock()) host->publishStatus();
    });
    if (!next->start(error)) {
      if (errors_) errors_->write({"debug", "debug.listenFailed", error, {{"port", config.port}}});
      next.reset();
    }
  }
  {
    const std::lock_guard lock(mutex_);
    server_ = std::move(next);
    config_ = config;
    lastError_ = error;
  }
  publishStatus();
}

void DebugHost::shutdown() {
  const std::lock_guard applying(applyMutex_);
  std::shared_ptr<DebugServer> current;
  {
    const std::lock_guard lock(mutex_);
    current = std::move(server_);
    server_.reset();
    config_.enabled = false;
  }
  if (current != nullptr) current->stop();
}

void DebugHost::publishEvent(const std::string& name, const nlohmann::json& data) {
  std::shared_ptr<DebugServer> current;
  {
    const std::lock_guard lock(mutex_);
    current = server_;
  }
  if (current != nullptr) current->publishEvent(name, data);
}

api::DebugStatusResult DebugHost::status() const {
  std::shared_ptr<DebugServer> current;
  api::DebugStatusResult result;
  {
    const std::lock_guard lock(mutex_);
    current = server_;
    result.enabled = config_.enabled;
    if (!lastError_.empty()) result.error = lastError_;
  }
  if (current != nullptr) {
    const auto serverStatus = current->status();
    result.listening = serverStatus.listening;
    result.port = serverStatus.port;
    result.connections = serverStatus.connections;
  }
  return result;
}

void DebugHost::publishStatus() {
  if (events_ == nullptr) return;
  const auto current = status();
  events_->publish(api::DEBUG_STATUS_CHANGED_EVENT, nlohmann::json(api::DebugStatusChangedData{current.enabled, current.listening, current.port, static_cast<std::int64_t>(current.connections.size())}));
}

}  // namespace ember::infra::debug
