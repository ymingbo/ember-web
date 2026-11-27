/*
 * ------------------------------------------------------------------------
 * 名称：debug-server.cpp
 * 说明：调试服务器的实现，见 debug-server.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/debug/debug-server.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <span>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "contracts/debug/debug-error.hpp"
#include "platform/network/websocket.hpp"

namespace ember::infra::debug {

namespace ws = platform::websocket;
namespace api = contracts::debug;
using Clock = std::chrono::steady_clock;

namespace {

/** 同时保持的连接数上限。 */
constexpr std::size_t MAX_CONNECTIONS = 32;
/** 读线程每次最多等多久再检查停止与超时。 */
constexpr int READ_SLICE_MS = 200;
/** 应用自定义关闭码之外的：不接受的数据类型。 */
constexpr std::uint16_t CLOSE_UNSUPPORTED_DATA = 1003;
constexpr std::uint16_t CLOSE_GOING_AWAY = 1001;

std::string isoNow() {
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm parts{};
#ifdef _WIN32
  gmtime_s(&parts, &now);
#else
  gmtime_r(&now, &parts);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &parts);
  return text;
}

long processId() {
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<long>(getpid());
#endif
}

bool elapsed(Clock::time_point since, int ms) { return Clock::now() - since > std::chrono::milliseconds(ms); }

}  // namespace

DebugServer::DebugServer(DebugServerOptions options, Dispatcher dispatch, std::shared_ptr<const nlohmann::json> discovery, std::shared_ptr<platform::ErrorLog> errors)
    : options_(std::move(options)), context_(std::make_shared<SessionContext>()), token_(options_.token) {
  context_->dispatch = std::move(dispatch);
  context_->discovery = std::move(discovery);
  context_->errors = std::move(errors);
  context_->coreVersion = options_.coreVersion;
  context_->appVersion = options_.appVersion;
  context_->token = [this] { return currentToken(); };
  context_->status = [this] { return status(); };
}

DebugServer::~DebugServer() { stop(); }

std::string DebugServer::currentToken() const {
  std::lock_guard lock(mutex_);
  return token_;
}

bool DebugServer::start(std::string& error) {
  if (listener_ != nullptr) return true;
  if (options_.token.empty()) {
    error = "调试服务的令牌不能为空";
    return false;
  }
  listener_ = platform::TcpListener::listenLoopback(options_.port, error);
  if (listener_ == nullptr) return false;
  stopping_ = false;
  port_ = listener_->port();
  listening_ = true;
  writeEndpointFile();
  acceptThread_ = std::thread([this] { acceptLoop(); });
  return true;
}

