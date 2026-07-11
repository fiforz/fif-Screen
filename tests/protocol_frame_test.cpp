#include <fif/protocol.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::filesystem::path find_repo_root() {
  auto path = std::filesystem::current_path();
  for (;;) {
    if (std::filesystem::exists(path / "protocol" / "test-vectors" / "test-vectors.json")) {
      return path;
    }
    if (!path.has_parent_path() || path == path.parent_path()) {
      throw std::runtime_error("repo root not found");
    }
    path = path.parent_path();
  }
}

std::vector<std::uint8_t> read_all_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open " + path.string());
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>());
}

void assert_vector(const std::string& file,
                   fif::MessageType type,
                   std::uint64_t sequence,
                   std::uint64_t timestamp_ns,
                   std::uint32_t flags,
                   const std::string& payload) {
  const auto bytes = read_all_bytes(find_repo_root() / "protocol" / "test-vectors" / file);
  fif::PacketReader reader(fif::kMaxVideoPayload);
  reader.feed(bytes.data(), bytes.size());
  auto packet = reader.next();
  assert(packet.has_value());
  assert(packet->header.type == type);
  assert(packet->header.sequence == sequence);
  assert(packet->header.timestamp_ns == timestamp_ns);
  assert(packet->header.flags == flags);
  assert(packet->payload.size() == payload.size());
  assert(std::string(packet->payload.begin(), packet->payload.end()) == payload);
}

void test_shared_vectors() {
  assert_vector("hello.bin", fif::MessageType::Hello, 1, 1000000000, 0,
                "{\"role\":\"android-client\",\"protocol\":1}");
  assert_vector("hello_ack.bin", fif::MessageType::HelloAck, 2, 1000001000, 0,
                "{\"role\":\"windows-host\",\"protocol\":1,\"controlPort\":27183,\"videoPort\":27184}");
  assert_vector("video_header.bin", fif::MessageType::VideoFrame, 3, 1000002000,
                fif::kFlagIdrFrame, "");
}

void test_partial_packet() {
  fif::PacketHeader header;
  header.type = fif::MessageType::Hello;
  header.sequence = 7;
  const auto encoded = fif::encode_packet(header, fif::bytes_from_string("{}"));

  fif::PacketReader reader(fif::kMaxControlPayload);
  reader.feed(encoded.data(), 10);
  assert(!reader.next().has_value());
  reader.feed(encoded.data() + 10, encoded.size() - 10);
  auto packet = reader.next();
  assert(packet.has_value());
  assert(packet->header.type == fif::MessageType::Hello);
  assert(packet->header.sequence == 7);
  assert(packet->payload.size() == 2);
}

void test_coalesced_packets() {
  fif::PacketHeader first;
  first.type = fif::MessageType::Ping;
  first.sequence = 1;
  auto a = fif::encode_packet(first, {});

  fif::PacketHeader second;
  second.type = fif::MessageType::Pong;
  second.sequence = 2;
  auto b = fif::encode_packet(second, {});

  std::vector<std::uint8_t> joined;
  joined.insert(joined.end(), a.begin(), a.end());
  joined.insert(joined.end(), b.begin(), b.end());

  fif::PacketReader reader(fif::kMaxControlPayload);
  reader.feed(joined.data(), joined.size());
  auto one = reader.next();
  auto two = reader.next();
  assert(one.has_value());
  assert(two.has_value());
  assert(one->header.type == fif::MessageType::Ping);
  assert(two->header.type == fif::MessageType::Pong);
}

