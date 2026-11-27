/*
 * ------------------------------------------------------------------------
 * 名称：debug-session.cpp
 * 说明：调试连接会话逻辑的实现，见 debug-session.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/debug/debug-session.hpp"

#include <algorithm>
#include <cctype>

#include "contracts/debug/debug-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::debug {

namespace ws = platform::websocket;
namespace api = contracts::debug;

namespace {

/** 订阅的最大条数。 */
constexpr std::size_t MAX_SUBSCRIPTIONS = 256;

std::string dumpJson(const nlohmann::json& json) { return json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); }

/** 令牌比较：长度相同时逐字节比较全部内容，不因第一个不同的字节提前返回。 */
bool tokenEquals(const std::string& expected, const std::string& given) {
  if (expected.empty() || expected.size() != given.size()) return false;
  unsigned char difference = 0;
  for (std::size_t i = 0; i < expected.size(); ++i) difference |= static_cast<unsigned char>(expected[i] ^ given[i]);
  return difference == 0;
}

/** 数据里顶层的 data 字段是不是二进制值。 */
bool hasBinaryData(const nlohmann::json& json) { return json.is_object() && json.contains("data") && json["data"].is_binary(); }

/**
 * 生成要发送的消息：数据里带二进制 data 时发二进制帧，其余发文本帧。
 * @param message 完整的 JSON-RPC 报文。
 * @param containerKey 报文里可能含有二进制 data 的对象的键（result 或 params），没有时为空。
 */
Outgoing buildOutgoing(nlohmann::json message, const char* containerKey) {
  auto* container = containerKey != nullptr && message.contains(containerKey) ? &message[containerKey] : nullptr;
  if (container == nullptr || !hasBinaryData(*container)) {
    const auto text = dumpJson(message);
    return {ws::Opcode::Text, std::vector<std::uint8_t>(text.begin(), text.end())};
  }
  const auto& binary = (*container)["data"].get_binary();
  const std::vector<std::uint8_t> bytes(binary.begin(), binary.end());
  container->erase("data");
  const auto text = dumpJson(message);
  Outgoing outgoing{ws::Opcode::Binary, {}};
  outgoing.payload.reserve(4 + text.size() + bytes.size());
  const auto length = static_cast<std::uint32_t>(text.size());
  for (int shift = 24; shift >= 0; shift -= 8) outgoing.payload.push_back(static_cast<std::uint8_t>(length >> shift));
  outgoing.payload.insert(outgoing.payload.end(), text.begin(), text.end());
  outgoing.payload.insert(outgoing.payload.end(), bytes.begin(), bytes.end());
  return outgoing;
}

nlohmann::json errorObject(int rpcCode, const std::string& message, std::string_view code) {
  return {{"code", rpcCode}, {"message", message}, {"data", {{"code", code}}}};
}

Outgoing errorResponse(const nlohmann::json& id, int rpcCode, const std::string& message, std::string_view code) {
  nlohmann::json response{{"jsonrpc", "2.0"}, {"id", id}, {"error", errorObject(rpcCode, message, code)}};
  return buildOutgoing(std::move(response), nullptr);
}

/** 请求名中的模块名。 */
std::string moduleOf(const std::string& name) {
  const auto dot = name.find('.');
  return dot == std::string::npos ? std::string() : name.substr(0, dot);
}

bool isIdentifier(std::string_view text) noexcept {
  if (text.empty() || !std::islower(static_cast<unsigned char>(text.front()))) return false;
  return std::ranges::all_of(text, [](unsigned char ch) { return std::isalnum(ch) != 0; });
}

}  // namespace

bool subscriptionValid(std::string_view pattern) noexcept {
  if (pattern == "*") return true;
  const auto dot = pattern.find('.');
  if (dot == std::string_view::npos) return false;
  const auto module = pattern.substr(0, dot);
  const auto name = pattern.substr(dot + 1);
  return isIdentifier(module) && (name == "*" || isIdentifier(name));
}

bool subscriptionMatches(std::string_view pattern, std::string_view eventName) noexcept {
  if (pattern == "*" || pattern == eventName) return true;
  if (pattern.size() >= 2 && pattern.ends_with(".*")) return eventName.starts_with(pattern.substr(0, pattern.size() - 1));
  return false;
}

DebugSession::DebugSession(std::int64_t id, std::shared_ptr<const SessionContext> context) : id_(id), context_(std::move(context)) {}

bool DebugSession::authenticated() const {
  std::lock_guard lock(mutex_);
  return authenticated_;
}

std::string DebugSession::client() const {
  std::lock_guard lock(mutex_);
  return client_;
}

void DebugSession::cancel() noexcept {
  std::lock_guard lock(mutex_);
  current_.cancel();
}

HandleResult DebugSession::handleText(std::string_view text) {
  HandleResult result;
  const auto json = nlohmann::json::parse(text, nullptr, false);
  if (json.is_discarded()) {
    result.outgoing.push_back(errorResponse(nullptr, RPC_PARSE_ERROR, "报文不是合法的 JSON", "core.invalidParams"));
    return result;
  }
  if (!json.is_object()) {
    result.outgoing.push_back(errorResponse(nullptr, RPC_INVALID_REQUEST, "报文必须是 JSON-RPC 请求对象（不支持批量请求）", "core.invalidParams"));
    return result;
  }
  if (auto response = handleRequest(json, result)) result.outgoing.push_back(std::move(*response));
  return result;
}

