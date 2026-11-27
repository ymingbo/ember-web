/*
 * ------------------------------------------------------------------------
 * 名称：debug-server.hpp
 * 说明：调试服务器：只监听 127.0.0.1 的 WebSocket 服务，每条连接一个读线程与一个写线程，握手、令牌与 JSON-RPC 请求交给会话逻辑（DebugSession）；事件按订阅推送，事件队列有上限，慢客户端只丢自己的事件，不拖慢核心。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：启动成功后把地址与令牌写进端点文件（用户数据目录的 run/debug-endpoint.json），方便命令行与 Copilot 的工具自动连接，停止时删除；
 *       令牌更换时已建立的连接被关闭（关闭码 4002）；停止时取消正在处理的请求并等待全部线程退出。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "contracts/debug/generated/debug.hpp"
#include "infra/debug/debug-session.hpp"
#include "platform/network/tcp-socket.hpp"

namespace ember::infra::debug {

/** 调试服务器的参数。 */
struct DebugServerOptions {
  /** 监听的端口；0 表示由系统选一个空闲端口（测试用）。 */
  std::uint16_t port{0};
  /** 令牌，必须非空。 */
  std::string token;
  /** 端点文件；为空时不写。 */
  std::filesystem::path endpointFile;
  std::string coreVersion;
  std::string appVersion;
  /** 单条请求的最大字节数。 */
  std::size_t maxRequestBytes{4 * 1024 * 1024};
  /** 每条连接排队等待发送的事件条数上限，超过后丢弃新事件。 */
  std::size_t eventQueueLimit{1024};
  /** 握手（HTTP 升级）与 debug.hello 的时限。 */
  int handshakeTimeoutMs{5000};
  /** 空闲多久发一次 ping，以及多久没有收到任何数据就关闭。 */
  int pingIntervalMs{30000};
  int idleTimeoutMs{90000};
};

/** 调试服务器。 */
class DebugServer {
 public:
  /**
   * @param options 参数。
   * @param dispatch 分发核心请求的函数。
   * @param discovery rpc.discover 返回的文档，可以为空。
   * @param errors 错误日志，可以为空。
   */
  DebugServer(DebugServerOptions options, Dispatcher dispatch, std::shared_ptr<const nlohmann::json> discovery, std::shared_ptr<platform::ErrorLog> errors);
  ~DebugServer();

  DebugServer(const DebugServer&) = delete;
  DebugServer& operator=(const DebugServer&) = delete;

  /** 开始监听；失败时返回 false 并写入原因（如端口被占用）。 */
  [[nodiscard]] bool start(std::string& error);

  /** 停止：取消正在处理的请求，关闭全部连接，等待线程退出，删除端点文件。 */
  void stop();

  [[nodiscard]] bool listening() const noexcept { return listening_.load(); }
  /** 实际监听的端口，没有监听时为 0。 */
  [[nodiscard]] std::uint16_t port() const noexcept { return port_.load(); }

  /** 更换令牌：已建立的连接全部关闭（关闭码 4002），之后的握手按新令牌检查，端点文件同步更新。 */
  void setToken(std::string token);

  /** 把核心的事件推给订阅了的连接；可在任意线程调用，不阻塞。 */
  void publishEvent(const std::string& name, const nlohmann::json& data);

  /** 状态：enabled 在服务器存在期间恒为 true，由模块根据首选项决定是否创建。 */
  [[nodiscard]] contracts::debug::DebugStatusResult status() const;

  /** 设置状态观察者：连接建立（完成握手）与断开时调用，在没有持锁的线程里。 */
  void setStatusObserver(std::function<void()> observer);

 private:
  struct Pending {
    Outgoing outgoing;
    bool isEvent{false};
  };

  /** 一条连接：套接字、会话、发送队列与两个线程。 */
  struct Connection {
    Connection(std::int64_t id, platform::TcpSocket socket, std::shared_ptr<const SessionContext> context) : session(id, std::move(context)), socket(std::move(socket)) {}

    DebugSession session;
    platform::TcpSocket socket;
    std::string connectedAt;
    std::thread reader;
    std::atomic<bool> finished{false};

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Pending> queue;
    std::size_t pendingEvents{0};
    std::size_t dropped{0};
    bool closing{false};
  };

  void acceptLoop();
  void serve(const std::shared_ptr<Connection>& connection);
  /** 握手；成功返回 true，leftover 是握手之后已收到的字节。 */
  bool handshake(Connection& connection, std::vector<std::uint8_t>& leftover);
  void writerLoop(Connection& connection);
  void enqueue(Connection& connection, Outgoing outgoing, bool isEvent);
  void requestClose(Connection& connection, std::uint16_t code, const std::string& reason);
  void reap();
  void writeEndpointFile() const;
  void notifyObserver() const;
  [[nodiscard]] std::string currentToken() const;

  DebugServerOptions options_;
  std::shared_ptr<SessionContext> context_;
  std::unique_ptr<platform::TcpListener> listener_;
  std::thread acceptThread_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> listening_{false};
  std::atomic<std::uint16_t> port_{0};
  std::atomic<std::int64_t> nextId_{1};

  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<Connection>> connections_;
  std::string token_;
  std::function<void()> observer_;
};

}  // namespace ember::infra::debug
