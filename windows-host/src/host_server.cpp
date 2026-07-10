#include "host_server.hpp"

#include "adb.hpp"
#include "encoder.hpp"
#include "mf_h264_encoder.hpp"
#include "win32_socket.hpp"
#include "screen_capture.hpp"

#include <fif/protocol.hpp>

#include <mmsystem.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace fif::host {

namespace {

struct VideoMode {
  int width = 1920;
  int height = 1080;
  int fps = 50;
  int bitrate_bps = 16'000'000;
};

struct DirtyPayload {
  std::vector<std::uint8_t> bytes;
  std::uint32_t rect_count = 0;
  bool full_frame = false;
};

constexpr int kDirtyTileSize = 64;
constexpr std::uint16_t kDirtyFlagFullFrame = 1u << 0;

class ScopedTimerResolution {
 public:
  ScopedTimerResolution() : active_(timeBeginPeriod(1) == TIMERR_NOERROR) {}
  ~ScopedTimerResolution() {
    if (active_) {
      timeEndPeriod(1);
    }
  }

  ScopedTimerResolution(const ScopedTimerResolution&) = delete;
  ScopedTimerResolution& operator=(const ScopedTimerResolution&) = delete;

 private:
  bool active_ = false;
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
  mode.bitrate_bps = env_int("FIF_VIDEO_BITRATE_MBPS", mode.bitrate_bps / 1'000'000,
                             2, 40) * 1'000'000;
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

  if (!frame.bgra || frame.bgra_stride < frame.width * 4) {
    return {};
  }

  std::vector<std::uint8_t> out;
  out.resize(static_cast<std::size_t>(frame.width) * frame.height * 2);
  std::size_t dst = 0;
  for (int y = 0; y < frame.height; ++y) {
    const auto* row = frame.bgra + static_cast<std::size_t>(y) * frame.bgra_stride;
    for (int x = 0; x < frame.width; ++x) {
      const auto* source = row + static_cast<std::size_t>(x) * 4;
      const std::uint16_t r = source[2] >> 3;
      const std::uint16_t g = source[1] >> 2;
      const std::uint16_t b = source[0] >> 3;
      const std::uint16_t pixel = static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
      out[dst++] = static_cast<std::uint8_t>(pixel & 0xff);
      out[dst++] = static_cast<std::uint8_t>((pixel >> 8) & 0xff);
    }
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

DirtyPayload build_dirty_rgb565_payload(RawFrame& frame,
                                         std::vector<std::uint8_t>& previous_rgb565) {
  std::vector<std::uint8_t> converted_rgb565;
  const std::vector<std::uint8_t>* current_rgb565 = &frame.rgb565;
  if (current_rgb565->empty()) {
    converted_rgb565 = rgba_to_rgb565(frame);
    current_rgb565 = &converted_rgb565;
  }
  const bool full_frame = previous_rgb565.size() != current_rgb565->size();

  DirtyPayload payload;
  payload.full_frame = full_frame;
  payload.bytes.reserve(full_frame ? current_rgb565->size() + 4096 : 256 * 1024);
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
          rgb565_tile_changed(*current_rgb565, previous_rgb565, frame.width,
                              x, y, tile_width, tile_height);
      if (!changed) {
        continue;
      }

      append_u16_le(payload.bytes, static_cast<std::uint16_t>(x));
      append_u16_le(payload.bytes, static_cast<std::uint16_t>(y));
      append_u16_le(payload.bytes, static_cast<std::uint16_t>(tile_width));
      append_u16_le(payload.bytes, static_cast<std::uint16_t>(tile_height));
      append_rgb565_tile(payload.bytes, *current_rgb565, frame.width, x, y, tile_width, tile_height);
      ++payload.rect_count;
    }
  }

  payload.bytes[rect_count_offset + 0] = static_cast<std::uint8_t>(payload.rect_count & 0xffu);
  payload.bytes[rect_count_offset + 1] = static_cast<std::uint8_t>((payload.rect_count >> 8u) & 0xffu);
  payload.bytes[rect_count_offset + 2] = static_cast<std::uint8_t>((payload.rect_count >> 16u) & 0xffu);
  payload.bytes[rect_count_offset + 3] = static_cast<std::uint8_t>((payload.rect_count >> 24u) & 0xffu);