void DebugServer::stop() {
  if (listener_ == nullptr) return;
  stopping_ = true;
  listener_->close();
  if (acceptThread_.joinable()) acceptThread_.join();

  std::vector<std::shared_ptr<Connection>> connections;
  {
    std::lock_guard lock(mutex_);
    connections = connections_;
  }
  for (const auto& connection : connections) connection->session.cancel();
  // 读线程在进行中的请求被取消、回完响应后会发出关闭帧（1001）并退出；写不出去（客户端不读）的连接再强行关闭套接字。
  for (int i = 0; i < 20 && !std::ranges::all_of(connections, [](const auto& c) { return c->finished.load(); }); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  for (const auto& connection : connections) {
    if (!connection->finished) connection->socket.shutdown();
  }
  for (const auto& connection : connections) {
    if (connection->reader.joinable()) connection->reader.join();
  }
  {
    std::lock_guard lock(mutex_);
    connections_.clear();
  }

  listening_ = false;
  port_ = 0;
  listener_.reset();
  if (!options_.endpointFile.empty()) {
    std::error_code ignored;
    std::filesystem::remove(options_.endpointFile, ignored);
  }
}

void DebugServer::setToken(std::string token) {
  std::vector<std::shared_ptr<Connection>> connections;
  {
    std::lock_guard lock(mutex_);
    if (token_ == token) return;
    token_ = std::move(token);
    connections = connections_;
  }
  for (const auto& connection : connections) {
    connection->session.cancel();
    requestClose(*connection, ws::CLOSE_TOKEN_CHANGED, "令牌已更换");
  }
  if (listening_) writeEndpointFile();
}

void DebugServer::setStatusObserver(std::function<void()> observer) {
  std::lock_guard lock(mutex_);
  observer_ = std::move(observer);
}

void DebugServer::notifyObserver() const {
  std::function<void()> observer;
  {
    std::lock_guard lock(mutex_);
    observer = observer_;
  }
  if (observer) observer();
}

api::DebugStatusResult DebugServer::status() const {
  api::DebugStatusResult result;
  result.enabled = true;
  result.listening = listening_;
  result.port = port_;
  std::lock_guard lock(mutex_);
  for (const auto& connection : connections_) {
    if (!connection->finished && connection->session.authenticated()) result.connections.push_back({connection->session.id(), connection->session.client(), connection->connectedAt});
  }
  return result;
}

void DebugServer::publishEvent(const std::string& name, const nlohmann::json& data) {
  std::vector<std::shared_ptr<Connection>> connections;
  {
    std::lock_guard lock(mutex_);
    connections = connections_;
  }
  for (const auto& connection : connections) {
    if (auto message = connection->session.eventMessage(name, data)) enqueue(*connection, std::move(*message), true);
  }
}

void DebugServer::writeEndpointFile() const {
  if (options_.endpointFile.empty()) return;
  const nlohmann::json content{{"url", "ws://127.0.0.1:" + std::to_string(port_.load()) + "/"},
                               {"port", port_.load()},
                               {"token", currentToken()},
                               {"pid", processId()},
                               {"coreVersion", options_.coreVersion},
                               {"appVersion", options_.appVersion},
                               {"startedAt", isoNow()}};
  try {
    std::filesystem::create_directories(options_.endpointFile.parent_path());
    auto temporary = options_.endpointFile;
    temporary += ".tmp";
    {
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      file << content.dump(2);
    }
    std::filesystem::rename(temporary, options_.endpointFile);
  } catch (const std::exception& error) {
    if (context_->errors) context_->errors->write({"debug", "debug.endpointFileFailed", error.what(), {{"path", options_.endpointFile.string()}}});
  }
}

void DebugServer::acceptLoop() {
  while (!stopping_) {
    auto accepted = listener_->accept(READ_SLICE_MS);
    reap();
    if (!accepted) continue;
    std::shared_ptr<Connection> connection;
    {
      std::lock_guard lock(mutex_);
      if (connections_.size() >= MAX_CONNECTIONS) continue;
      connection = std::make_shared<Connection>(nextId_++, std::move(*accepted), context_);
      connection->connectedAt = isoNow();
      connections_.push_back(connection);
    }
    connection->reader = std::thread([this, connection] { serve(connection); });
  }
}

void DebugServer::reap() {
  std::vector<std::shared_ptr<Connection>> finished;
  {
    std::lock_guard lock(mutex_);
    const auto split = std::stable_partition(connections_.begin(), connections_.end(), [](const auto& connection) { return !connection->finished.load(); });
    finished.assign(split, connections_.end());
    connections_.erase(split, connections_.end());
  }
  for (const auto& connection : finished) {
    if (connection->reader.joinable()) connection->reader.join();
  }
}

bool DebugServer::handshake(Connection& connection, std::vector<std::uint8_t>& leftover) {
  const auto started = Clock::now();
  std::string received;
  std::vector<std::uint8_t> buffer(4096);
  while (!stopping_) {
    const auto parsed = ws::parseHandshake(received);
    if (parsed.status == ws::HandshakeParse::Status::Invalid) {
      const auto response = ws::buildHttpError(400, "请求头不合格");
      static_cast<void>(connection.socket.writeAll(reinterpret_cast<const std::uint8_t*>(response.data()), response.size()));
      return false;
    }
    if (parsed.status == ws::HandshakeParse::Status::Complete) {
      const auto check = ws::checkHandshake(parsed.request, {port_.load(), "/"});
      const auto response = check.ok ? ws::buildHandshakeResponse(check) : ws::buildHttpError(check.status, check.reason);
      static_cast<void>(connection.socket.writeAll(reinterpret_cast<const std::uint8_t*>(response.data()), response.size()));
      if (!check.ok) return false;
      leftover.assign(received.begin() + static_cast<std::ptrdiff_t>(parsed.consumed), received.end());
      return true;
    }
    if (elapsed(started, options_.handshakeTimeoutMs)) return false;
    const auto read = connection.socket.read(buffer.data(), buffer.size(), READ_SLICE_MS);
    if (read.kind == platform::TcpSocket::ReadResult::Kind::Data) {
      received.append(reinterpret_cast<const char*>(buffer.data()), read.size);
    } else if (read.kind != platform::TcpSocket::ReadResult::Kind::Timeout) {
      return false;
    }
  }
  return false;
}

void DebugServer::serve(const std::shared_ptr<Connection>& connectionPointer) {
  Connection& connection = *connectionPointer;
  std::vector<std::uint8_t> leftover;
  if (!handshake(connection, leftover)) {
    connection.socket.shutdown();
    connection.finished = true;
    return;
  }

  std::thread writer([this, &connection] { writerLoop(connection); });
  ws::FrameDecoder decoder(options_.maxRequestBytes);
  decoder.feed(leftover);
  const auto connectedAt = Clock::now();
  auto lastReceive = connectedAt;
  auto lastPing = connectedAt;
  bool announced = false;
  bool running = true;
  std::vector<std::uint8_t> buffer(64 * 1024);

  while (running && !stopping_) {
    {
      std::lock_guard lock(connection.mutex);
      if (connection.closing) break;
    }
    while (auto message = decoder.next()) {
      switch (message->opcode) {
        case ws::Opcode::Text: {
          const std::string text(message->payload.begin(), message->payload.end());
          auto result = connection.session.handleText(text);
          for (auto& outgoing : result.outgoing) enqueue(connection, std::move(outgoing), false);
          if (!announced && connection.session.authenticated()) {
            announced = true;
            notifyObserver();
          }
          if (result.closeCode) {
            requestClose(connection, *result.closeCode, result.closeReason);
            running = false;
          }
          lastReceive = Clock::now();
          break;
        }
        case ws::Opcode::Binary:
          requestClose(connection, CLOSE_UNSUPPORTED_DATA, "不接受客户端发送的二进制消息");
          running = false;
          break;
        case ws::Opcode::Ping:
          enqueue(connection, {ws::Opcode::Pong, std::move(message->payload)}, false);
          break;
        case ws::Opcode::Close: {
          const auto& payload = message->payload;
          const std::uint16_t code = payload.size() >= 2 ? static_cast<std::uint16_t>((payload[0] << 8) | payload[1]) : ws::CLOSE_NORMAL;
          requestClose(connection, code == 1005 || code == 1006 || code == 1015 ? ws::CLOSE_NORMAL : code, "");
          running = false;
          break;
        }
        default:
          break;
      }
      if (!running) break;
    }
    if (!running) break;
    if (decoder.status() != ws::FrameDecoder::Status::Ok) {
      const auto status = decoder.status();
      requestClose(connection, status == ws::FrameDecoder::Status::TooBig ? ws::CLOSE_TOO_BIG : status == ws::FrameDecoder::Status::InvalidPayload ? ws::CLOSE_INVALID_PAYLOAD : ws::CLOSE_PROTOCOL_ERROR, "");
      break;
    }
    if (!connection.session.authenticated() && elapsed(connectedAt, options_.handshakeTimeoutMs)) {
      requestClose(connection, ws::CLOSE_UNAUTHORIZED, "握手后没有及时完成 debug.hello");
      break;
    }
    if (elapsed(lastReceive, options_.idleTimeoutMs)) {
      requestClose(connection, ws::CLOSE_POLICY, "长时间没有收到数据");
      break;
    }
    if (elapsed(lastPing, options_.pingIntervalMs)) {
      lastPing = Clock::now();
      enqueue(connection, {ws::Opcode::Ping, {}}, false);
    }

    const auto read = connection.socket.read(buffer.data(), buffer.size(), READ_SLICE_MS);
    if (read.kind == platform::TcpSocket::ReadResult::Kind::Data) {
      lastReceive = Clock::now();
      decoder.feed(std::span(buffer.data(), read.size));
    } else if (read.kind != platform::TcpSocket::ReadResult::Kind::Timeout) {
      break;
    }
  }

  connection.session.cancel();
  requestClose(connection, CLOSE_GOING_AWAY, "");
  writer.join();
  connection.socket.shutdown();
  if (announced) {
    // 先标记结束再通知，状态里才不再包含这条连接。
    connection.finished = true;
    notifyObserver();
  }
  connection.finished = true;
}

void DebugServer::enqueue(Connection& connection, Outgoing outgoing, bool isEvent) {
  std::lock_guard lock(connection.mutex);
  if (connection.closing) return;
  if (isEvent) {
    if (connection.pendingEvents >= options_.eventQueueLimit) {
      ++connection.dropped;
      connection.wake.notify_one();
      return;
    }
    ++connection.pendingEvents;
  }
  connection.queue.push_back({std::move(outgoing), isEvent});
  connection.wake.notify_one();
}

void DebugServer::requestClose(Connection& connection, std::uint16_t code, const std::string& reason) {
  std::lock_guard lock(connection.mutex);
  if (connection.closing) return;
  connection.closing = true;
  // 写线程把 Close 的负载（关闭码加原因）编码成帧发出。
  std::vector<std::uint8_t> payload{static_cast<std::uint8_t>(code >> 8), static_cast<std::uint8_t>(code)};
  payload.insert(payload.end(), reason.begin(), reason.end());
  connection.queue.push_back({{ws::Opcode::Close, std::move(payload)}, false});
  connection.wake.notify_one();
}

void DebugServer::writerLoop(Connection& connection) {
  for (;;) {
    Pending item;
    {
      std::unique_lock lock(connection.mutex);
      connection.wake.wait(lock, [&connection] { return !connection.queue.empty() || connection.dropped > 0; });
      if (connection.queue.empty()) {
        // 事件队列满过：通知客户端丢了多少条（响应不会被丢弃，所以只在队列空下来之后补发）。
        nlohmann::json message{{"jsonrpc", "2.0"}, {"method", api::DEBUG_EVENTS_DROPPED_EVENT}, {"params", {{"count", connection.dropped}}}};
        const auto text = message.dump();
        item.outgoing = {ws::Opcode::Text, std::vector<std::uint8_t>(text.begin(), text.end())};
        connection.dropped = 0;
      } else {
        item = std::move(connection.queue.front());
        connection.queue.pop_front();
        if (item.isEvent) --connection.pendingEvents;
      }
    }
    const auto frame = ws::encodeFrame(item.outgoing.opcode, item.outgoing.payload);
    const bool ok = connection.socket.writeAll(frame.data(), frame.size());
    if (!ok || item.outgoing.opcode == ws::Opcode::Close) {
      std::lock_guard lock(connection.mutex);
      connection.closing = true;
      connection.queue.clear();
      connection.socket.shutdown();
      return;
    }
  }
}

}  // namespace ember::infra::debug
