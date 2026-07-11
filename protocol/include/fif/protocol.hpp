#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fif {

inline constexpr std::array<std::uint8_t, 4> kMagic{'F', 'I', 'F', '1'};
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kHeaderSize = 32;
inline constexpr std::uint32_t kMaxControlPayload = 1u * 1024u * 1024u;
inline constexpr std::uint32_t kMaxVideoPayload = 16u * 1024u * 1024u;
inline constexpr std::uint8_t kInputPayloadVersion = 1;
inline constexpr std::size_t kMaxTouchContacts = 16;
inline constexpr std::size_t kTouchFrameHeaderSize = 4;
inline constexpr std::size_t kTouchContactSize = 14;
inline constexpr std::uint16_t kMaxTouchPointerId = 256;
inline constexpr std::uint16_t kMaxTouchPressure = 1024;
inline constexpr std::uint16_t kDiscoveryPort = 27182;
inline constexpr std::size_t kDiscoveryPacketSize = 20;
inline constexpr std::array<std::uint8_t, 8> kDiscoveryRequestMagic{
    'F', 'I', 'F', 'D', 'I', 'S', 'C', '1'};
inline constexpr std::array<std::uint8_t, 8> kDiscoveryResponseMagic{
    'F', 'I', 'F', 'H', 'E', 'R', 'E', '1'};
inline constexpr std::uint8_t kPairingPayloadVersion = 1;
inline constexpr std::uint32_t kPairingPbkdf2Iterations = 100'000;
inline constexpr std::size_t kPairingSaltSize = 16;
inline constexpr std::size_t kPairingNonceSize = 32;
inline constexpr std::size_t kPairingProofSize = 32;
inline constexpr std::size_t kPairChallengePayloadSize = 56;
inline constexpr std::size_t kPairResponsePayloadSize = 68;
inline constexpr std::size_t kPairResultPayloadSize = 36;
inline constexpr std::size_t kVideoChallengePayloadSize = 36;
inline constexpr std::size_t kVideoAuthPayloadSize = 36;

enum class MessageType : std::uint16_t {
  Hello = 1,
  HelloAck = 2,
  Ping = 3,
  Pong = 4,
  DisplayMode = 5,
  CodecConfig = 6,
  Stats = 7,
  RequestIdr = 8,
  Disconnect = 9,
  PairChallenge = 10,
  PairResponse = 11,
  PairResult = 12,
  VideoChallenge = 13,
  VideoAuth = 14,
  VideoConfig = 100,
  VideoFrame = 101,
  InputEvent = 200,
  Error = 900,
};

enum PacketFlags : std::uint32_t {
  kFlagNone = 0,
  kFlagCodecConfig = 1u << 0,
  kFlagIdrFrame = 1u << 1,
  kFlagDroppedBefore = 1u << 2,
};

enum class InputEventKind : std::uint8_t {
  TouchFrame = 1,
};

enum class TouchPhase : std::uint8_t {
  Down = 1,
  Move = 2,
  Up = 3,
  Cancel = 4,
};

struct PacketHeader {
  std::uint16_t version = kProtocolVersion;
  MessageType type = MessageType::Error;
  std::uint32_t payload_length = 0;
  std::uint64_t sequence = 0;
  std::uint64_t timestamp_ns = 0;
  std::uint32_t flags = kFlagNone;
};

struct Packet {
  PacketHeader header;
  std::vector<std::uint8_t> payload;
};

struct TouchContact {
  std::uint16_t pointer_id = 0;
  TouchPhase phase = TouchPhase::Move;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t pressure = 0;
  std::uint16_t major = 0;
  std::uint16_t minor = 0;
};

struct TouchFrame {
  std::vector<TouchContact> contacts;
};

struct DiscoveryPacket {
  std::uint16_t control_port = 0;
  std::uint16_t video_port = 0;
  std::uint32_t request_nonce = 0;
};

