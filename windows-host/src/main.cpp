#include "host_server.hpp"

#include <charconv>
#include <iostream>
#include <system_error>
#include <string_view>

namespace {

bool parse_port(std::string_view value, std::uint16_t& out) {
  unsigned parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end || parsed > 65535) {
    return false;
  }
  out = static_cast<std::uint16_t>(parsed);
  return true;
}

void print_usage() {
  std::cout << "fif-host [--control-port N] [--video-port N] [--no-adb]\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  fif::host::HostConfig config;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      print_usage();
      return 0;
    }
    if (arg == "--no-adb") {
      config.setup_adb_reverse = false;
      continue;
    }
    if (arg == "--control-port" && i + 1 < argc) {
      if (!parse_port(argv[++i], config.control_port)) {
        std::cerr << "invalid control port\n";
        return 1;
      }
      continue;
    }
    if (arg == "--video-port" && i + 1 < argc) {
      if (!parse_port(argv[++i], config.video_port)) {
        std::cerr << "invalid video port\n";
        return 1;
      }
      continue;
    }

    std::cerr << "unknown argument: " << arg << "\n";
    print_usage();
    return 1;
  }

  try {
    return fif::host::HostServer(config).run();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
