#pragma once

#include <cstdint>

namespace fif::host {

struct HostConfig {
  std::uint16_t control_port = 27183;
  std::uint16_t video_port = 27184;
  bool setup_adb_reverse = true;
};

class HostServer {
 public:
  explicit HostServer(HostConfig config) : config_(config) {}
  int run();

 private:
  void run_adb_reverse_maintainer();
  void run_control_channel();
  void run_video_channel();

  HostConfig config_;
};

}  // namespace fif::host
