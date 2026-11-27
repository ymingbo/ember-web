/*
 * ------------------------------------------------------------------------
 * 名称：tcp-socket.cpp
 * 说明：本机 TCP 套接字封装的实现，见 tcp-socket.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "platform/network/tcp-socket.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ember::platform {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket INVALID_NATIVE = INVALID_SOCKET;

void ensureNetworkStarted() {
  static std::once_flag once;
  std::call_once(once, [] {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  });
}

void closeNative(NativeSocket socket) { closesocket(socket); }
int lastError() { return WSAGetLastError(); }
#else
using NativeSocket = int;
constexpr NativeSocket INVALID_NATIVE = -1;

void ensureNetworkStarted() {}
void closeNative(NativeSocket socket) { ::close(socket); }
int lastError() { return errno; }
#endif

NativeSocket native(std::uintptr_t handle) { return static_cast<NativeSocket>(handle); }

/** 等待套接字可读；返回 1 可读，0 超时，-1 出错。 */
int waitReadable(NativeSocket socket, int timeoutMs) {
#ifdef _WIN32
  WSAPOLLFD entry{};
  entry.fd = socket;
  entry.events = POLLRDNORM;
  const int result = WSAPoll(&entry, 1, timeoutMs);
#else
  pollfd entry{};
  entry.fd = socket;
  entry.events = POLLIN;
  const int result = ::poll(&entry, 1, timeoutMs);
#endif
  return result;
}

std::string errorText(const std::string& what) { return what + "（错误码 " + std::to_string(lastError()) + "）"; }

}  // namespace

TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : handle_(other.handle_), valid_(std::exchange(other.valid_, false)) {}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    valid_ = std::exchange(other.valid_, false);
  }
  return *this;
}

void TcpSocket::close() noexcept {
  if (!valid_) return;
  closeNative(native(handle_));
  valid_ = false;
}

TcpSocket::ReadResult TcpSocket::read(std::uint8_t* buffer, std::size_t size, int timeoutMs) {
  if (!valid_) return {ReadResult::Kind::Error, 0};
  const int ready = waitReadable(native(handle_), timeoutMs);
  if (ready == 0) return {ReadResult::Kind::Timeout, 0};
  if (ready < 0) return {ReadResult::Kind::Error, 0};
#ifdef _WIN32
  const int received = ::recv(native(handle_), reinterpret_cast<char*>(buffer), static_cast<int>(size), 0);
#else
  const auto received = ::recv(native(handle_), buffer, size, 0);
#endif
  if (received == 0) return {ReadResult::Kind::Closed, 0};
  if (received < 0) return {ReadResult::Kind::Error, 0};
  return {ReadResult::Kind::Data, static_cast<std::size_t>(received)};
}

bool TcpSocket::writeAll(const std::uint8_t* data, std::size_t size) {
  if (!valid_) return false;
  std::size_t sent = 0;
  while (sent < size) {
#ifdef _WIN32
    const int count = ::send(native(handle_), reinterpret_cast<const char*>(data + sent), static_cast<int>(std::min<std::size_t>(size - sent, 1 << 20)), 0);
#elif defined(MSG_NOSIGNAL)
    const auto count = ::send(native(handle_), data + sent, size - sent, MSG_NOSIGNAL);
#else
    const auto count = ::send(native(handle_), data + sent, size - sent, 0);
#endif
    if (count <= 0) return false;
    sent += static_cast<std::size_t>(count);
  }
  return true;
}

void TcpSocket::shutdown() noexcept {
  if (!valid_) return;
#ifdef _WIN32
  ::shutdown(native(handle_), SD_BOTH);
#else
  ::shutdown(native(handle_), SHUT_RDWR);
#endif
}

std::uint16_t TcpSocket::peerPort() const noexcept {
  if (!valid_) return 0;
  sockaddr_in address{};
#ifdef _WIN32
  int length = sizeof(address);
#else
  socklen_t length = sizeof(address);
#endif
  if (::getpeername(native(handle_), reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;
  return ntohs(address.sin_port);
}

std::optional<TcpSocket> TcpSocket::connectLoopback(std::uint16_t port, std::string& error) {
  ensureNetworkStarted();
  const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == INVALID_NATIVE) {
    error = errorText("无法创建套接字");
    return std::nullopt;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (::connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    error = errorText("无法连接 127.0.0.1:" + std::to_string(port));
    closeNative(socket);
    return std::nullopt;
  }
  return TcpSocket(static_cast<std::uintptr_t>(socket));
}

std::unique_ptr<TcpListener> TcpListener::listenLoopback(std::uint16_t port, std::string& error) {
  ensureNetworkStarted();
  const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == INVALID_NATIVE) {
    error = errorText("无法创建套接字");
    return nullptr;
  }
  // 端口占用要如实报错：Windows 上 SO_REUSEADDR 会允许别的程序抢占端口，改用独占；其他系统用 SO_REUSEADDR 只为重启后立即可用。
#ifdef _WIN32
  const BOOL exclusive = TRUE;
  setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
  const int reuse = 1;
  setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (::bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    error = errorText("无法监听端口 " + std::to_string(port) + "，可能已被占用");
    closeNative(socket);
    return nullptr;
  }
  if (::listen(socket, 16) != 0) {
    error = errorText("无法开始监听");
    closeNative(socket);
    return nullptr;
  }
  sockaddr_in bound{};
#ifdef _WIN32
  int boundLength = sizeof(bound);
#else
  socklen_t boundLength = sizeof(bound);
#endif
  ::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &boundLength);
  return std::unique_ptr<TcpListener>(new TcpListener(static_cast<std::uintptr_t>(socket), ntohs(bound.sin_port)));
}

TcpListener::~TcpListener() {
  open_ = false;
  closeNative(native(handle_));
}

std::optional<TcpSocket> TcpListener::accept(int timeoutMs) {
  if (!open_) return std::nullopt;
  if (waitReadable(native(handle_), timeoutMs) <= 0 || !open_) return std::nullopt;
  const NativeSocket connection = ::accept(native(handle_), nullptr, nullptr);
  if (connection == INVALID_NATIVE) return std::nullopt;
  const int noDelay = 1;
#ifdef _WIN32
  setsockopt(connection, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
#else
  setsockopt(connection, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
#ifdef SO_NOSIGPIPE
  setsockopt(connection, SOL_SOCKET, SO_NOSIGPIPE, &noDelay, sizeof(noDelay));
#endif
#endif
  return TcpSocket(static_cast<std::uintptr_t>(connection));
}

void TcpListener::close() noexcept { open_ = false; }

}  // namespace ember::platform
