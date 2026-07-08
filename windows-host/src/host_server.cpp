#include "host_server.hpp"

#include "adb.hpp"
#include "encoder.hpp"
#include "win32_socket.hpp"
#include "screen_capture.hpp"

#include <fif/protocol.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace fif::host {

namespace {

struct VideoMode {
  int width = 1920;
  int height = 1080;
  int fps = 30;
};

int env_int(const char* name, int fallback, int min_value, int max_value) {
  const char* raw = std::getenv(name);
  if (!raw || raw[0] == '\0') {
    return fallback;
  }
  try {
    const int parsed = std::stoi(raw);
    return std::clamp(parsed, min_value, max_value);
  } catch (...) {
    return fallback;
  }
}

VideoMode selected_video_mode() {
  VideoMode mode;
  mode.width = env_int("FIF_VIDEO_WIDTH", mode.width, 320, 1920);
  mode.height = env_int("FIF_VIDEO_HEIGHT", mode.height, 180, 1080);
  mode.fps = env_int("FIF_VIDEO_FPS", mode.fps, 1, 60);
  return mode;
}

bool env_flag_enabled(const char* name) {
  const char* raw = std::getenv(name);
  if (!raw) {
    return false;
  }
  const std::string value(raw);
  return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
}

std::vector<std::uint8_t> rgba_to_rgb565(const RawFrame& frame) {
  std::vector<std::uint8_t> out;
  out.resize(static_cast<std::size_t>(frame.width) * frame.height * 2);
  std::size_t dst = 0;
  for (std::size_t src = 0; src + 3 < frame.rgba.size(); src += 4) {
    const std::uint16_t r = frame.rgba[src + 0] >> 3;
    const std::uint16_t g = frame.rgba[src + 1] >> 2;
    const std::uint16_t b = frame.rgba[src + 2] >> 3;
    const std::uint16_t pixel = static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
    out[dst++] = static_cast<std::uint8_t>(pixel & 0xff);
    out[dst++] = static_cast<std::uint8_t>((pixel >> 8) & 0xff);
  }
  return out;
}

std::vector<std::uint8_t> make_json_payload(const std::string& json) {
  return fif::bytes_from_string(json);
}

std::vector<std::uint8_t> make_packet(fif::MessageType type,
                                      std::uint64_t sequence,
                                      const std::string& json) {
  fif::PacketHeader header;
  header.type = type;
  header.sequence = sequence;
  return fif::encode_packet(header, make_json_payload(json));
}

std::string make_hello_ack_json(std::uint16_t control_port, std::uint16_t video_port) {
  const VideoMode mode = selected_video_mode();
  std::ostringstream json;
  json << "{\"role\":\"windows-host\",\"protocol\":1,\"controlPort\":" << control_port
       << ",\"videoPort\":" << video_port
       << ",\"selectedMode\":{\"width\":" << mode.width
       << ",\"height\":" << mode.height
       << ",\"refreshHz\":" << mode.fps << "}"
       << ",\"codec\":{\"mime\":\"application/fif-raw-rgb565\",\"lowLatency\":true}}";
  return json.str();
}

}  // namespace

int HostServer::run() {
  WinsockRuntime winsock;
  AdbReverseManager adb;

  if (config_.setup_adb_reverse) {
    if (!adb.setup({config_.control_port, config_.video_port})) {
      std::cerr << "adb reverse setup failed; use --no-adb for local loopback testing\n";
      return 2;
    }
  }

  std::thread control([this] { run_control_channel(); });
  std::thread video([this] { run_video_channel(); });

  control.join();
  video.join();

  if (config_.setup_adb_reverse) {
    adb.remove({config_.control_port, config_.video_port});
  }

  return 0;
}

void HostServer::run_control_channel() {
  TcpServer server(config_.control_port, "control");
  server.listen();

  std::uint64_t sequence = 1;

  for (;;) {
    std::cout << "waiting for Android control client\n";
    Socket client = server.accept_one();
    std::cout << "control client connected\n";

    fif::PacketReader reader(fif::kMaxControlPayload);
    const auto hello_ack = make_packet(
        fif::MessageType::HelloAck,
        sequence++,
        make_hello_ack_json(config_.control_port, config_.video_port));

    while (client.valid()) {
      auto bytes = client.recv_some(4096);
      if (!bytes) {
        std::cout << "control client disconnected\n";
        break;
      }

      try {
        reader.feed(bytes->data(), bytes->size());
        while (auto packet = reader.next()) {
          switch (packet->header.type) {
            case fif::MessageType::Hello:
              std::cout << "FIFSCREEN_HOST event=hello_received windows_timestamp_ns="
                        << fif::monotonic_now_ns() << "\n";
              if (!client.send_all(hello_ack)) {
                client.close();
              } else {
                std::cout << "FIFSCREEN_HOST event=hello_ack_sent windows_timestamp_ns="
                          << fif::monotonic_now_ns() << "\n";
              }
              break;
            case fif::MessageType::Ping: {
              auto pong = make_packet(fif::MessageType::Pong, sequence++, "{}");
              if (!client.send_all(pong)) {
                client.close();
              }
              break;
            }
            case fif::MessageType::Stats:
              std::cout << "received Android stats packet length="
                        << packet->payload.size() << "\n";
              break;
            case fif::MessageType::RequestIdr:
              std::cout << "Android requested IDR frame\n";
              break;
            default:
              std::cout << "control packet type="
                        << static_cast<std::uint16_t>(packet->header.type)
                        << " length=" << packet->payload.size() << "\n";
              break;
          }
        }
      } catch (const std::exception& e) {
        std::cerr << "control protocol error: " << e.what() << "\n";
        client.close();
      }
    }
  }
}