struct PairChallenge {
  std::uint32_t iterations = kPairingPbkdf2Iterations;
  std::array<std::uint8_t, kPairingSaltSize> salt{};
  std::array<std::uint8_t, kPairingNonceSize> server_nonce{};
};

struct PairResponse {
  std::array<std::uint8_t, kPairingNonceSize> client_nonce{};
  std::array<std::uint8_t, kPairingProofSize> proof{};
};

struct PairResult {
  bool accepted = false;
  std::array<std::uint8_t, kPairingProofSize> host_proof{};
};

struct VideoChallenge {
  std::array<std::uint8_t, kPairingNonceSize> nonce{};
};

struct VideoAuth {
  std::array<std::uint8_t, kPairingProofSize> proof{};
};

inline std::uint64_t monotonic_now_ns() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

inline void write_u16_le(std::uint8_t* out, std::uint16_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xffu);
  out[1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

inline void write_u32_le(std::uint8_t* out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out[i] = static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu);
  }
}

inline void write_u64_le(std::uint8_t* out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out[i] = static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu);
  }
}

inline std::uint16_t read_u16_le(const std::uint8_t* in) {
  return static_cast<std::uint16_t>(in[0]) |
         static_cast<std::uint16_t>(in[1] << 8u);
}

inline std::uint32_t read_u32_le(const std::uint8_t* in) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(in[i]) << (8u * i);
  }
  return value;
}

inline std::uint64_t read_u64_le(const std::uint8_t* in) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(in[i]) << (8u * i);
  }
  return value;
}

inline std::array<std::uint8_t, kDiscoveryPacketSize> encode_discovery_request(
    std::uint32_t request_nonce) {
  std::array<std::uint8_t, kDiscoveryPacketSize> out{};
  std::copy(kDiscoveryRequestMagic.begin(), kDiscoveryRequestMagic.end(), out.begin());
  write_u16_le(out.data() + 8, kProtocolVersion);
  write_u32_le(out.data() + 16, request_nonce);
  return out;
}

inline std::array<std::uint8_t, kDiscoveryPacketSize> encode_discovery_response(
    std::uint16_t control_port,
    std::uint16_t video_port,
    std::uint32_t request_nonce) {
  std::array<std::uint8_t, kDiscoveryPacketSize> out{};
  std::copy(kDiscoveryResponseMagic.begin(), kDiscoveryResponseMagic.end(), out.begin());
  write_u16_le(out.data() + 8, kProtocolVersion);
  write_u16_le(out.data() + 12, control_port);
  write_u16_le(out.data() + 14, video_port);
  write_u32_le(out.data() + 16, request_nonce);
  return out;
}

inline DiscoveryPacket decode_discovery_packet(const std::uint8_t* data,
                                                std::size_t size,
                                                bool response) {
  const auto& expected = response ? kDiscoveryResponseMagic : kDiscoveryRequestMagic;
  if (size != kDiscoveryPacketSize ||
      !std::equal(expected.begin(), expected.end(), data) ||
      read_u16_le(data + 8) != kProtocolVersion ||
      read_u16_le(data + 10) != 0) {
    throw std::runtime_error("invalid discovery packet");
  }
  DiscoveryPacket packet;
  packet.control_port = read_u16_le(data + 12);
  packet.video_port = read_u16_le(data + 14);
  packet.request_nonce = read_u32_le(data + 16);
  if (!response && (packet.control_port != 0 || packet.video_port != 0)) {
    throw std::runtime_error("invalid discovery request ports");
  }
  if (response && (packet.control_port == 0 || packet.video_port == 0)) {
    throw std::runtime_error("invalid discovery response ports");
  }
  return packet;
}

