/*
 * ------------------------------------------------------------------------
 * 名称：websocket.cpp
 * 说明：WebSocket 服务端纯逻辑的实现，见 websocket.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "platform/network/websocket.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace ember::platform::websocket {

namespace {

constexpr std::string_view WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::uint32_t rotateLeft(std::uint32_t value, int bits) noexcept { return (value << bits) | (value >> (32 - bits)); }

std::string lower(std::string_view text) {
  std::string result(text);
  std::ranges::transform(result, result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return result;
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  return text;
}

/** 逗号分隔的头字段值里是不是含有某个记号（不区分大小写）。 */
bool hasToken(std::string_view value, std::string_view token) {
  std::size_t start = 0;
  while (start <= value.size()) {
    auto end = value.find(',', start);
    if (end == std::string_view::npos) end = value.size();
    if (lower(trim(value.substr(start, end - start))) == lower(token)) return true;
    start = end + 1;
  }
  return false;
}

const std::string* header(const HttpRequest& request, const char* name) {
  const auto it = request.headers.find(name);
  return it == request.headers.end() ? nullptr : &it->second;
}

bool isLoopbackHost(std::string_view host) {
  const auto text = lower(host);
  return text == "127.0.0.1" || text == "localhost" || text == "[::1]";
}

/** Host 头：本机地址加服务的端口。 */
bool hostAllowed(std::string_view host, std::uint16_t port) {
  const auto colon = host.rfind(':');
  if (colon == std::string_view::npos || colon + 1 >= host.size()) return false;
  const auto portText = host.substr(colon + 1);
  if (portText != std::to_string(port)) return false;
  return isLoopbackHost(host.substr(0, colon));
}

/** Origin 头：http 或 https 加本机地址（任意端口）。 */
bool originAllowed(std::string_view origin) {
  std::string_view rest;
  if (origin.starts_with("http://")) {
    rest = origin.substr(7);
  } else if (origin.starts_with("https://")) {
    rest = origin.substr(8);
  } else {
    return false;
  }
  if (rest.empty() || rest.find('/') != std::string_view::npos) return false;
  // [::1] 里有冒号，端口只取最后一个冒号在方括号之后的部分。
  const auto bracket = rest.rfind(']');
  const auto colon = rest.rfind(':');
  const bool hasPort = colon != std::string_view::npos && (bracket == std::string_view::npos || colon > bracket);
  return isLoopbackHost(hasPort ? rest.substr(0, colon) : rest);
}

/** Sec-WebSocket-Key：16 个字节的 Base64，共 24 个字符，以 == 结尾。 */
bool keyValid(std::string_view key) {
  if (key.size() != 24 || !key.ends_with("==")) return false;
  return std::ranges::all_of(key.substr(0, 22), [](unsigned char ch) { return std::isalnum(ch) || ch == '+' || ch == '/'; });
}

}  // namespace

