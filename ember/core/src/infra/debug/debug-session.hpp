/*
 * ------------------------------------------------------------------------
 * 名称：debug-session.hpp
 * 说明：一条调试连接的会话逻辑：把 WebSocket 文本消息当作 JSON-RPC 2.0 请求处理（握手、令牌、订阅、发现、分发到核心的请求），生成要发回的消息，并按订阅过滤事件；不碰网络，便于测试。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：同一条连接上的请求由连接的读线程按到达顺序逐个处理；不支持批量请求（数组）。
 *       结果或事件数据里顶层的 data 字段如果是二进制值（nlohmann::json::binary），就改发二进制帧：
 *       [4 字节大端的 JSON 长度][JSON 报文（不含 data）][data 的原始字节]。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "contracts/debug/generated/debug.hpp"
#include "platform/logging/error-log.hpp"
#include "platform/network/websocket.hpp"
#include "platform/requests/cancellation.hpp"

namespace ember::infra::debug {

/** 要发给客户端的一个 WebSocket 消息。 */
struct Outgoing {
  platform::websocket::Opcode opcode{platform::websocket::Opcode::Text};
  std::vector<std::uint8_t> payload;
};

/** 分发一个核心请求；异常按核心的约定（CoreError、SchemaError）抛出。 */
using Dispatcher = std::function<nlohmann::json(std::string_view name, const nlohmann::json& params, const platform::CancellationToken& token)>;

/** 全部会话共用的环境。 */
struct SessionContext {
  Dispatcher dispatch;
  /** rpc.discover 返回的文档（OpenRPC）；没有时 rpc.discover 报方法不存在。 */
  std::shared_ptr<const nlohmann::json> discovery;
  std::shared_ptr<platform::ErrorLog> errors;
  std::string coreVersion;
  std::string appVersion;
  /** 当前的令牌；每次握手都重新取，令牌更换后新握手立即按新令牌检查。 */
  std::function<std::string()> token;
  /** debug.status 的结果。 */
  std::function<contracts::debug::DebugStatusResult()> status;
};

/** 处理一条消息的结果。 */
struct HandleResult {
  std::vector<Outgoing> outgoing;
  /** 有值时要在发完 outgoing 后关闭连接。 */
  std::optional<std::uint16_t> closeCode;
  std::string closeReason;
};

/** JSON-RPC 的错误码。 */
inline constexpr int RPC_PARSE_ERROR = -32700;
inline constexpr int RPC_INVALID_REQUEST = -32600;
inline constexpr int RPC_METHOD_NOT_FOUND = -32601;
inline constexpr int RPC_INVALID_PARAMS = -32602;
inline constexpr int RPC_INTERNAL_ERROR = -32603;
/** 应用错误：具体的错误码放在 error.data.code。 */
inline constexpr int RPC_APPLICATION_ERROR = -32000;

/** 一条调试连接的会话。 */
class DebugSession {
 public:
  /**
   * @param id 连接编号。
   * @param context 共用的环境。
   */
  DebugSession(std::int64_t id, std::shared_ptr<const SessionContext> context);

  /** 处理客户端发来的一条文本消息。 */
  [[nodiscard]] HandleResult handleText(std::string_view text);

  /** 连接被关闭时调用：取消正在处理的请求。 */
  void cancel() noexcept;

  /** 事件的消息：本连接订阅了才有；不在读线程里调用也安全。 */
  [[nodiscard]] std::optional<Outgoing> eventMessage(std::string_view name, const nlohmann::json& data) const;

  [[nodiscard]] std::int64_t id() const noexcept { return id_; }
  /** 是否已通过握手。 */
  [[nodiscard]] bool authenticated() const;
  /** 客户端自报的名称；握手前为空。 */
  [[nodiscard]] std::string client() const;

 private:
  /** 处理一个请求对象；通知（没有 id）没有返回值。 */
  std::optional<Outgoing> handleRequest(const nlohmann::json& request, HandleResult& result);
  nlohmann::json callMethod(const std::string& method, const nlohmann::json& params, HandleResult& result);
  nlohmann::json hello(const nlohmann::json& params, HandleResult& result);
  nlohmann::json changeSubscription(const nlohmann::json& params, bool add);

  std::int64_t id_;
  std::shared_ptr<const SessionContext> context_;
  mutable std::mutex mutex_;
  bool authenticated_{false};
  std::string client_;
  std::set<std::string> subscriptions_;
  platform::CancellationToken current_;
};

/** 事件名是否匹配订阅的写法：完全相同、“模块.*”或“*”。 */
[[nodiscard]] bool subscriptionMatches(std::string_view pattern, std::string_view eventName) noexcept;

/** 订阅写法是否合法：“*”、“模块.*”或“模块.事件”。 */
[[nodiscard]] bool subscriptionValid(std::string_view pattern) noexcept;

}  // namespace ember::infra::debug
