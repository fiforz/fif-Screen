#pragma once

#include "encoder.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fif::host {

class MfH264Encoder {
 public:
  MfH264Encoder();
  ~MfH264Encoder();

  MfH264Encoder(const MfH264Encoder&) = delete;
  MfH264Encoder& operator=(const MfH264Encoder&) = delete;

  bool initialize(const EncoderConfig& config);
  std::vector<EncodedPacket> encode_bgra(const std::uint8_t* bgra,
                                         std::uint32_t stride,
                                         std::uint64_t frame_id,
                                         std::uint64_t timestamp_ns);
  void request_idr();
  void shutdown();

  [[nodiscard]] bool healthy() const;
  [[nodiscard]] const std::string& name() const;
  [[nodiscard]] const std::string& last_error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fif::host
