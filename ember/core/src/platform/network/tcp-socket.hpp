/*
 * ------------------------------------------------------------------------
 * 名称：tcp-socket.hpp
 * 说明：本机 TCP 套接字的薄封装：只监听 127.0.0.1 的监听器与已建立的连接，阻塞读写，读与接受都带超时，便于线程检查停止标志；Windows 用 Winsock，macOS 与 Linux 用 BSD 套接字。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：对象只能移动；shutdown 可在另一个线程调用，使阻塞中的读与写立即返回；析构时关闭套接字。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ember::platform {

/** 一个已建立的 TCP 连接。 */
class TcpSocket {
 public:
  /** 读的结果。 */
  struct ReadResult {
    enum class Kind : std::uint8_t { Data, Timeout, Closed, Error };
    Kind kind{Kind::Error};
    /** Kind 为 Data 时读到的字节数。 */
    std::size_t size{0};
  };

  TcpSocket() noexcept = default;
  explicit TcpSocket(std::uintptr_t handle) noexcept : handle_(handle), valid_(true) {}
  ~TcpSocket();

  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;
  TcpSocket(TcpSocket&& other) noexcept;
  TcpSocket& operator=(TcpSocket&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept { return valid_; }

  /** 连接到本机 127.0.0.1 的端口（测试与本机工具用）；失败返回空。 */
  [[nodiscard]] static std::optional<TcpSocket> connectLoopback(std::uint16_t port, std::string& error);

  /** 最多等 timeoutMs 毫秒读数据；对方关闭返回 Closed。 */
  [[nodiscard]] ReadResult read(std::uint8_t* buffer, std::size_t size, int timeoutMs);

  /** 写完全部数据；失败（对方已断开、被 shutdown）返回 false。 */
  [[nodiscard]] bool writeAll(const std::uint8_t* data, std::size_t size);

  /** 关闭收发，使另一个线程里阻塞的读写立即返回；套接字本身到析构时才释放。 */
  void shutdown() noexcept;

  /** 对方的端口（用于日志）；取不到时为 0。 */
  [[nodiscard]] std::uint16_t peerPort() const noexcept;

 private:
  void close() noexcept;

  std::uintptr_t handle_{0};
  bool valid_{false};
};

/** 只监听 127.0.0.1 的 TCP 监听器。 */
class TcpListener {
 public:
  /**
   * 开始监听。
   * @param port 端口，0 表示由系统选一个空闲端口。
   * @param error 失败时写入原因（如端口已被占用）。
   * @returns 监听器；失败时为空。
   */
  [[nodiscard]] static std::unique_ptr<TcpListener> listenLoopback(std::uint16_t port, std::string& error);

  ~TcpListener();
  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;

  /** 实际监听的端口。 */
  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

  /** 最多等 timeoutMs 毫秒接受一个连接；没有连接或监听器已关闭时返回空。 */
  [[nodiscard]] std::optional<TcpSocket> accept(int timeoutMs);

  /** 停止接受连接：之后的 accept 立即返回空；端口在析构时才释放，调用方要先让 accept 的线程退出。 */
  void close() noexcept;

 private:
  TcpListener(std::uintptr_t handle, std::uint16_t port) noexcept : handle_(handle), port_(port) {}

  std::uintptr_t handle_;
  std::uint16_t port_;
  std::atomic<bool> open_{true};
};

}  // namespace ember::platform