inline std::vector<std::uint8_t> encode_pair_challenge(const PairChallenge& value) {
  std::vector<std::uint8_t> out(kPairChallengePayloadSize, 0);
  out[0] = kPairingPayloadVersion;
  write_u32_le(out.data() + 4, value.iterations);
  std::copy(value.salt.begin(), value.salt.end(), out.begin() + 8);
  std::copy(value.server_nonce.begin(), value.server_nonce.end(), out.begin() + 24);
  return out;
}

inline PairChallenge decode_pair_challenge(const std::vector<std::uint8_t>& payload) {
  if (payload.size() != kPairChallengePayloadSize ||
      payload[0] != kPairingPayloadVersion || payload[1] != 0 ||
      payload[2] != 0 || payload[3] != 0) {
    throw std::runtime_error("invalid pairing challenge");
  }
  PairChallenge value;
  value.iterations = read_u32_le(payload.data() + 4);
  if (value.iterations < 10'000 || value.iterations > 1'000'000) {
    throw std::runtime_error("invalid pairing iteration count");
  }
  std::copy_n(payload.begin() + 8, value.salt.size(), value.salt.begin());
  std::copy_n(payload.begin() + 24, value.server_nonce.size(), value.server_nonce.begin());
  return value;
}

inline std::vector<std::uint8_t> encode_pair_response(const PairResponse& value) {
  std::vector<std::uint8_t> out(kPairResponsePayloadSize, 0);
  out[0] = kPairingPayloadVersion;
  std::copy(value.client_nonce.begin(), value.client_nonce.end(), out.begin() + 4);
  std::copy(value.proof.begin(), value.proof.end(), out.begin() + 36);
  return out;
}

inline PairResponse decode_pair_response(const std::vector<std::uint8_t>& payload) {
  if (payload.size() != kPairResponsePayloadSize ||
      payload[0] != kPairingPayloadVersion || payload[1] != 0 ||
      payload[2] != 0 || payload[3] != 0) {
    throw std::runtime_error("invalid pairing response");
  }
  PairResponse value;
  std::copy_n(payload.begin() + 4, value.client_nonce.size(), value.client_nonce.begin());
  std::copy_n(payload.begin() + 36, value.proof.size(), value.proof.begin());
  return value;
}

inline std::vector<std::uint8_t> encode_pair_result(const PairResult& value) {
  std::vector<std::uint8_t> out(kPairResultPayloadSize, 0);
  out[0] = kPairingPayloadVersion;
  out[1] = value.accepted ? 1 : 0;
  std::copy(value.host_proof.begin(), value.host_proof.end(), out.begin() + 4);
  return out;
}

inline PairResult decode_pair_result(const std::vector<std::uint8_t>& payload) {
  if (payload.size() != kPairResultPayloadSize ||
      payload[0] != kPairingPayloadVersion || payload[1] > 1 ||
      payload[2] != 0 || payload[3] != 0) {
    throw std::runtime_error("invalid pairing result");
  }
  PairResult value;
  value.accepted = payload[1] == 1;
  std::copy_n(payload.begin() + 4, value.host_proof.size(), value.host_proof.begin());
  return value;
}

inline std::vector<std::uint8_t> encode_video_challenge(const VideoChallenge& value) {
  std::vector<std::uint8_t> out(kVideoChallengePayloadSize, 0);
  out[0] = kPairingPayloadVersion;
  std::copy(value.nonce.begin(), value.nonce.end(), out.begin() + 4);
  return out;
}

inline VideoChallenge decode_video_challenge(const std::vector<std::uint8_t>& payload) {
  if (payload.size() != kVideoChallengePayloadSize ||
      payload[0] != kPairingPayloadVersion || payload[1] != 0 ||
      payload[2] != 0 || payload[3] != 0) {
    throw std::runtime_error("invalid video challenge");
  }
  VideoChallenge value;
  std::copy_n(payload.begin() + 4, value.nonce.size(), value.nonce.begin());
  return value;
}