  if (!frame.rgb565.empty()) {
    previous_rgb565.swap(frame.rgb565);
  } else {
    previous_rgb565 = std::move(converted_rgb565);
  }
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
       << ",\"codec\":{\"mime\":\"video/avc\",\"lowLatency\":true}}";
  return json.str();
}

class LatestBgraCapture {
 public:
  struct FrameView {
    int slot = -1;
    const std::uint8_t* bgra = nullptr;
    std::uint32_t stride = 0;
    std::uint64_t frame_id = 0;
    std::uint64_t timestamp_ns = 0;
  };

  struct Stats {
    std::uint64_t captured = 0;
    std::uint64_t dropped = 0;
    std::uint64_t capture_time_ns = 0;
    std::uint64_t capture_time_max_ns = 0;
  };

  LatestBgraCapture(ScreenTarget target, VideoMode mode, bool save_capture_proof)
      : target_(std::move(target)), mode_(mode), save_capture_proof_(save_capture_proof) {
    const auto bytes = static_cast<std::size_t>(mode_.width) * mode_.height * 4;
    for (auto& slot : slots_) {
      slot.bgra.resize(bytes);
    }
  }

  ~LatestBgraCapture() {
    stop();
  }

  void start() {
    if (running_.exchange(true)) {
      return;
    }
    worker_ = std::thread([this] { capture_loop(); });
  }

  void stop() {
    running_ = false;
    ready_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  bool wait_next(FrameView& view, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!ready_.wait_for(lock, timeout, [this] {
          return ready_slot_ >= 0 || !running_.load();
        })) {
      return false;
    }
    if (ready_slot_ < 0) {
      return false;
    }

    const int index = ready_slot_;
    ready_slot_ = -1;
    auto& slot = slots_[index];
    slot.state = SlotState::Processing;
    view.slot = index;
    view.bgra = slot.bgra.data();
    view.stride = static_cast<std::uint32_t>(mode_.width * 4);
    view.frame_id = slot.frame_id;
    view.timestamp_ns = slot.timestamp_ns;
    return true;
  }

  void release(int index) {
    if (index < 0 || index >= static_cast<int>(slots_.size())) {
      return;
    }
    std::lock_guard lock(mutex_);
    slots_[index].state = SlotState::Free;
  }

  Stats take_stats() {
    return Stats{
        captured_.exchange(0), dropped_.exchange(0),
        capture_time_ns_.exchange(0), capture_time_max_ns_.exchange(0)};
  }

 private:
  enum class SlotState { Free, Writing, Ready, Processing };

  struct Slot {
    std::vector<std::uint8_t> bgra;
    SlotState state = SlotState::Free;
    std::uint64_t frame_id = 0;
    std::uint64_t timestamp_ns = 0;
  };