void test_rejects_large_payload() {
  fif::PacketHeader header;
  header.type = fif::MessageType::Stats;
  header.payload_length = fif::kMaxControlPayload + 1;
  auto encoded_header = fif::encode_header(header);

  fif::PacketReader reader(fif::kMaxControlPayload);
  reader.feed(encoded_header.data(), encoded_header.size());

  bool rejected = false;
  try {
    (void)reader.next();
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
}

void test_touch_frame_round_trip() {
  fif::TouchFrame frame;
  frame.contacts.push_back(
      {1, fif::TouchPhase::Down, 0, 65535, 768, 2048, 1024});
  frame.contacts.push_back(
      {2, fif::TouchPhase::Move, 32768, 16384, 512, 0, 0});

  const auto encoded = fif::encode_touch_frame(frame);
  assert(encoded.size() == fif::kTouchFrameHeaderSize + 2 * fif::kTouchContactSize);
  const auto decoded = fif::decode_touch_frame(encoded);
  assert(decoded.contacts.size() == 2);
  assert(decoded.contacts[0].pointer_id == 1);
  assert(decoded.contacts[0].phase == fif::TouchPhase::Down);
  assert(decoded.contacts[0].y == 65535);
  assert(decoded.contacts[0].pressure == 768);
  assert(decoded.contacts[0].major == 2048);
  assert(decoded.contacts[1].pointer_id == 2);
  assert(decoded.contacts[1].phase == fif::TouchPhase::Move);
  assert(decoded.contacts[1].x == 32768);
}

void test_rejects_invalid_touch_frame() {
  fif::TouchFrame frame;
  frame.contacts.push_back(
      {1, fif::TouchPhase::Down, 100, 200, 512, 1000, 800});
  frame.contacts.push_back(
      {2, fif::TouchPhase::Move, 300, 400, 512, 1000, 800});
  auto encoded = fif::encode_touch_frame(frame);
  const std::size_t second_contact =
      fif::kTouchFrameHeaderSize + fif::kTouchContactSize;
  encoded[second_contact] = 1;
  encoded[second_contact + 1] = 0;

  bool rejected = false;
  try {
    (void)fif::decode_touch_frame(encoded);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  assert(rejected);
}

void test_lan_discovery_round_trip() {
  constexpr std::uint32_t nonce = 0x78563412;
  const auto request = fif::encode_discovery_request(nonce);
  const auto decoded_request = fif::decode_discovery_packet(
      request.data(), request.size(), false);
  assert(decoded_request.control_port == 0);
  assert(decoded_request.video_port == 0);
  assert(decoded_request.request_nonce == nonce);

  const auto response = fif::encode_discovery_response(27183, 27184, nonce);
  const auto decoded_response = fif::decode_discovery_packet(
      response.data(), response.size(), true);
  assert(decoded_response.control_port == 27183);
  assert(decoded_response.video_port == 27184);
  assert(decoded_response.request_nonce == nonce);
}

void test_pairing_payload_round_trip() {
  fif::PairChallenge challenge;
  challenge.iterations = 100000;
  for (std::size_t index = 0; index < challenge.salt.size(); ++index) {
    challenge.salt[index] = static_cast<std::uint8_t>(index);
  }
  for (std::size_t index = 0; index < challenge.server_nonce.size(); ++index) {
    challenge.server_nonce[index] = static_cast<std::uint8_t>(0x10 + index);
  }
  const auto decoded_challenge = fif::decode_pair_challenge(
      fif::encode_pair_challenge(challenge));
  assert(decoded_challenge.iterations == challenge.iterations);
  assert(decoded_challenge.salt == challenge.salt);
  assert(decoded_challenge.server_nonce == challenge.server_nonce);

  fif::PairResult result;
  result.accepted = true;
  result.host_proof.fill(0xa5);
  const auto decoded_result = fif::decode_pair_result(
      fif::encode_pair_result(result));
  assert(decoded_result.accepted);
  assert(decoded_result.host_proof == result.host_proof);
}

}  // namespace

int main() {
  test_shared_vectors();
  test_partial_packet();
  test_coalesced_packets();
  test_rejects_large_payload();
  test_touch_frame_round_trip();
  test_rejects_invalid_touch_frame();
  test_lan_discovery_round_trip();
  test_pairing_payload_round_trip();
  std::cout << "protocol tests passed\n";
  return 0;
}
