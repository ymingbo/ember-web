/*
 * ------------------------------------------------------------------------
 * 名称：websocket.hpp
 * 说明：WebSocket（RFC 6455）的纯逻辑：服务端一侧的握手请求解析与检查、握手响应，客户端一侧的握手请求与响应校验，帧的编码与解码（含分片、控制帧与掩码），不碰网络。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：只实现调试服务需要的部分：不支持扩展与压缩，子协议只回显客户端请求里的 ember-debug.v1；
 *       客户端发来的帧必须带掩码，服务端发出的帧不带（解码器按角色检查）；文本消息必须是合法的 UTF-8；单条消息的大小上限由调用方指定。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ember::platform::websocket {

/** SHA-1 摘要（握手用，不用于安全目的）。 */
[[nodiscard]] std::array<std::uint8_t, 20> sha1(std::string_view data);

/** Base64 编码。 */
[[nodiscard]] std::string base64Encode(std::span<const std::uint8_t> data);

/** 由客户端的 Sec-WebSocket-Key 算出 Sec-WebSocket-Accept。 */
[[nodiscard]] std::string acceptKey(std::string_view clientKey);

/** 握手请求的头：方法、目标与头字段（字段名转成小写）。 */
struct HttpRequest {
  std::string method;
  std::string target;
  std::map<std::string, std::string> headers;
};

/** 握手请求的解析结果。 */
struct HandshakeParse {
  enum class Status : std::uint8_t { Incomplete, Complete, Invalid };
  Status status{Status::Incomplete};
  HttpRequest request;
  /** 请求头占用的字节数（含结尾的空行）；Complete 时有效，之后的字节属于 WebSocket 帧。 */
  std::size_t consumed{0};
};

/** 请求头的最大字节数。 */
inline constexpr std::size_t MAX_HANDSHAKE_BYTES = 16 * 1024;

/** 从缓冲区解析握手请求；还没收到完整的头时返回 Incomplete，头超过上限或格式不对时返回 Invalid。 */
[[nodiscard]] HandshakeParse parseHandshake(std::string_view buffer);

/** 握手的检查规则。 */
struct HandshakePolicy {
  /** 服务监听的端口，Host 头必须是 127.0.0.1、localhost 或 [::1] 加这个端口。 */
  std::uint16_t port{0};
  /** 允许的请求目标。 */
  std::string target{"/"};
};

/** 握手检查的结果：通过时 accept 是要回的 Sec-WebSocket-Accept，否则 status 与 reason 用于回错误响应。 */
struct HandshakeCheck {
  bool ok{false};
  int status{400};
  std::string reason;
  std::string accept;
  /** 客户端请求了子协议 ember-debug.v1 时为 true。 */
  bool subprotocol{false};
};

/** 子协议名。 */
inline constexpr std::string_view SUBPROTOCOL = "ember-debug.v1";

/**
 * 检查握手请求：GET、Upgrade、Version 13、Host 与 Origin 都要合格。
 * Origin 没有（命令行与脚本等非浏览器客户端）或指向本机（127.0.0.1、localhost、[::1]）才允许，其余一律拒绝，防止网页跨站连接本机的调试服务。
 */
[[nodiscard]] HandshakeCheck checkHandshake(const HttpRequest& request, const HandshakePolicy& policy);

/** 握手成功的响应（含结尾空行）。 */
[[nodiscard]] std::string buildHandshakeResponse(const HandshakeCheck& check);

/** 握手失败的响应：状态码、原因，连接随后关闭。 */
[[nodiscard]] std::string buildHttpError(int status, std::string_view reason);

/** 客户端：握手请求（含结尾空行），请求 ember-debug.v1 子协议；key 是 16 个随机字节的 Base64。 */
[[nodiscard]] std::string buildClientHandshake(std::string_view host, std::uint16_t port, std::string_view key);

/** 服务端握手响应的解析结果。 */
struct HandshakeResponseParse {
  enum class Status : std::uint8_t { Incomplete, Complete, Invalid };
  Status status{Status::Incomplete};
  /** HTTP 状态码；Complete 时有效。 */
  int statusCode{0};
  /** 头字段（字段名转成小写）。 */
  std::map<std::string, std::string> headers;
  /** 响应头占用的字节数（含结尾的空行）；Complete 时有效，之后的字节属于 WebSocket 帧。 */
  std::size_t consumed{0};
};