inline std::vector<std::uint8_t> encode_video_auth(const VideoAuth& value) {
  std::vector<std::uint8_t> out(kVideoAuthPayloadSize, 0);
  out[0] = kPairingPayloadVersion;
  std::copy(value.proof.begin(), value.proof.end(), out.begin() + 4);
  return out;
}

inline VideoAuth decode_video_auth(const std::vector<std::uint8_t>& payload) {
  if (payload.size() != kVideoAuthPayloadSize ||
      payload[0] != kPairingPayloadVersion || payload[1] != 0 ||
      payload[2] != 0 || payload[3] != 0) {
    throw std::runtime_error("invalid video authentication");
  }
  VideoAuth value;
  std::copy_n(payload.begin() + 4, value.proof.size(), value.proof.begin());
  return value;
}

inline bool is_valid_touch_phase(TouchPhase phase) {
  switch (phase) {
    case TouchPhase::Down:
    case TouchPhase::Move:
    case TouchPhase::Up:
    case TouchPhase::Cancel:
      return true;
  }
  return false;
}

inline std::vector<std::uint8_t> encode_touch_frame(const TouchFrame& frame) {
  if (frame.contacts.empty() || frame.contacts.size() > kMaxTouchContacts) {
    throw std::runtime_error("invalid touch contact count");
  }

  std::vector<std::uint8_t> out(
      kTouchFrameHeaderSize + frame.contacts.size() * kTouchContactSize, 0);
  out[0] = static_cast<std::uint8_t>(InputEventKind::TouchFrame);
  out[1] = kInputPayloadVersion;
  out[2] = static_cast<std::uint8_t>(frame.contacts.size());

  for (std::size_t i = 0; i < frame.contacts.size(); ++i) {
    const auto& contact = frame.contacts[i];
    if (contact.pointer_id == 0 || contact.pointer_id > kMaxTouchPointerId ||
        !is_valid_touch_phase(contact.phase) || contact.pressure > kMaxTouchPressure) {
      throw std::runtime_error("invalid touch contact");
    }
    for (std::size_t previous = 0; previous < i; ++previous) {
      if (frame.contacts[previous].pointer_id == contact.pointer_id) {
        throw std::runtime_error("duplicate touch pointer id");
      }
    }

    auto* encoded = out.data() + kTouchFrameHeaderSize + i * kTouchContactSize;
    write_u16_le(encoded, contact.pointer_id);
    encoded[2] = static_cast<std::uint8_t>(contact.phase);
    write_u16_le(encoded + 4, contact.x);
    write_u16_le(encoded + 6, contact.y);
    write_u16_le(encoded + 8, contact.pressure);
    write_u16_le(encoded + 10, contact.major);
    write_u16_le(encoded + 12, contact.minor);
  }
  return out;
}

inline TouchFrame decode_touch_frame(const std::vector<std::uint8_t>& payload) {
  if (payload.size() < kTouchFrameHeaderSize ||
      payload[0] != static_cast<std::uint8_t>(InputEventKind::TouchFrame) ||
      payload[1] != kInputPayloadVersion || payload[3] != 0) {
    throw std::runtime_error("invalid touch frame header");
  }

  const std::size_t count = payload[2];
  if (count == 0 || count > kMaxTouchContacts ||
      payload.size() != kTouchFrameHeaderSize + count * kTouchContactSize) {
    throw std::runtime_error("invalid touch frame length");
  }

  TouchFrame frame;
  frame.contacts.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto* encoded = payload.data() + kTouchFrameHeaderSize + i * kTouchContactSize;
    if (encoded[3] != 0) {
      throw std::runtime_error("invalid touch contact reserved byte");
    }

    TouchContact contact;
    contact.pointer_id = read_u16_le(encoded);
    contact.phase = static_cast<TouchPhase>(encoded[2]);
    contact.x = read_u16_le(encoded + 4);
    contact.y = read_u16_le(encoded + 6);
    contact.pressure = read_u16_le(encoded + 8);
    contact.major = read_u16_le(encoded + 10);
    contact.minor = read_u16_le(encoded + 12);
    if (contact.pointer_id == 0 || contact.pointer_id > kMaxTouchPointerId ||
        !is_valid_touch_phase(contact.phase) || contact.pressure > kMaxTouchPressure) {
      throw std::runtime_error("invalid touch contact");
    }
    for (const auto& previous : frame.contacts) {
      if (previous.pointer_id == contact.pointer_id) {
        throw std::runtime_error("duplicate touch pointer id");
      }
    }
    frame.contacts.push_back(contact);
  }
  return frame;
}