  static void update_max(std::atomic<std::uint64_t>& maximum, std::uint64_t value) {
    auto current = maximum.load(std::memory_order_relaxed);
    while (current < value &&
           !maximum.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
  }

  void capture_loop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    GdiScreenCapturer capturer(target_, mode_.width, mode_.height);
    RawFrame raw;
    bool capture_proof_saved = !save_capture_proof_;
    std::uint64_t frame_id = 0;
    const auto frame_interval = std::chrono::microseconds(1'000'000 / mode_.fps);
    auto next_frame_deadline = std::chrono::steady_clock::now();

    while (running_) {
      const auto capture_start = std::chrono::steady_clock::now();
      if (!capturer.capture(raw, false)) {
        std::this_thread::sleep_for(frame_interval);
        continue;
      }
      const std::uint64_t timestamp_ns = fif::monotonic_now_ns();

      if (save_capture_proof_ && !capture_proof_saved) {
        capture_proof_saved = save_rgba_png(
            L"artifacts\\usb-video-mvp\\capture-test.png", raw);
        std::cout << "capture proof saved="
                  << (capture_proof_saved ? "true" : "false")
                  << " path=artifacts\\usb-video-mvp\\capture-test.png\n";
      }

      int write_slot = -1;
      {
        std::lock_guard lock(mutex_);
        for (int i = 0; i < static_cast<int>(slots_.size()); ++i) {
          if (slots_[i].state == SlotState::Free) {
            write_slot = i;
            break;
          }
        }
        if (write_slot < 0 && ready_slot_ >= 0) {
          write_slot = ready_slot_;
          ready_slot_ = -1;
          dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        if (write_slot >= 0) {
          slots_[write_slot].state = SlotState::Writing;
        }
      }

      if (write_slot >= 0) {
        auto& destination = slots_[write_slot].bgra;
        const std::size_t row_bytes = static_cast<std::size_t>(mode_.width) * 4;
        for (int y = 0; y < mode_.height; ++y) {
          std::memcpy(destination.data() + static_cast<std::size_t>(y) * row_bytes,
                      raw.bgra + static_cast<std::size_t>(y) * raw.bgra_stride,
                      row_bytes);
        }

        {
          std::lock_guard lock(mutex_);
          if (ready_slot_ >= 0) {
            slots_[ready_slot_].state = SlotState::Free;
            dropped_.fetch_add(1, std::memory_order_relaxed);
          }
          auto& slot = slots_[write_slot];
          slot.frame_id = ++frame_id;
          slot.timestamp_ns = timestamp_ns;
          slot.state = SlotState::Ready;
          ready_slot_ = write_slot;
        }
        ready_.notify_one();
      } else {
        dropped_.fetch_add(1, std::memory_order_relaxed);
      }

      const auto capture_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - capture_start);
      const auto capture_ns = static_cast<std::uint64_t>(capture_elapsed.count());
      captured_.fetch_add(1, std::memory_order_relaxed);
      capture_time_ns_.fetch_add(capture_ns, std::memory_order_relaxed);
      update_max(capture_time_max_ns_, capture_ns);

      next_frame_deadline += frame_interval;
      const auto now = std::chrono::steady_clock::now();
      if (now < next_frame_deadline) {
        std::this_thread::sleep_until(next_frame_deadline);
      } else {
        next_frame_deadline = now;
      }
    }
  }

