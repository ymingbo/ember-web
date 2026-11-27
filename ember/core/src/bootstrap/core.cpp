/*
 * ------------------------------------------------------------------------
 * 名称：core.cpp
 * 说明：核心入口的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#include "bootstrap/core.hpp"

#include <exception>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "bootstrap/input-message.hpp"
#include "bootstrap/module-list.hpp"
#include "bootstrap/module-registry.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "platform/events/event-sink.hpp"
#include "platform/events/event-source.hpp"
#include "platform/logging/file-error-log.hpp"
#include "platform/requests/cancellation.hpp"
#include "platform/requests/request-router.hpp"
#include "platform/threading/thread-pool.hpp"
#include "platform/time/system-clock.hpp"
#include "shared/errors/core-error.hpp"
#include "shared/json/json-fields.hpp"

#ifndef EMBER_CORE_VERSION
#define EMBER_CORE_VERSION "0.0.0-dev"
#endif

namespace ember::bootstrap {

namespace json_fields = shared::json_fields;

/** 把模块发布的事件转给事件处理函数，并转给其他订阅者（调试服务）。 */
class CoreEvents final : public platform::EventSink, public platform::EventSource {
 public:
  void setHandler(Core::EventHandler handler) {
    std::lock_guard lock(mutex_);
    handler_ = std::move(handler);
  }

  void subscribe(Listener listener) override {
    std::lock_guard lock(mutex_);
    listeners_.push_back(std::move(listener));
  }

  void clearListeners() {
    std::lock_guard lock(mutex_);
    listeners_.clear();
  }

  void publish(std::string_view name, const nlohmann::json& data) override {
    Core::EventHandler handler;
    std::vector<Listener> listeners;
    {
      std::lock_guard lock(mutex_);
      handler = handler_;
      listeners = listeners_;
    }
    if (handler) handler(std::string(name), data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    const std::string eventName(name);
    for (const auto& listener : listeners) {
      try {
        listener(eventName, data);
      } catch (...) {
        // 订阅者失败不影响事件的发布和其他订阅者。
      }
    }
  }

 private:
  std::mutex mutex_;
  Core::EventHandler handler_;
  std::vector<Listener> listeners_;
};

/** 把运行模块发布的帧转给帧处理函数；没有处理函数时丢弃。 */
class CoreFrames final : public platform::FrameOutput {
 public:
  void setHandler(Core::FrameHandler handler) {
    auto shared = handler ? std::make_shared<const Core::FrameHandler>(std::move(handler)) : nullptr;
    std::lock_guard lock(mutex_);
    handler_ = std::move(shared);
  }

  void publish(const platform::FrameView& frame) noexcept override {
    std::shared_ptr<const Core::FrameHandler> handler;
    {
      std::lock_guard lock(mutex_);
      handler = handler_;
    }
    if (handler == nullptr) return;
    try {
      (*handler)(frame);
    } catch (...) {
      // 处理函数失败时这一帧就丢弃，不影响模拟。
    }
  }

