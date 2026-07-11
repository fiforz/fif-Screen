#include "pairing_crypto.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

template <std::size_t Size>
std::array<std::uint8_t, Size> from_hex(std::string_view hex) {
  if (hex.size() != Size * 2) {
    throw std::runtime_error("invalid hex length");
  }
  auto nibble = [](char value) -> std::uint8_t {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    throw std::runtime_error("invalid hex character");
  };
  std::array<std::uint8_t, Size> output{};
  for (std::size_t index = 0; index < Size; ++index) {
    output[index] = static_cast<std::uint8_t>(
        (nibble(hex[index * 2]) << 4) | nibble(hex[index * 2 + 1]));
  }
  return output;
}

void test_pairing_vector() {
  fif::PairChallenge challenge;
  for (std::size_t index = 0; index < challenge.salt.size(); ++index) {
    challenge.salt[index] = static_cast<std::uint8_t>(index);
  }
  for (std::size_t index = 0; index < challenge.server_nonce.size(); ++index) {
    challenge.server_nonce[index] = static_cast<std::uint8_t>(0x10 + index);
  }
  std::array<std::uint8_t, fif::kPairingNonceSize> client_nonce{};
  std::array<std::uint8_t, fif::kPairingNonceSize> video_nonce{};
  for (std::size_t index = 0; index < client_nonce.size(); ++index) {
    client_nonce[index] = static_cast<std::uint8_t>(0x30 + index);
    video_nonce[index] = static_cast<std::uint8_t>(0x50 + index);
  }

  const auto material = fif::host::derive_pairing_material(
      "1234", challenge, client_nonce);
  assert(material.control_proof == from_hex<32>(
      "39f519ba5dddb0ff94c3e5513495e904efe1c0d1c80e76935bf83382a568fdcd"));
  assert(material.session_key == from_hex<32>(
      "03c0519ce3121b0f74123b391f8ea86b53660e02e5ff9fdc0fbad3dccc84be1e"));
  assert(fif::host::make_host_proof(material.session_key) == from_hex<32>(
      "6cc14c8f91464da895e2f1cead6a32de250db7a638a8c1b56e023085a4a6e9f6"));
  assert(fif::host::make_video_proof(material.session_key, video_nonce) == from_hex<32>(
      "58f43d30ce583edeabd3115564e5483c49ba0c9ef9d11efbeac76c8a31339506"));
}

void test_pin_validation() {
  assert(fif::host::is_valid_pairing_pin("0000"));
  assert(fif::host::is_valid_pairing_pin("9876"));
  assert(!fif::host::is_valid_pairing_pin("123"));
  assert(!fif::host::is_valid_pairing_pin("12a4"));
  assert(!fif::host::is_valid_pairing_pin("12345"));
}

}  // namespace

int main() {
  test_pairing_vector();
  test_pin_validation();
  std::cout << "pairing crypto tests passed\n";
  return 0;
}
