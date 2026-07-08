#include "host_server.hpp"

#include "adb.hpp"
#include "encoder.hpp"
#include "win32_socket.hpp"

#include <fif/protocol.hpp>

#include <atomic>
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
       << ",\"selectedMode\":{\"width\":1280,\"height\":720,\"refreshHz\":60}"
       << ",\"codec\":{\"mime\":\"video/avc\",\"lowLatency\":true}}";
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

  NullVideoEncoder encoder;
  encoder.initialize({});

  for (;;) {
    std::cout << "waiting for Android video client\n";
    Socket client = server.accept_one();
    std::cout << "video client connected; encoder/capture path is not wired yet\n";

    // Keep the channel open for protocol testing. Real H.264 packets will be
    // produced after the capture and hardware encoder backend is connected.
    while (client.valid()) {
      auto bytes = client.recv_some(1024);
      if (!bytes) {
        std::cout << "video client disconnected\n";
        break;
      }
    }
  }
}

}  // namespace fif::host