 private:
  std::mutex mutex_;
  std::shared_ptr<const Core::FrameHandler> handler_;
};

namespace {

/** 错误响应的 JSON 文本。 */
std::string errorPayload(std::string_view code, const std::string& message) {
  return nlohmann::json{{"code", code}, {"message", message}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

/** 请求名中的模块名。 */
std::string moduleOf(const std::string& name) {
  const auto dot = name.find('.');
  return dot == std::string::npos ? std::string() : name.substr(0, dot);
}

}  // namespace

Core::Core() : events_(std::make_shared<CoreEvents>()), frames_(std::make_shared<CoreFrames>()) {}

Core::~Core() { stop(); }

void Core::setEventHandler(EventHandler handler) { events_->setHandler(std::move(handler)); }

void Core::setFrameHandler(FrameHandler handler) { frames_->setHandler(std::move(handler)); }

void Core::sendInput(const std::string& messageJson) {
  const auto event = parseInputMessage(messageJson);
  std::shared_ptr<contracts::runtime::RuntimeService> runtime;
  {
    std::lock_guard lock(mutex_);
    if (!started_ || stopping_) throw shared::CoreError(shared::ERROR_UNAVAILABLE, "核心未启动或正在停止");
    runtime = runtime_;
  }
  runtime->sendInput(event);
}

std::string Core::start(const std::string& optionsJson) {
  std::lock_guard lock(mutex_);
  if (started_ || stopping_) throw shared::CoreError(shared::ERROR_UNAVAILABLE, "核心已启动过");

  const auto options = nlohmann::json::parse(optionsJson, nullptr, false);
  if (options.is_discarded()) throw shared::SchemaError("启动参数不是合法的 JSON");
  json_fields::requireObject(options, "options");
  json_fields::rejectUnknown(options, "options", {"userDataPath", "appVersion", "workerCount", "bundledPluginsPath", "debugPort", "debugToken"});
  const auto* userDataField = json_fields::find(options, "userDataPath");
  if (userDataField == nullptr) throw shared::SchemaError("options.userDataPath 缺少");
  const auto userDataText = json_fields::readString(*userDataField, "options.userDataPath", 1, std::nullopt, {});
  const auto* appVersionField = json_fields::find(options, "appVersion");
  if (appVersionField == nullptr) throw shared::SchemaError("options.appVersion 缺少");
  const auto appVersion = json_fields::readString(*appVersionField, "options.appVersion", 1, std::nullopt, {});
  const auto* workerField = json_fields::find(options, "workerCount");
  const auto workerCount = workerField == nullptr ? 0 : json_fields::readInteger(*workerField, "options.workerCount", 1, 64);
  const auto* bundledField = json_fields::find(options, "bundledPluginsPath");
  const auto bundledText = bundledField == nullptr ? std::string() : json_fields::readString(*bundledField, "options.bundledPluginsPath", 1, std::nullopt, {});
  const auto* debugPortField = json_fields::find(options, "debugPort");
  std::optional<std::uint16_t> debugPort;
  if (debugPortField != nullptr) debugPort = static_cast<std::uint16_t>(json_fields::readInteger(*debugPortField, "options.debugPort", 1, 65535));
  const auto* debugTokenField = json_fields::find(options, "debugToken");
  const auto debugToken = debugTokenField == nullptr ? std::string() : json_fields::readString(*debugTokenField, "options.debugToken", 1, 128, {});

  const std::filesystem::path userDataPath(std::u8string(userDataText.begin(), userDataText.end()));
  auto clock = std::make_shared<platform::SystemClock>();
  auto errors = std::make_shared<platform::FileErrorLog>(userDataPath / "logs", clock);
  auto router = std::make_unique<platform::RequestRouter>();

  std::vector<std::function<void()>> stopHandlers;
  std::vector<std::function<void()>> startHandlers;
  ModuleServices services;
  ModuleRegistry registry{*router, events_, errors, clock, appVersion, EMBER_CORE_VERSION, userDataPath, std::filesystem::path(std::u8string(bundledText.begin(), bundledText.end())), frames_, services, stopHandlers, startHandlers, events_, debugPort, debugToken};
  try {
    for (const auto& module : moduleList()) module.registerModule(registry);
    runtime_ = services.require<contracts::runtime::RuntimeService>();
    for (const auto& handler : startHandlers) handler();
  } catch (...) {
    for (auto it = stopHandlers.rbegin(); it != stopHandlers.rend(); ++it) (*it)();
    throw;
  }

  stopHandlers_ = std::move(stopHandlers);
  errors_ = std::move(errors);
  router_ = std::move(router);
  pool_ = std::make_unique<platform::ThreadPool>(static_cast<std::size_t>(workerCount));
  started_ = true;

  return nlohmann::json{{"coreVersion", EMBER_CORE_VERSION}, {"requests", router_->names()}}.dump();
}

void Core::request(const std::string& id, const std::string& name, const std::string& paramsJson, RequestDone done) {
  std::shared_ptr<platform::CancellationToken> token;
  {
    std::lock_guard lock(mutex_);
    if (!started_ || stopping_) {
      done(false, errorPayload(shared::ERROR_UNAVAILABLE, "核心未启动或正在停止"));
      return;
    }
    if (running_.count(id) > 0) {
      done(false, errorPayload(shared::ERROR_INVALID_PARAMS, "请求标识重复：" + id));
      return;
    }
    token = std::make_shared<platform::CancellationToken>();
    running_.emplace(id, token);
    const bool posted = pool_->post([this, id, name, paramsJson, token, done] {
      run(id, name, paramsJson, *token, done);
      std::lock_guard finished(mutex_);
      running_.erase(id);
    });
    if (posted) return;
    running_.erase(id);
  }
  done(false, errorPayload(shared::ERROR_UNAVAILABLE, "核心正在停止"));
}

void Core::run(const std::string& id, const std::string& name, const std::string& paramsJson, const platform::CancellationToken& token, const RequestDone& done) {
  std::string payload;
  bool ok = false;
  try {
    token.throwIfCancelled();
    const auto params = nlohmann::json::parse(paramsJson, nullptr, false);
    if (params.is_discarded()) throw shared::SchemaError("params 不是合法的 JSON");
    const auto result = router_->dispatch(name, params, token);
    token.throwIfCancelled();
    payload = result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    ok = true;
  } catch (const shared::CoreError& error) {
    payload = errorPayload(error.code(), error.what());
  } catch (const std::exception& error) {
    errors_->write({moduleOf(name), std::string(shared::ERROR_INTERNAL), error.what(), {{"requestName", name}, {"requestId", id}}});
    payload = errorPayload(shared::ERROR_INTERNAL, "核心内部错误，详情已写入日志");
  } catch (...) {
    errors_->write({moduleOf(name), std::string(shared::ERROR_INTERNAL), "未知异常", {{"requestName", name}, {"requestId", id}}});
    payload = errorPayload(shared::ERROR_INTERNAL, "核心内部错误，详情已写入日志");
  }
  try {
    done(ok, std::move(payload));
  } catch (...) {
    // 回调失败时无法再通知调用方。
  }
}

void Core::cancel(const std::string& id) {
  std::lock_guard lock(mutex_);
  const auto it = running_.find(id);
  if (it != running_.end()) it->second->cancel();
}

void Core::stop() {
  {
    std::lock_guard lock(mutex_);
    if (!started_ || stopping_) return;
    stopping_ = true;
  }
  // 后登记的模块可能依赖先登记的模块，按相反顺序停下。
  for (auto it = stopHandlers_.rbegin(); it != stopHandlers_.rend(); ++it) (*it)();
  pool_->stop();
  std::lock_guard lock(mutex_);
  pool_.reset();
  router_.reset();
  runtime_.reset();
  stopHandlers_.clear();
  errors_.reset();
  events_->setHandler(nullptr);
  events_->clearListeners();
  frames_->setHandler(nullptr);
  started_ = false;
}

}  // namespace ember::bootstrap
