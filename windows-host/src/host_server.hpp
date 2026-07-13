#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace fif::host {

class Socket;
class TcpServer;
class UdpServer;

enum class TransportMode {
  Usb,
  Lan,
  Relay,
};

struct HostConfig {
  std::uint16_t control_port = 27183;
  std::uint16_t video_port = 27184;
  std::uint16_t discovery_port = 27182;
  bool setup_adb_reverse = true;
  TransportMode transport = TransportMode::Usb;
  std::string pairing_pin;
  std::string relay_url;
};

class HostServer {
 public:
  explicit HostServer(HostConfig config) : config_(config) {}
  int run();

 private:
  void run_adb_reverse_maintainer();
  void run_lan_discovery(UdpServer& server);
  void run_control_channel(TcpServer& server);
  void run_video_channel(TcpServer& server);
  bool authenticate_lan_control(Socket& client, std::uint64_t& sequence);
  bool authenticate_lan_video(Socket& client, std::uint64_t& sequence);

  struct LanSession {
    std::array<std::uint8_t, 32> key{};
    std::uint64_t generation = 0;
    std::chrono::steady_clock::time_point created{};
    bool video_authenticated = false;
  };

  void publish_lan_session(const std::array<std::uint8_t, 32>& key);
  [[nodiscard]] std::optional<LanSession> current_lan_session();
  bool complete_video_auth(std::uint64_t generation);

  HostConfig config_;
  std::mutex lan_session_mutex_;
  std::optional<LanSession> lan_session_;
  std::uint64_t lan_session_generation_ = 0;
};

}  // namespace fif::host