void HostServer::run_video_channel() {
  TcpServer server(config_.video_port, "video");
  server.listen();

  auto target = find_fifscreen_display();
  if (!target) {
    std::cerr << "FifScreen display not found; video channel will retry on client connect\n";
  }

  TestOverlayWindow overlay;
  if (target) {
    std::cout << "selected capture display name=" << narrow(target->device_name)
              << " string=" << narrow(target->device_string)
              << " pos=" << target->x << "," << target->y
              << " size=" << target->width << "x" << target->height << "\n";
    if (env_flag_enabled("FIF_SHOW_TEST_OVERLAY")) {
      overlay.start(*target);
    }
  }

  for (;;) {
    std::cout << "waiting for Android video client\n";
    Socket client = server.accept_one();
    std::cout << "video client connected; streaming raw FifScreen capture\n";

    if (!target) {
      target = find_fifscreen_display();
      if (target) {
        if (env_flag_enabled("FIF_SHOW_TEST_OVERLAY")) {
          overlay.start(*target);
        }
      } else {
        std::cerr << "no capture display available for this video client\n";
        client.close();
        continue;
      }
    }

    const VideoMode mode = selected_video_mode();
    GdiScreenCapturer capturer(*target, mode.width, mode.height);

    std::uint64_t sequence = 1;
    std::ostringstream config;
    config << "{\"codec\":\"raw-rgb565\",\"width\":" << mode.width
           << ",\"height\":" << mode.height
           << ",\"fps\":" << mode.fps
           << ",\"bytesPerPixel\":2"
           << ",\"sourceDisplay\":\"" << narrow(target->device_string)
           << "\",\"sourceWidth\":" << target->width
           << ",\"sourceHeight\":" << target->height << "}";

    fif::PacketHeader config_header;
    config_header.type = fif::MessageType::VideoConfig;
    config_header.sequence = sequence++;
    if (!client.send_all(fif::encode_packet(config_header, fif::bytes_from_string(config.str())))) {
      std::cout << "video client disconnected while sending config\n";
      continue;
    }

    RawFrame frame;
    const bool save_capture_proof = env_flag_enabled("FIF_SAVE_CAPTURE_PROOF");
    bool saved_capture_proof = !save_capture_proof;
    std::uint64_t frames_sent = 0;
    std::uint64_t bytes_sent = 0;
    auto last_stats = std::chrono::steady_clock::now();
    const auto frame_interval = std::chrono::microseconds(1'000'000 / mode.fps);

    while (client.valid()) {
      const auto frame_start = std::chrono::steady_clock::now();
      if (!capturer.capture(frame)) {
        std::cerr << "capture failed; retrying\n";
        std::this_thread::sleep_for(frame_interval);
        continue;
      }

      if (save_capture_proof && !saved_capture_proof) {
        saved_capture_proof =
            save_rgba_png(L"artifacts\\usb-video-mvp\\capture-test.png", frame);
        std::cout << "capture proof saved="
                  << (saved_capture_proof ? "true" : "false")
                  << " path=artifacts\\usb-video-mvp\\capture-test.png\n";
      }

      fif::PacketHeader frame_header;
      frame_header.type = fif::MessageType::VideoFrame;
      frame_header.sequence = sequence++;
      const auto payload = rgba_to_rgb565(frame);
      const auto packet = fif::encode_packet(frame_header, payload);
      if (!client.send_all(packet)) {
        std::cout << "video client disconnected while sending frame\n";
        break;
      }
      ++frames_sent;
      bytes_sent += packet.size();

      const auto now = std::chrono::steady_clock::now();
      if (now - last_stats >= std::chrono::seconds(1)) {
        std::cout << "FIFSCREEN_HOST event=video_stats frames_sent=" << frames_sent
                  << " video_bytes_sent=" << bytes_sent
                  << " output=" << mode.width << "x" << mode.height
                  << " fps_target=" << mode.fps << "\n";
        last_stats = now;
      }

      const auto elapsed = std::chrono::steady_clock::now() - frame_start;
      if (elapsed < frame_interval) {
        std::this_thread::sleep_for(frame_interval - elapsed);
      }
    }
  }
}

}  // namespace fif::host