std::optional<Outgoing> DebugSession::handleRequest(const nlohmann::json& request, HandleResult& result) {
  const bool hasId = request.contains("id");
  const nlohmann::json id = hasId ? request["id"] : nlohmann::json(nullptr);
  const bool idValid = !hasId || id.is_string() || id.is_number_integer() || id.is_null();

  const auto jsonrpc = request.find("jsonrpc");
  const auto method = request.find("method");
  if (!idValid || jsonrpc == request.end() || *jsonrpc != "2.0" || method == request.end() || !method->is_string()) {
    return errorResponse(idValid ? id : nlohmann::json(nullptr), RPC_INVALID_REQUEST, "不是合法的 JSON-RPC 2.0 请求", "core.invalidParams");
  }
  const auto params = request.contains("params") ? request["params"] : nlohmann::json::object();
  if (!params.is_object()) return errorResponse(id, RPC_INVALID_PARAMS, "params 必须是对象", "core.invalidParams");

  const auto name = method->get<std::string>();
  try {
    auto resultJson = callMethod(name, params, result);
    if (!hasId) return std::nullopt;
    nlohmann::json response{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(resultJson)}};
    return buildOutgoing(std::move(response), "result");
  } catch (const shared::CoreError& error) {
    if (!hasId) return std::nullopt;
    int rpcCode = RPC_APPLICATION_ERROR;
    if (error.code() == shared::ERROR_INVALID_PARAMS) rpcCode = RPC_INVALID_PARAMS;
    if (error.code() == shared::ERROR_UNKNOWN_REQUEST) rpcCode = RPC_METHOD_NOT_FOUND;
    return errorResponse(id, rpcCode, error.what(), error.code());
  } catch (const std::exception& error) {
    if (context_->errors) context_->errors->write({moduleOf(name), std::string(shared::ERROR_INTERNAL), error.what(), {{"requestName", name}, {"session", id_}}});
    if (!hasId) return std::nullopt;
    return errorResponse(id, RPC_INTERNAL_ERROR, "核心内部错误，详情已写入日志", shared::ERROR_INTERNAL);
  }
}

nlohmann::json DebugSession::callMethod(const std::string& method, const nlohmann::json& params, HandleResult& result) {
  if (!authenticated()) {
    if (method != api::DEBUG_HELLO_REQUEST) {
      result.closeCode = ws::CLOSE_UNAUTHORIZED;
      result.closeReason = "没有先完成 debug.hello";
      throw shared::CoreError(api::ERROR_UNAUTHORIZED, "请先调用 debug.hello 完成握手");
    }
    return hello(params, result);
  }
  if (method == api::DEBUG_HELLO_REQUEST) return hello(params, result);
  if (method == api::DEBUG_SUBSCRIBE_REQUEST) return changeSubscription(params, true);
  if (method == api::DEBUG_UNSUBSCRIBE_REQUEST) return changeSubscription(params, false);
  if (method == api::DEBUG_STATUS_REQUEST) {
    static_cast<void>(api::parseDebugStatusParams(params, "params"));
    return nlohmann::json(context_->status());
  }
  if (method == "rpc.discover") {
    if (!context_->discovery) throw shared::CoreError(shared::ERROR_UNKNOWN_REQUEST, "没有可用的接口描述文档");
    return *context_->discovery;
  }

  platform::CancellationToken token;
  {
    std::lock_guard lock(mutex_);
    current_ = platform::CancellationToken();
    token = current_;
  }
  return context_->dispatch(method, params, token);
}

nlohmann::json DebugSession::hello(const nlohmann::json& params, HandleResult& result) {
  const auto parsed = api::parseDebugHelloParams(params, "params");
  if (parsed.protocol.value_or(api::PROTOCOL_VERSION) != api::PROTOCOL_VERSION) throw shared::CoreError(api::ERROR_UNSUPPORTED_PROTOCOL, "不支持的协议版本，服务端只支持 1");
  if (!tokenEquals(context_->token(), parsed.token)) {
    result.closeCode = ws::CLOSE_UNAUTHORIZED;
    result.closeReason = "令牌不对";
    throw shared::CoreError(api::ERROR_UNAUTHORIZED, "令牌不对");
  }
  {
    std::lock_guard lock(mutex_);
    authenticated_ = true;
    client_ = parsed.client;
  }
  return nlohmann::json(api::DebugHelloResult{api::PROTOCOL_VERSION, context_->coreVersion, context_->appVersion, id_});
}

nlohmann::json DebugSession::changeSubscription(const nlohmann::json& params, bool add) {
  // 订阅与取消订阅的参数结构相同，用同一个解析函数。
  const auto parsed = api::parseDebugSubscribeParams(params, "params");
  for (const auto& pattern : parsed.events) {
    if (!subscriptionValid(pattern)) throw shared::SchemaError("事件名写法不对：" + pattern + "，应为 模块.事件、模块.* 或 *");
  }
  std::lock_guard lock(mutex_);
  for (const auto& pattern : parsed.events) {
    if (add) {
      if (subscriptions_.size() >= MAX_SUBSCRIPTIONS && !subscriptions_.contains(pattern)) throw shared::SchemaError("订阅的条数太多");
      subscriptions_.insert(pattern);
    } else {
      subscriptions_.erase(pattern);
    }
  }
  return nlohmann::json{{"events", std::vector<std::string>(subscriptions_.begin(), subscriptions_.end())}};
}

std::optional<Outgoing> DebugSession::eventMessage(std::string_view name, const nlohmann::json& data) const {
  {
    std::lock_guard lock(mutex_);
    if (!authenticated_ || !std::ranges::any_of(subscriptions_, [name](const std::string& pattern) { return subscriptionMatches(pattern, name); })) return std::nullopt;
  }
  nlohmann::json message{{"jsonrpc", "2.0"}, {"method", std::string(name)}, {"params", data}};
  return buildOutgoing(std::move(message), "params");
}

}  // namespace ember::infra::debug
