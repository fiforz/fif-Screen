#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace fif::host {

struct EncoderConfig {
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  std::uint32_t refresh_hz = 60;
  std::uint32_t bitrate_bps = 12000000;
};

struct EncodedPacket {
  std::uint64_t frame_id = 0;
  std::uint64_t t0_capture_ns = 0;
  std::uint64_t t1_submit_ns = 0;
  std::uint64_t t2_encoded_ns = 0;
  bool is_idr = false;
  bool is_codec_config = false;
  std::vector<std::uint8_t> bytes;
};

class IVideoEncoder {
 public:
  virtual ~IVideoEncoder() = default;
  virtual bool initialize(const EncoderConfig& config) = 0;
  virtual bool reconfigure(const EncoderConfig& config) = 0;
  virtual bool submit_frame(void* d3d_texture, std::uint64_t frame_id, std::uint64_t timestamp_ns) = 0;
  virtual std::optional<EncodedPacket> poll_packet() = 0;
  virtual void request_idr() = 0;
  virtual void shutdown() = 0;
};

class NullVideoEncoder final : public IVideoEncoder {
 public:
  bool initialize(const EncoderConfig& config) override {
    config_ = config;
    return true;
  }

  bool reconfigure(const EncoderConfig& config) override {
    config_ = config;
    return true;
  }

  bool submit_frame(void*, std::uint64_t, std::uint64_t) override {
    return false;
  }

  std::optional<EncodedPacket> poll_packet() override {
    return std::nullopt;
  }

  void request_idr() override {
    idr_requested_ = true;
  }

  void shutdown() override {}

 private:
  EncoderConfig config_{};
  bool idr_requested_ = false;
};

}  // namespace fif::host

