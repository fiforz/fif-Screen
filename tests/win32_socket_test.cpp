#include "win32_socket.hpp"

#include <iostream>
#include <stdexcept>

int main() {
  fif::host::WinsockRuntime winsock;
  fif::host::TcpServer first(0, "first", fif::host::BindMode::Loopback);
  first.listen();

  const std::uint16_t port = first.local_port();
  if (port == 0) {
    std::cerr << "first server did not receive an ephemeral port\n";
    return 1;
  }

  fif::host::TcpServer duplicate(
      port, "duplicate", fif::host::BindMode::Loopback);
  try {
    duplicate.listen();
  } catch (const std::runtime_error&) {
    std::cout << "exclusive TCP bind test passed port=" << port << "\n";
    return 0;
  }

  std::cerr << "duplicate TCP listener unexpectedly succeeded port=" << port
            << "\n";
  return 1;
}