std::array<std::uint8_t, 20> sha1(std::string_view data) {
  std::uint32_t h0 = 0x67452301;
  std::uint32_t h1 = 0xEFCDAB89;
  std::uint32_t h2 = 0x98BADCFE;
  std::uint32_t h3 = 0x10325476;
  std::uint32_t h4 = 0xC3D2E1F0;

  std::vector<std::uint8_t> message(data.begin(), data.end());
  const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8;
  message.push_back(0x80);
  while (message.size() % 64 != 56) message.push_back(0);
  for (int shift = 56; shift >= 0; shift -= 8) message.push_back(static_cast<std::uint8_t>(bitLength >> shift));

  for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
    std::array<std::uint32_t, 80> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      const auto* p = &message[chunk + i * 4];
      w[i] = (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
    }
    for (std::size_t i = 16; i < 80; ++i) w[i] = rotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    std::uint32_t a = h0;
    std::uint32_t b = h1;
    std::uint32_t c = h2;
    std::uint32_t d = h3;
    std::uint32_t e = h4;
    for (std::size_t i = 0; i < 80; ++i) {
      std::uint32_t f = 0;
      std::uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      const std::uint32_t temp = rotateLeft(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rotateLeft(b, 30);
      b = a;
      a = temp;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::array<std::uint8_t, 20> digest{};
  const std::uint32_t words[5] = {h0, h1, h2, h3, h4};
  for (std::size_t i = 0; i < 5; ++i) {
    for (std::size_t j = 0; j < 4; ++j) digest[i * 4 + j] = static_cast<std::uint8_t>(words[i] >> (24 - 8 * j));
  }
  return digest;
}

std::string base64Encode(std::span<const std::uint8_t> data) {
  static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve((data.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    const std::uint32_t value = (static_cast<std::uint32_t>(data[i]) << 16) | (static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
    result.push_back(ALPHABET[(value >> 18) & 63]);
    result.push_back(ALPHABET[(value >> 12) & 63]);
    result.push_back(ALPHABET[(value >> 6) & 63]);
    result.push_back(ALPHABET[value & 63]);
  }
  if (i + 1 == data.size()) {
    const std::uint32_t value = static_cast<std::uint32_t>(data[i]) << 16;
    result.push_back(ALPHABET[(value >> 18) & 63]);
    result.push_back(ALPHABET[(value >> 12) & 63]);
    result += "==";
  } else if (i + 2 == data.size()) {
    const std::uint32_t value = (static_cast<std::uint32_t>(data[i]) << 16) | (static_cast<std::uint32_t>(data[i + 1]) << 8);
    result.push_back(ALPHABET[(value >> 18) & 63]);
    result.push_back(ALPHABET[(value >> 12) & 63]);
    result.push_back(ALPHABET[(value >> 6) & 63]);
    result.push_back('=');
  }
  return result;
}

std::string acceptKey(std::string_view clientKey) {
  const auto digest = sha1(std::string(clientKey) + std::string(WEBSOCKET_GUID));
  return base64Encode(digest);
}

/** 解析头字段（从 position 开始的每一行），字段名转小写，重名的字段用逗号连起来；某一行没有冒号时返回 false。 */
static bool parseHeaderFields(std::string_view head, std::size_t position, std::map<std::string, std::string>& headers) {
  while (position < head.size()) {
    auto lineEnd = head.find("\r\n", position);
    if (lineEnd == std::string_view::npos) lineEnd = head.size();
    const auto line = head.substr(position, lineEnd - position);
    position = lineEnd + 2;
    const auto colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) return false;
    const auto name = lower(trim(line.substr(0, colon)));
    const auto value = std::string(trim(line.substr(colon + 1)));
    auto [it, inserted] = headers.try_emplace(name, value);
    if (!inserted) it->second += ", " + value;
  }
  return true;
}

HandshakeParse parseHandshake(std::string_view buffer) {
  HandshakeParse result;
  const auto end = buffer.find("\r\n\r\n");
  if (end == std::string_view::npos) {
    result.status = buffer.size() > MAX_HANDSHAKE_BYTES ? HandshakeParse::Status::Invalid : HandshakeParse::Status::Incomplete;
    return result;
  }
  if (end + 4 > MAX_HANDSHAKE_BYTES) {
    result.status = HandshakeParse::Status::Invalid;
    return result;
  }

  const auto head = buffer.substr(0, end);
  const auto firstLineEnd = head.find("\r\n");
  const auto requestLine = head.substr(0, firstLineEnd);
  const auto firstSpace = requestLine.find(' ');
  const auto secondSpace = firstSpace == std::string_view::npos ? std::string_view::npos : requestLine.find(' ', firstSpace + 1);
  if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos || requestLine.substr(secondSpace + 1) != "HTTP/1.1") {
    result.status = HandshakeParse::Status::Invalid;
    return result;
  }
  result.request.method = std::string(requestLine.substr(0, firstSpace));
  result.request.target = std::string(requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1));

  std::size_t position = firstLineEnd == std::string_view::npos ? head.size() : firstLineEnd + 2;
  if (!parseHeaderFields(head, position, result.request.headers)) {
    result.status = HandshakeParse::Status::Invalid;
    return result;
  }
  result.status = HandshakeParse::Status::Complete;
  result.consumed = end + 4;
  return result;
}

std::string buildClientHandshake(std::string_view host, std::uint16_t port, std::string_view key) {
  std::string request = "GET / HTTP/1.1\r\nHost: ";
  request += host;
  request += ':';
  request += std::to_string(port);
  request += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ";
  request += key;
  request += "\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: ";
  request += SUBPROTOCOL;
  request += "\r\n\r\n";
  return request;
}

HandshakeResponseParse parseHandshakeResponse(std::string_view buffer) {
  HandshakeResponseParse result;
  const auto end = buffer.find("\r\n\r\n");
  if (end == std::string_view::npos) {
    result.status = buffer.size() > MAX_HANDSHAKE_BYTES ? HandshakeResponseParse::Status::Invalid : HandshakeResponseParse::Status::Incomplete;
    return result;
  }
  if (end + 4 > MAX_HANDSHAKE_BYTES) {
    result.status = HandshakeResponseParse::Status::Invalid;
    return result;
  }
  const auto head = buffer.substr(0, end);
  const auto firstLineEnd = head.find("\r\n");
  const auto statusLine = head.substr(0, firstLineEnd);
  // 状态行：HTTP/1.1 状态码 原因
  const auto firstSpace = statusLine.find(' ');
  if (statusLine.substr(0, firstSpace) != "HTTP/1.1" || firstSpace == std::string_view::npos || statusLine.size() < firstSpace + 4) {
    result.status = HandshakeResponseParse::Status::Invalid;
    return result;
  }
  int code = 0;
  for (std::size_t i = firstSpace + 1; i < firstSpace + 4; ++i) {
    if (statusLine[i] < '0' || statusLine[i] > '9') {
      result.status = HandshakeResponseParse::Status::Invalid;
      return result;
    }
    code = code * 10 + (statusLine[i] - '0');
  }
  result.statusCode = code;
  const std::size_t position = firstLineEnd == std::string_view::npos ? head.size() : firstLineEnd + 2;
  if (!parseHeaderFields(head, position, result.headers)) {
    result.status = HandshakeResponseParse::Status::Invalid;
    return result;
  }
  result.status = HandshakeResponseParse::Status::Complete;
  result.consumed = end + 4;
  return result;
}

bool clientHandshakeAccepted(const HandshakeResponseParse& response, std::string_view key) {
  if (response.status != HandshakeResponseParse::Status::Complete || response.statusCode != 101) return false;
  const auto accept = response.headers.find("sec-websocket-accept");
  return accept != response.headers.end() && accept->second == acceptKey(key);
}

HandshakeCheck checkHandshake(const HttpRequest& request, const HandshakePolicy& policy) {
  HandshakeCheck check;
  const auto reject = [&check](int status, std::string reason) {
    check.ok = false;
    check.status = status;
    check.reason = std::move(reason);
    return check;
  };
  if (request.method != "GET") return reject(405, "只接受 GET");
  if (request.target != policy.target) return reject(404, "没有这个路径");

  const auto* host = header(request, "host");
  if (host == nullptr || !hostAllowed(*host, policy.port)) return reject(403, "Host 不是本机地址");
  const auto* origin = header(request, "origin");
  if (origin != nullptr && !originAllowed(*origin)) return reject(403, "Origin 不是本机页面");

  const auto* upgrade = header(request, "upgrade");
  const auto* connection = header(request, "connection");
  if (upgrade == nullptr || !hasToken(*upgrade, "websocket") || connection == nullptr || !hasToken(*connection, "upgrade")) return reject(426, "需要升级为 WebSocket");
  const auto* version = header(request, "sec-websocket-version");
  if (version == nullptr || trim(*version) != "13") return reject(426, "只支持 WebSocket 版本 13");
  const auto* key = header(request, "sec-websocket-key");
  if (key == nullptr || !keyValid(*key)) return reject(400, "Sec-WebSocket-Key 不合格");

  check.ok = true;
  check.status = 101;
  check.accept = acceptKey(*key);
  const auto* protocols = header(request, "sec-websocket-protocol");
  check.subprotocol = protocols != nullptr && hasToken(*protocols, SUBPROTOCOL);
  return check;
}

std::string buildHandshakeResponse(const HandshakeCheck& check) {
  std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + check.accept + "\r\n";
  if (check.subprotocol) response += "Sec-WebSocket-Protocol: " + std::string(SUBPROTOCOL) + "\r\n";
  response += "\r\n";
  return response;
}

std::string buildHttpError(int status, std::string_view reason) {
  const char* text = "Bad Request";
  switch (status) {
    case 403:
      text = "Forbidden";
      break;
    case 404:
      text = "Not Found";
      break;
    case 405:
      text = "Method Not Allowed";
      break;
    case 426:
      text = "Upgrade Required";
      break;
    default:
      break;
  }
  const std::string body(reason);
  std::string response = "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n";
  if (status == 426) response += "Sec-WebSocket-Version: 13\r\n";
  response += "\r\n" + body;
  return response;
}

bool isValidUtf8(std::span<const std::uint8_t> data) noexcept {
  std::size_t i = 0;
  while (i < data.size()) {
    const std::uint8_t lead = data[i];
    std::size_t extra = 0;
    std::uint32_t code = 0;
    if (lead < 0x80) {
      ++i;
      continue;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
      extra = 1;
      code = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      extra = 2;
      code = lead & 0x0F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      extra = 3;
      code = lead & 0x07;
    } else {
      return false;
    }
    if (i + extra >= data.size()) return false;
    for (std::size_t j = 1; j <= extra; ++j) {
      if ((data[i + j] & 0xC0) != 0x80) return false;
      code = (code << 6) | (data[i + j] & 0x3F);
    }
    if ((extra == 2 && code < 0x800) || (extra == 3 && code < 0x10000) || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    i += extra + 1;
  }
  return true;
}

FrameDecoder::FrameDecoder(std::size_t maxMessageBytes, Role role) : maxMessageBytes_(maxMessageBytes), role_(role) {}

void FrameDecoder::feed(std::span<const std::uint8_t> data) {
  if (status_ != Status::Ok) return;
  if (offset_ > 0 && offset_ >= buffer_.size() / 2) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset_));
    offset_ = 0;
  }
  buffer_.insert(buffer_.end(), data.begin(), data.end());
}

