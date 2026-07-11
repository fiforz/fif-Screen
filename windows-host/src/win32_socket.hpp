#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fif::host {

class WinsockRuntime {
 public:
  WinsockRuntime();
  ~WinsockRuntime();

  WinsockRuntime(const WinsockRuntime&) = delete;
  WinsockRuntime& operator=(const WinsockRuntime&) = delete;
};

class Socket {
 public:
  Socket() = default;
  explicit Socket(SOCKET socket) : socket_(socket) {}
  ~Socket();

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  [[nodiscard]] bool valid() const { return socket_ != INVALID_SOCKET; }
  [[nodiscard]] SOCKET native() const { return socket_; }
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> recv_some(std::size_t max_bytes) const;
  [[nodiscard]] bool send_all(const std::vector<std::uint8_t>& bytes) const;
  void set_receive_timeout(std::uint32_t timeout_ms) const;
  void close();

 private:
  SOCKET socket_ = INVALID_SOCKET;
};

enum class BindMode {
  Loopback,
  Any,
};

class TcpServer {
 public:
  TcpServer(std::uint16_t port, std::string name, BindMode bind_mode);
  ~TcpServer();

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  void listen();
  [[nodiscard]] Socket accept_one() const;

 private:
  std::uint16_t port_;
  std::string name_;
  BindMode bind_mode_;
  SOCKET listen_socket_ = INVALID_SOCKET;
};

struct UdpDatagram {
  std::vector<std::uint8_t> bytes;
  sockaddr_in peer{};
};

class UdpServer {
 public:
  explicit UdpServer(std::uint16_t port) : port_(port) {}
  ~UdpServer();

  UdpServer(const UdpServer&) = delete;
  UdpServer& operator=(const UdpServer&) = delete;

  void listen();
  [[nodiscard]] std::optional<UdpDatagram> receive(std::size_t max_bytes) const;
  [[nodiscard]] bool send_to(std::span<const std::uint8_t> bytes,
                             const sockaddr_in& peer) const;

 private:
  std::uint16_t port_;
  SOCKET socket_ = INVALID_SOCKET;
};

}  // namespace fif::host
