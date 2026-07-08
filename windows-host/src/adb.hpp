#pragma once

#include <cstdint>
#include <string>

namespace fif::host {

struct AdbReverseConfig {
  std::uint16_t control_port = 27183;
  std::uint16_t video_port = 27184;
};

class AdbReverseManager {
 public:
  [[nodiscard]] bool is_adb_available() const;
  [[nodiscard]] bool setup(const AdbReverseConfig& config) const;
  void remove(const AdbReverseConfig& config) const;

 private:
  [[nodiscard]] std::string adb_command() const;
};

}  // namespace fif::host