std::optional<Message> FrameDecoder::next() {
  while (ready_.empty() && status_ == Status::Ok) {
    if (!parseFrame()) break;
  }
  if (ready_.empty()) return std::nullopt;
  auto message = std::move(ready_.front());
  ready_.pop_front();
  return message;
}

bool FrameDecoder::parseFrame() {
  const auto available = buffer_.size() - offset_;
  if (available < 2) return false;
  const auto* head = buffer_.data() + offset_;
  const bool fin = (head[0] & 0x80) != 0;
  const auto opcode = static_cast<Opcode>(head[0] & 0x0F);
  const bool masked = (head[1] & 0x80) != 0;
  std::uint64_t length = head[1] & 0x7F;
  std::size_t headerBytes = 2;

  // 按 RFC 6455：客户端发的帧必须带掩码，服务端发的帧不能带。
  if ((head[0] & 0x70) != 0 || masked != (role_ == Role::Server)) {
    status_ = Status::ProtocolError;
    return false;
  }
  if (length == 126) {
    if (available < 4) return false;
    length = (static_cast<std::uint64_t>(head[2]) << 8) | head[3];
    headerBytes = 4;
  } else if (length == 127) {
    if (available < 10) return false;
    length = 0;
    for (std::size_t i = 0; i < 8; ++i) length = (length << 8) | head[2 + i];
    if ((length >> 63) != 0) {
      status_ = Status::ProtocolError;
      return false;
    }
    headerBytes = 10;
  }

  const bool control = (static_cast<std::uint8_t>(opcode) & 0x8) != 0;
  const bool known = opcode == Opcode::Continuation || opcode == Opcode::Text || opcode == Opcode::Binary || opcode == Opcode::Close || opcode == Opcode::Ping || opcode == Opcode::Pong;
  if (!known || (control && (!fin || length > 125))) {
    status_ = Status::ProtocolError;
    return false;
  }
  const std::uint64_t already = partial_ ? partial_->payload.size() : 0;
  if (!control && already + length > maxMessageBytes_) {
    status_ = Status::TooBig;
    return false;
  }

  const std::size_t maskBytes = masked ? 4 : 0;
  if (available < headerBytes + maskBytes + length) return false;
  std::vector<std::uint8_t> payload(static_cast<std::size_t>(length));
  const auto* body = head + headerBytes + maskBytes;
  if (masked) {
    const auto* mask = head + headerBytes;
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = body[i] ^ mask[i % 4];
  } else {
    std::copy(body, body + payload.size(), payload.begin());
  }
  offset_ += headerBytes + maskBytes + static_cast<std::size_t>(length);

  if (control) {
    if (opcode == Opcode::Close && !payload.empty() && (payload.size() == 1 || !isValidUtf8(std::span(payload).subspan(2)))) {
      status_ = payload.size() == 1 ? Status::ProtocolError : Status::InvalidPayload;
      return false;
    }
    ready_.push_back({opcode, std::move(payload)});
    return true;
  }

  if (opcode == Opcode::Continuation) {
    if (!partial_) {
      status_ = Status::ProtocolError;
      return false;
    }
    partial_->payload.insert(partial_->payload.end(), payload.begin(), payload.end());
  } else {
    if (partial_) {
      status_ = Status::ProtocolError;
      return false;
    }
    partial_ = Message{opcode, std::move(payload)};
  }
  if (fin) {
    if (partial_->opcode == Opcode::Text && !isValidUtf8(partial_->payload)) {
      status_ = Status::InvalidPayload;
      return false;
    }
    ready_.push_back(std::move(*partial_));
    partial_.reset();
  }
  return true;
}

