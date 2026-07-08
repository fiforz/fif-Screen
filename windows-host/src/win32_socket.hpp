#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <optional>
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
  void close();

 private:
  SOCKET socket_ = INVALID_SOCKET;
};

class TcpServer {
 public:
  TcpServer(std::uint16_t port, std::string name);
  ~TcpServer();

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  void listen();
  [[nodiscard]] Socket accept_one() const;

 private:
  std::uint16_t port_;
  std::string name_;
  SOCKET listen_socket_ = INVALID_SOCKET;
};

}  // namespace fif::host