inline std::array<std::uint8_t, kHeaderSize> encode_header(const PacketHeader& header) {
  std::array<std::uint8_t, kHeaderSize> out{};
  std::memcpy(out.data(), kMagic.data(), kMagic.size());
  write_u16_le(out.data() + 4, header.version);
  write_u16_le(out.data() + 6, static_cast<std::uint16_t>(header.type));
  write_u32_le(out.data() + 8, header.payload_length);
  write_u64_le(out.data() + 12, header.sequence);
  write_u64_le(out.data() + 20, header.timestamp_ns);
  write_u32_le(out.data() + 28, header.flags);
  return out;
}

inline PacketHeader decode_header(const std::uint8_t* data, std::uint32_t max_payload) {
  if (std::memcmp(data, kMagic.data(), kMagic.size()) != 0) {
    throw std::runtime_error("invalid packet magic");
  }

  PacketHeader header;
  header.version = read_u16_le(data + 4);
  if (header.version != kProtocolVersion) {
    throw std::runtime_error("unsupported protocol version");
  }

  header.type = static_cast<MessageType>(read_u16_le(data + 6));
  header.payload_length = read_u32_le(data + 8);
  if (header.payload_length > max_payload) {
    throw std::runtime_error("payload length exceeds channel maximum");
  }

  header.sequence = read_u64_le(data + 12);
  header.timestamp_ns = read_u64_le(data + 20);
  header.flags = read_u32_le(data + 28);
  return header;
}

inline std::vector<std::uint8_t> encode_packet(PacketHeader header,
                                               const std::vector<std::uint8_t>& payload) {
  if (payload.size() > UINT32_MAX) {
    throw std::runtime_error("payload too large");
  }

  header.payload_length = static_cast<std::uint32_t>(payload.size());
  if (header.timestamp_ns == 0) {
    header.timestamp_ns = monotonic_now_ns();
  }

  auto encoded_header = encode_header(header);
  std::vector<std::uint8_t> out;
  out.reserve(encoded_header.size() + payload.size());
  out.insert(out.end(), encoded_header.begin(), encoded_header.end());
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

inline std::vector<std::uint8_t> bytes_from_string(const std::string& value) {
  return std::vector<std::uint8_t>(value.begin(), value.end());
}

class PacketReader {
 public:
  explicit PacketReader(std::uint32_t max_payload) : max_payload_(max_payload) {}

  void feed(const std::uint8_t* data, std::size_t size) {
    buffer_.insert(buffer_.end(), data, data + size);
  }

  [[nodiscard]] std::optional<Packet> next() {
    if (buffer_.size() < kHeaderSize) {
      return std::nullopt;
    }

    const PacketHeader header = decode_header(buffer_.data(), max_payload_);
    const std::size_t full_size = kHeaderSize + header.payload_length;
    if (buffer_.size() < full_size) {
      return std::nullopt;
    }

    Packet packet;
    packet.header = header;
    packet.payload.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                          buffer_.begin() + static_cast<std::ptrdiff_t>(full_size));
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(full_size));
    return packet;
  }

  [[nodiscard]] std::size_t buffered_bytes() const { return buffer_.size(); }

 private:
  std::uint32_t max_payload_;
  std::vector<std::uint8_t> buffer_;
};

}  // namespace fif