/** 从缓冲区解析服务端的握手响应；还没收到完整的头时返回 Incomplete，头超过上限或格式不对时返回 Invalid。 */
[[nodiscard]] HandshakeResponseParse parseHandshakeResponse(std::string_view buffer);

/** 服务端是否接受了握手：状态码 101，且 Sec-WebSocket-Accept 与 key 对得上。 */
[[nodiscard]] bool clientHandshakeAccepted(const HandshakeResponseParse& response, std::string_view key);

/** 帧的操作码。 */
enum class Opcode : std::uint8_t { Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

/** 一条完整的消息（数据消息已合并分片）或一个控制帧。 */
struct Message {
  Opcode opcode{Opcode::Text};
  std::vector<std::uint8_t> payload;
};

/** 关闭码。 */
inline constexpr std::uint16_t CLOSE_NORMAL = 1000;
inline constexpr std::uint16_t CLOSE_PROTOCOL_ERROR = 1002;
inline constexpr std::uint16_t CLOSE_INVALID_PAYLOAD = 1007;
inline constexpr std::uint16_t CLOSE_POLICY = 1008;
inline constexpr std::uint16_t CLOSE_TOO_BIG = 1009;
inline constexpr std::uint16_t CLOSE_INTERNAL = 1011;
/** 应用自定义：没有先完成 debug.hello 或令牌不对。 */
inline constexpr std::uint16_t CLOSE_UNAUTHORIZED = 4001;
/** 应用自定义：令牌已更换，连接被服务端关闭。 */
inline constexpr std::uint16_t CLOSE_TOKEN_CHANGED = 4002;

/** 帧解码器：按字节喂入，取出完整的消息；出错后不再产生消息。 */
class FrameDecoder {
 public:
  /** 解码的状态。 */
  enum class Status : std::uint8_t { Ok, ProtocolError, InvalidPayload, TooBig };

  /** 解码哪一方发来的帧：Server 解码客户端发来的帧（必须带掩码），Client 解码服务端发来的帧（不能带掩码）。 */
  enum class Role : std::uint8_t { Server, Client };

  /**
   * @param maxMessageBytes 单条消息（分片合并后）的最大字节数。
   * @param role 解码的是哪一方发来的帧；默认解码客户端发来的帧（服务端用）。
   */
  explicit FrameDecoder(std::size_t maxMessageBytes, Role role = Role::Server);

  /** 喂入收到的字节。 */
  void feed(std::span<const std::uint8_t> data);

  /** 取出下一条完整的消息；没有时返回空。出错后返回空，原因见 status()。 */
  [[nodiscard]] std::optional<Message> next();

  [[nodiscard]] Status status() const noexcept { return status_; }

 private:
  /** 解析缓冲区里的一个帧；数据不够返回 false。 */
  bool parseFrame();

  std::size_t maxMessageBytes_;
  Role role_;
  std::vector<std::uint8_t> buffer_;
  std::size_t offset_{0};
  std::deque<Message> ready_;
  /** 正在合并的分片消息。 */
  std::optional<Message> partial_;
  Status status_{Status::Ok};
};

/**
 * 编码一个帧（FIN=1，不分片）。
 * @param opcode 操作码。
 * @param payload 负载。
 * @param maskKey 掩码；服务端发出的帧不带，测试里模拟客户端时给出。
 */
[[nodiscard]] std::vector<std::uint8_t> encodeFrame(Opcode opcode, std::span<const std::uint8_t> payload, std::optional<std::array<std::uint8_t, 4>> maskKey = std::nullopt);

/** 编码文本帧。 */
[[nodiscard]] std::vector<std::uint8_t> encodeText(std::string_view text);

/** 编码关闭帧：关闭码与原因。 */
[[nodiscard]] std::vector<std::uint8_t> encodeClose(std::uint16_t code, std::string_view reason = {});

/** 字节序列是不是合法的 UTF-8。 */
[[nodiscard]] bool isValidUtf8(std::span<const std::uint8_t> data) noexcept;

}  // namespace ember::platform::websocket