  ScreenTarget target_;
  VideoMode mode_;
  bool save_capture_proof_ = false;
  std::array<Slot, 3> slots_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  int ready_slot_ = -1;
  std::atomic<std::uint64_t> captured_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<std::uint64_t> capture_time_ns_{0};
  std::atomic<std::uint64_t> capture_time_max_ns_{0};
};

void run_h264_video_session(Socket& client,
                            const ScreenTarget& target,
                            const VideoMode& mode,
                            MfH264Encoder& encoder,
                            std::uint64_t& sequence,
                            bool save_capture_proof) {
  LatestBgraCapture capture(target, mode, save_capture_proof);
  capture.start();

  std::uint64_t frames_submitted = 0;
  std::uint64_t frames_sent = 0;
  std::uint64_t bytes_sent = 0;
  std::uint64_t last_frames_submitted = 0;
  std::uint64_t last_frames_sent = 0;
  std::uint64_t last_bytes_sent = 0;
  std::chrono::nanoseconds encode_time{};
  std::chrono::nanoseconds encode_time_max{};
  std::chrono::nanoseconds pipeline_latency{};
  std::chrono::nanoseconds pipeline_latency_max{};
  std::chrono::nanoseconds send_time{};
  std::chrono::nanoseconds send_time_max{};
  auto last_stats = std::chrono::steady_clock::now();

  while (client.valid()) {
    LatestBgraCapture::FrameView frame;
    if (!capture.wait_next(frame, std::chrono::milliseconds(500))) {
      continue;
    }

    const auto encode_start = std::chrono::steady_clock::now();
    auto encoded_packets = encoder.encode_bgra(
        frame.bgra, frame.stride, frame.frame_id, frame.timestamp_ns);
    capture.release(frame.slot);
    const auto encode_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - encode_start);
    encode_time += encode_elapsed;
    encode_time_max = std::max(encode_time_max, encode_elapsed);
    ++frames_submitted;

    if (!encoder.healthy()) {
      std::cerr << "H.264 encoder failed: " << encoder.last_error() << "\n";
      break;
    }

    bool send_ok = true;
    for (auto& encoded : encoded_packets) {
      fif::PacketHeader frame_header;
      frame_header.type = fif::MessageType::VideoFrame;
      frame_header.sequence = sequence++;
      frame_header.timestamp_ns = encoded.t0_capture_ns;
      if (encoded.is_codec_config) {
        frame_header.flags |= fif::kFlagCodecConfig;
      }
      if (encoded.is_idr) {
        frame_header.flags |= fif::kFlagIdrFrame;
      }

      const auto packet = fif::encode_packet(frame_header, encoded.bytes);
      const auto send_start = std::chrono::steady_clock::now();
      if (!client.send_all(packet)) {
        send_ok = false;
        break;
      }
      const auto send_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - send_start);
      send_time += send_elapsed;
      send_time_max = std::max(send_time_max, send_elapsed);
      if (!encoded.is_codec_config) {
        ++frames_sent;
        if (encoded.t2_encoded_ns >= encoded.t0_capture_ns) {
          const auto latency = std::chrono::nanoseconds(
              encoded.t2_encoded_ns - encoded.t0_capture_ns);
          pipeline_latency += latency;
          pipeline_latency_max = std::max(pipeline_latency_max, latency);
        }
      }
      bytes_sent += packet.size();
    }
    if (!send_ok) {
      std::cout << "video client disconnected while sending frame\n";
      break;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_stats >= std::chrono::seconds(1)) {
      const auto capture_stats = capture.take_stats();
      const auto interval_submitted = frames_submitted - last_frames_submitted;
      const auto interval_sent = frames_sent - last_frames_sent;
      const auto interval_bytes = bytes_sent - last_bytes_sent;
      const auto capture_divisor = std::max<std::uint64_t>(1, capture_stats.captured);
      const auto encode_divisor = std::max<std::uint64_t>(1, interval_submitted);
      std::cout << "FIFSCREEN_HOST event=video_stats frames_sent=" << frames_sent
                << " video_bytes_sent=" << bytes_sent
                << " interval_fps=" << interval_sent
                << " capture_fps=" << capture_stats.captured
                << " interval_bytes=" << interval_bytes
                << " codec=video/avc"
                << " pipeline_dropped=" << capture_stats.dropped
                << " output=" << mode.width << "x" << mode.height
                << " fps_target=" << mode.fps
                << " capture_avg_us=" << capture_stats.capture_time_ns / capture_divisor / 1000
                << " capture_max_us=" << capture_stats.capture_time_max_ns / 1000
                << " encode_avg_us="
                << std::chrono::duration_cast<std::chrono::microseconds>(encode_time).count() /
                       encode_divisor
                << " encode_max_us="
                << std::chrono::duration_cast<std::chrono::microseconds>(encode_time_max).count()
                << " pipeline_latency_avg_us="
                << std::chrono::duration_cast<std::chrono::microseconds>(pipeline_latency).count() /
                       std::max<std::uint64_t>(1, interval_sent)
                << " pipeline_latency_max_us="
                << std::chrono::duration_cast<std::chrono::microseconds>(pipeline_latency_max).count()
                << " send_avg_us="
                << std::chrono::duration_cast<std::chrono::microseconds>(send_time).count() /
                       encode_divisor
                << " send_max_us="
                << std::chrono::duration_cast<std::chrono::microseconds>(send_time_max).count()
                << "\n";
      last_frames_submitted = frames_submitted;
      last_frames_sent = frames_sent;
      last_bytes_sent = bytes_sent;
      encode_time = {};
      encode_time_max = {};
      pipeline_latency = {};
      pipeline_latency_max = {};
      send_time = {};
      send_time_max = {};
      last_stats = now;
    }
  }

  capture.stop();
}

}  // namespace

