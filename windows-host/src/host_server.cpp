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
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace fif::host {

namespace {

struct VideoMode {
  int width = 1600;
  int height = 900;
  int fps = 60;
};

struct DirtyPayload {
  std::vector<std::uint8_t> bytes;
  std::uint32_t rect_count = 0;
  bool full_frame = false;
};

constexpr int kDirtyTileSize = 64;
constexpr std::uint16_t kDirtyFlagFullFrame = 1u << 0;

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
  if (!frame.rgb565.empty()) {
    return frame.rgb565;
  }

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

void append_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu));
  }
}

bool rgb565_tile_changed(const std::vector<std::uint8_t>& current,
                         const std::vector<std::uint8_t>& previous,
                         int frame_width,
                         int x,
                         int y,
                         int width,
                         int height) {
  const std::size_t stride = static_cast<std::size_t>(frame_width) * 2;
  const std::size_t row_bytes = static_cast<std::size_t>(width) * 2;
  for (int row = 0; row < height; ++row) {
    const std::size_t offset = (static_cast<std::size_t>(y + row) * frame_width + x) * 2;
    if (std::memcmp(current.data() + offset, previous.data() + offset, row_bytes) != 0) {
      return true;
    }
  }
  return false;
}

void append_rgb565_tile(std::vector<std::uint8_t>& out,
                        const std::vector<std::uint8_t>& rgb565,
                        int frame_width,
                        int x,
                        int y,
                        int width,
                        int height) {
  const std::size_t row_bytes = static_cast<std::size_t>(width) * 2;
  for (int row = 0; row < height; ++row) {
    const std::size_t offset = (static_cast<std::size_t>(y + row) * frame_width + x) * 2;
    out.insert(out.end(), rgb565.begin() + static_cast<std::ptrdiff_t>(offset),
               rgb565.begin() + static_cast<std::ptrdiff_t>(offset + row_bytes));
  }
}

DirtyPayload build_dirty_rgb565_payload(const RawFrame& frame,
                                        std::vector<std::uint8_t>& previous_rgb565) {
  auto current_rgb565 = rgba_to_rgb565(frame);
  const bool full_frame = previous_rgb565.size() != current_rgb565.size();

  DirtyPayload payload;
  payload.full_frame = full_frame;
  payload.bytes.reserve(full_frame ? current_rgb565.size() + 4096 : 256 * 1024);
  payload.bytes.insert(payload.bytes.end(), {'F', 'D', 'R', '1'});
  append_u16_le(payload.bytes, static_cast<std::uint16_t>(kDirtyTileSize));
  append_u16_le(payload.bytes, full_frame ? kDirtyFlagFullFrame : 0);
  const std::size_t rect_count_offset = payload.bytes.size();
  append_u32_le(payload.bytes, 0);

  for (int y = 0; y < frame.height; y += kDirtyTileSize) {
    const int tile_height = std::min(kDirtyTileSize, frame.height - y);
    for (int x = 0; x < frame.width; x += kDirtyTileSize) {
      const int tile_width = std::min(kDirtyTileSize, frame.width - x);
      const bool changed = full_frame ||
          rgb565_tile_changed(current_rgb565, previous_rgb565, frame.width,
                              x, y, tile_width, tile_height);
      if (!changed) {
        continue;
      }

      append_u16_le(payload.bytes, static_cast<std::uint16_t>(x));
      append_u16_le(payload.bytes, static_cast<std::uint16_t>(y));
      append_u16_le(payload.bytes, static_cast<std::uint16_t>(tile_width));
      append_u16_le(payload.bytes, static_cast<std::uint16_t>(tile_height));
      append_rgb565_tile(payload.bytes, current_rgb565, frame.width, x, y, tile_width, tile_height);
      ++payload.rect_count;
    }
  }

  payload.bytes[rect_count_offset + 0] = static_cast<std::uint8_t>(payload.rect_count & 0xffu);
  payload.bytes[rect_count_offset + 1] = static_cast<std::uint8_t>((payload.rect_count >> 8u) & 0xffu);
  payload.bytes[rect_count_offset + 2] = static_cast<std::uint8_t>((payload.rect_count >> 16u) & 0xffu);
  payload.bytes[rect_count_offset + 3] = static_cast<std::uint8_t>((payload.rect_count >> 24u) & 0xffu);

  previous_rgb565 = std::move(current_rgb565);
  return payload;
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
       << ",\"codec\":{\"mime\":\"application/fif-raw-rgb565-dirty\",\"lowLatency\":true}}";
  return json.str();
}

}  // namespace

