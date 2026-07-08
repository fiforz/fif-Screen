#include "win32_socket.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace fif::host {

namespace {

std::string last_wsa_error(const char* prefix) {
  return std::string(prefix) + " WSA error " + std::to_string(WSAGetLastError());
}

}  // namespace

WinsockRuntime::WinsockRuntime() {
  WSADATA data{};
  const int rc = WSAStartup(MAKEWORD(2, 2), &data);
  if (rc != 0) {
    throw std::runtime_error("WSAStartup failed: " + std::to_string(rc));
  }
}

WinsockRuntime::~WinsockRuntime() {
  WSACleanup();
}

Socket::~Socket() {
  close();
}

Socket::Socket(Socket&& other) noexcept : socket_(other.socket_) {
  other.socket_ = INVALID_SOCKET;
}

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    close();
    socket_ = other.socket_;
    other.socket_ = INVALID_SOCKET;
  }
  return *this;
}

std::optional<std::vector<std::uint8_t>> Socket::recv_some(std::size_t max_bytes) const {
  std::vector<std::uint8_t> buffer(max_bytes);
  const int received = ::recv(socket_, reinterpret_cast<char*>(buffer.data()),
                              static_cast<int>(buffer.size()), 0);
  if (received == 0) {
    return std::nullopt;
  }
  if (received == SOCKET_ERROR) {
    return std::nullopt;
  }
  buffer.resize(static_cast<std::size_t>(received));
  return buffer;
}

bool Socket::send_all(const std::vector<std::uint8_t>& bytes) const {
  std::size_t sent_total = 0;
  while (sent_total < bytes.size()) {
    const int sent = ::send(socket_, reinterpret_cast<const char*>(bytes.data() + sent_total),
                            static_cast<int>(bytes.size() - sent_total), 0);
    if (sent == SOCKET_ERROR || sent == 0) {
      return false;
    }
    sent_total += static_cast<std::size_t>(sent);
  }
  return true;
}

void Socket::close() {
  if (socket_ != INVALID_SOCKET) {
    closesocket(socket_);
    socket_ = INVALID_SOCKET;
  }
}

TcpServer::TcpServer(std::uint16_t port, std::string name)
    : port_(port), name_(std::move(name)) {}

TcpServer::~TcpServer() {
  if (listen_socket_ != INVALID_SOCKET) {
    closesocket(listen_socket_);
  }
}

void TcpServer::listen() {
  listen_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_socket_ == INVALID_SOCKET) {
    throw std::runtime_error(last_wsa_error("socket failed"));
  }

  BOOL yes = TRUE;
  setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port_);

  if (::bind(listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    throw std::runtime_error(last_wsa_error("bind failed"));
  }

  if (::listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR) {
    throw std::runtime_error(last_wsa_error("listen failed"));
  }

  std::cout << name_ << " listening on 127.0.0.1:" << port_ << "\n";
}

Socket TcpServer::accept_one() const {
  sockaddr_in client_addr{};
  int client_len = sizeof(client_addr);
  const SOCKET client = ::accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
  if (client == INVALID_SOCKET) {
    throw std::runtime_error(last_wsa_error("accept failed"));
  }
  return Socket(client);
}

}  // namespace fif::host