std::vector<std::uint8_t> encodeFrame(Opcode opcode, std::span<const std::uint8_t> payload, std::optional<std::array<std::uint8_t, 4>> maskKey) {
  std::vector<std::uint8_t> frame;
  frame.reserve(payload.size() + 14);
  frame.push_back(static_cast<std::uint8_t>(0x80 | static_cast<std::uint8_t>(opcode)));
  const std::uint8_t maskBit = maskKey ? 0x80 : 0;
  if (payload.size() < 126) {
    frame.push_back(static_cast<std::uint8_t>(maskBit | payload.size()));
  } else if (payload.size() <= 0xFFFF) {
    frame.push_back(static_cast<std::uint8_t>(maskBit | 126));
    frame.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
    frame.push_back(static_cast<std::uint8_t>(payload.size()));
  } else {
    frame.push_back(static_cast<std::uint8_t>(maskBit | 127));
    for (int shift = 56; shift >= 0; shift -= 8) frame.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(payload.size()) >> shift));
  }
  if (maskKey) {
    frame.insert(frame.end(), maskKey->begin(), maskKey->end());
    for (std::size_t i = 0; i < payload.size(); ++i) frame.push_back(payload[i] ^ (*maskKey)[i % 4]);
  } else {
    frame.insert(frame.end(), payload.begin(), payload.end());
  }
  return frame;
}

std::vector<std::uint8_t> encodeText(std::string_view text) {
  return encodeFrame(Opcode::Text, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::vector<std::uint8_t> encodeClose(std::uint16_t code, std::string_view reason) {
  std::vector<std::uint8_t> payload{static_cast<std::uint8_t>(code >> 8), static_cast<std::uint8_t>(code)};
  payload.insert(payload.end(), reason.begin(), reason.end());
  return encodeFrame(Opcode::Close, payload);
}

}  // namespace ember::platform::websocket