int HostServer::run() {
  WinsockRuntime winsock;

  if (config_.setup_adb_reverse) {
    std::thread adb_reverse([this] { run_adb_reverse_maintainer(); });
    adb_reverse.detach();
  }

  std::thread control([this] { run_control_channel(); });
  std::thread video([this] { run_video_channel(); });

  control.join();
  video.join();

  return 0;
}

void HostServer::run_adb_reverse_maintainer() {
  AdbReverseManager adb;
  bool configured = false;
  for (;;) {
    const bool ok = adb.setup({config_.control_port, config_.video_port});
    if (ok && !configured) {
      std::cout << "adb reverse configured for Android reconnect\n";
    }
    if (!ok && configured) {
      std::cout << "adb reverse lost; waiting for Android device\n";
    }
    configured = ok;
    std::this_thread::sleep_for(ok ? std::chrono::seconds(5) : std::chrono::seconds(2));
  }
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
    std::cout << "video client connected; streaming dirty raw FifScreen capture\n";

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
    config << "{\"codec\":\"raw-rgb565-dirty\",\"width\":" << mode.width
           << ",\"height\":" << mode.height
           << ",\"fps\":" << mode.fps
           << ",\"bytesPerPixel\":2"
           << ",\"tileSize\":" << kDirtyTileSize
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
    std::uint64_t dirty_rects_sent = 0;
    std::uint64_t full_frames_sent = 0;
    std::uint64_t last_frames_sent = 0;
    std::uint64_t last_bytes_sent = 0;
    std::vector<std::uint8_t> previous_rgb565;
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
      auto dirty = build_dirty_rgb565_payload(frame, previous_rgb565);
      if (dirty.full_frame) {
        frame_header.flags |= fif::kFlagIdrFrame;
        ++full_frames_sent;
      }
      dirty_rects_sent += dirty.rect_count;
      const auto packet = fif::encode_packet(frame_header, dirty.bytes);
      if (!client.send_all(packet)) {
        std::cout << "video client disconnected while sending frame\n";
        break;
      }
      ++frames_sent;
      bytes_sent += packet.size();

      const auto now = std::chrono::steady_clock::now();
      if (now - last_stats >= std::chrono::seconds(1)) {
        const auto interval_frames = frames_sent - last_frames_sent;
        const auto interval_bytes = bytes_sent - last_bytes_sent;
        std::cout << "FIFSCREEN_HOST event=video_stats frames_sent=" << frames_sent
                  << " video_bytes_sent=" << bytes_sent
                  << " interval_fps=" << interval_frames
                  << " interval_bytes=" << interval_bytes
                  << " dirty_rects_sent=" << dirty_rects_sent
                  << " full_frames_sent=" << full_frames_sent
                  << " output=" << mode.width << "x" << mode.height
                  << " fps_target=" << mode.fps << "\n";
        last_frames_sent = frames_sent;
        last_bytes_sent = bytes_sent;
        dirty_rects_sent = 0;
        full_frames_sent = 0;
        last_stats = now;
      }

      const auto elapsed = std::chrono::steady_clock::now() - frame_start;
      if (elapsed < frame_interval) {
        std::this_thread::sleep_for(frame_interval - elapsed);
      } else {
        std::this_thread::yield();
      }
    }
  }
}

}  // namespace fif::host