int HostServer::run() {
  ScopedTimerResolution timer_resolution;
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
    std::cout << "video client connected\n";

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

    EncoderConfig encoder_config;
    encoder_config.width = static_cast<std::uint32_t>(mode.width);
    encoder_config.height = static_cast<std::uint32_t>(mode.height);
    encoder_config.refresh_hz = static_cast<std::uint32_t>(mode.fps);
    encoder_config.bitrate_bps = static_cast<std::uint32_t>(mode.bitrate_bps);
    MfH264Encoder encoder;
    const bool h264_enabled = !env_flag_enabled("FIF_FORCE_DIRTY_RAW") &&
                              encoder.initialize(encoder_config);
    if (h264_enabled) {
      std::cout << "FIFSCREEN_HOST event=encoder_ready codec=video/avc encoder=\""
                << encoder.name() << "\" output=" << mode.width << "x" << mode.height
                << " fps_target=" << mode.fps
                << " bitrate_bps=" << mode.bitrate_bps << "\n";
    } else {
      std::cout << "FIFSCREEN_HOST event=encoder_fallback codec=raw-rgb565-dirty reason=\""
                << encoder.last_error() << "\"\n";
    }

    std::uint64_t sequence = 1;
    std::ostringstream config;
    config << "{\"codec\":\"" << (h264_enabled ? "video/avc" : "raw-rgb565-dirty")
           << "\",\"width\":" << mode.width
           << ",\"height\":" << mode.height
           << ",\"fps\":" << mode.fps
           << ",\"bitrateBps\":" << (h264_enabled ? mode.bitrate_bps : 0)
           << ",\"lowLatency\":true";
    if (!h264_enabled) {
      config << ",\"bytesPerPixel\":2"
             << ",\"tileSize\":" << kDirtyTileSize;
    }
    config
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

    const bool save_capture_proof = env_flag_enabled("FIF_SAVE_CAPTURE_PROOF");
    if (h264_enabled) {
      run_h264_video_session(
          client, *target, mode, encoder, sequence, save_capture_proof);
      continue;
    }

    GdiScreenCapturer capturer(*target, mode.width, mode.height);
    RawFrame frame;
    bool saved_capture_proof = !save_capture_proof;
    std::uint64_t frames_sent = 0;
    std::uint64_t bytes_sent = 0;
    std::uint64_t frames_captured = 0;
    std::uint64_t dirty_rects_sent = 0;
    std::uint64_t full_frames_sent = 0;
    std::uint64_t last_frames_sent = 0;
    std::uint64_t last_bytes_sent = 0;
    std::uint64_t last_frames_captured = 0;
    std::vector<std::uint8_t> previous_rgb565;
    std::chrono::nanoseconds capture_time{};
    std::chrono::nanoseconds encode_time{};
    std::chrono::nanoseconds send_time{};
    std::chrono::nanoseconds capture_time_max{};
    std::chrono::nanoseconds encode_time_max{};
    std::chrono::nanoseconds send_time_max{};
    auto last_stats = std::chrono::steady_clock::now();
    const auto frame_interval = std::chrono::microseconds(1'000'000 / mode.fps);
    auto next_frame_deadline = std::chrono::steady_clock::now();

    while (client.valid()) {
      const auto capture_start = std::chrono::steady_clock::now();
      if (!capturer.capture(frame, !h264_enabled)) {
        std::cerr << "capture failed; retrying\n";
        std::this_thread::sleep_for(frame_interval);
        continue;
      }
      const auto capture_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - capture_start);
      capture_time += capture_elapsed;
      capture_time_max = std::max(capture_time_max, capture_elapsed);
      ++frames_captured;

      if (save_capture_proof && !saved_capture_proof) {
        saved_capture_proof =
            save_rgba_png(L"artifacts\\usb-video-mvp\\capture-test.png", frame);
        std::cout << "capture proof saved="
                  << (saved_capture_proof ? "true" : "false")
                  << " path=artifacts\\usb-video-mvp\\capture-test.png\n";
      }

      const std::uint64_t capture_timestamp_ns = fif::monotonic_now_ns();
      const auto encode_start = std::chrono::steady_clock::now();
      std::vector<EncodedPacket> encoded_packets;
      if (h264_enabled) {
        encoded_packets = encoder.encode_bgra(
            frame.bgra, static_cast<std::uint32_t>(frame.bgra_stride),
            frames_captured, capture_timestamp_ns);
        if (!encoder.healthy()) {
          std::cerr << "H.264 encoder failed: " << encoder.last_error() << "\n";
          break;
        }
      } else {
        auto dirty = build_dirty_rgb565_payload(frame, previous_rgb565);
        EncodedPacket raw_packet;
        raw_packet.t0_capture_ns = capture_timestamp_ns;
        raw_packet.is_idr = dirty.full_frame;
        raw_packet.bytes = std::move(dirty.bytes);
        encoded_packets.push_back(std::move(raw_packet));
        if (dirty.full_frame) {
          ++full_frames_sent;
        }
        dirty_rects_sent += dirty.rect_count;
      }
      const auto encode_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - encode_start);
      encode_time += encode_elapsed;
      encode_time_max = std::max(encode_time_max, encode_elapsed);

      bool send_ok = true;
      for (auto& encoded : encoded_packets) {
        fif::PacketHeader frame_header;
        frame_header.type = fif::MessageType::VideoFrame;
        frame_header.sequence = sequence++;
        frame_header.timestamp_ns = encoded.t0_capture_ns;
        if (encoded.is_codec_config) {
          frame_header.flags |= fif::kFlagCodecConfig;
        }
        if (encoded.is_idr) {
          frame_header.flags |= fif::kFlagIdrFrame;
        }

        const auto packet = fif::encode_packet(frame_header, encoded.bytes);
        const auto send_start = std::chrono::steady_clock::now();
        if (!client.send_all(packet)) {
          send_ok = false;
          break;
        }
        const auto send_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - send_start);
        send_time += send_elapsed;
        send_time_max = std::max(send_time_max, send_elapsed);
        if (!encoded.is_codec_config) {
          ++frames_sent;
        }
        bytes_sent += packet.size();
      }
      if (!send_ok) {
        std::cout << "video client disconnected while sending frame\n";
        break;
      }

      const auto now = std::chrono::steady_clock::now();
      if (now - last_stats >= std::chrono::seconds(1)) {
        const auto interval_frames = frames_sent - last_frames_sent;
        const auto interval_bytes = bytes_sent - last_bytes_sent;
        const auto interval_captured = frames_captured - last_frames_captured;
        const auto frame_divisor = std::max<std::uint64_t>(1, interval_captured);
        std::cout << "FIFSCREEN_HOST event=video_stats frames_sent=" << frames_sent
                  << " video_bytes_sent=" << bytes_sent
                  << " interval_fps=" << interval_frames
                  << " capture_fps=" << interval_captured
                  << " interval_bytes=" << interval_bytes
                  << " codec=" << (h264_enabled ? "video/avc" : "raw-rgb565-dirty")
                  << " dirty_rects_sent=" << dirty_rects_sent
                  << " full_frames_sent=" << full_frames_sent
                  << " output=" << mode.width << "x" << mode.height
                  << " fps_target=" << mode.fps
                  << " capture_avg_us="
                  << std::chrono::duration_cast<std::chrono::microseconds>(capture_time).count() /
                         frame_divisor
                  << " capture_max_us="
                  << std::chrono::duration_cast<std::chrono::microseconds>(capture_time_max).count()
                  << " encode_avg_us="
                  << std::chrono::duration_cast<std::chrono::microseconds>(encode_time).count() /
                         frame_divisor
                  << " encode_max_us="
                  << std::chrono::duration_cast<std::chrono::microseconds>(encode_time_max).count()
                  << " send_avg_us="
                  << std::chrono::duration_cast<std::chrono::microseconds>(send_time).count() /
                         frame_divisor
                  << " send_max_us="
                  << std::chrono::duration_cast<std::chrono::microseconds>(send_time_max).count()
                  << "\n";
        last_frames_sent = frames_sent;
        last_bytes_sent = bytes_sent;
        last_frames_captured = frames_captured;
        dirty_rects_sent = 0;
        full_frames_sent = 0;
        capture_time = {};
        encode_time = {};
        send_time = {};
        capture_time_max = {};
        encode_time_max = {};
        send_time_max = {};
        last_stats = now;
      }

      next_frame_deadline += frame_interval;
      const auto pacing_now = std::chrono::steady_clock::now();
      if (pacing_now < next_frame_deadline) {
        std::this_thread::sleep_until(next_frame_deadline);
      } else {
        next_frame_deadline = pacing_now;
        std::this_thread::yield();
      }
    }
  }
}

}  // namespace fif::host
