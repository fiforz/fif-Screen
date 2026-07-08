#include "host_server.hpp"

#include "adb.hpp"
#include "encoder.hpp"
#include "win32_socket.hpp"
#include "screen_capture.hpp"

#include <fif/protocol.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace fif::host {

namespace {

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
  std::ostringstream json;
  json << "{\"role\":\"windows-host\",\"protocol\":1,\"controlPort\":" << control_port
       << ",\"videoPort\":" << video_port
       << ",\"selectedMode\":{\"width\":640,\"height\":360,\"refreshHz\":10}"
       << ",\"codec\":{\"mime\":\"application/fif-raw-rgba\",\"lowLatency\":true}}";
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
    overlay.start(*target);
  }

  for (;;) {
    std::cout << "waiting for Android video client\n";
    Socket client = server.accept_one();
    std::cout << "video client connected; streaming raw FifScreen capture\n";

    if (!target) {
      target = find_fifscreen_display();
      if (target) {
        overlay.start(*target);
      } else {
        std::cerr << "no capture display available for this video client\n";
        client.close();
        continue;
      }
    }

    constexpr int kOutputWidth = 640;
    constexpr int kOutputHeight = 360;
    constexpr int kFps = 10;
    GdiScreenCapturer capturer(*target, kOutputWidth, kOutputHeight);

    std::uint64_t sequence = 1;
    std::ostringstream config;
    config << "{\"codec\":\"raw-rgba\",\"width\":" << kOutputWidth
           << ",\"height\":" << kOutputHeight
           << ",\"fps\":" << kFps
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
    bool saved_capture_proof = false;
    std::uint64_t frames_sent = 0;
    std::uint64_t bytes_sent = 0;
    auto last_stats = std::chrono::steady_clock::now();
    const auto frame_interval = std::chrono::milliseconds(1000 / kFps);

    while (client.valid()) {
      const auto frame_start = std::chrono::steady_clock::now();
      if (!capturer.capture(frame)) {
        std::cerr << "capture failed; retrying\n";
        std::this_thread::sleep_for(frame_interval);
        continue;
      }

      if (!saved_capture_proof) {
        saved_capture_proof =
            save_rgba_png(L"artifacts\\usb-video-mvp\\capture-test.png", frame);
        std::cout << "capture proof saved="
                  << (saved_capture_proof ? "true" : "false")
                  << " path=artifacts\\usb-video-mvp\\capture-test.png\n";
      }

      fif::PacketHeader frame_header;
      frame_header.type = fif::MessageType::VideoFrame;
      frame_header.sequence = sequence++;
      const auto packet = fif::encode_packet(frame_header, frame.rgba);
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
                  << " output=" << kOutputWidth << "x" << kOutputHeight
                  << " fps_target=" << kFps << "\n";
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
