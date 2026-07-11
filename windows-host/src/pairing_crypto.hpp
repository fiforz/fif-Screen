#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include <fif/protocol.hpp>

namespace fif::host {

using PairingKey = std::array<std::uint8_t, fif::kPairingProofSize>;

struct PairingMaterial {
  PairingKey control_proof{};
  PairingKey session_key{};
};

[[nodiscard]] bool is_valid_pairing_pin(std::string_view pin);
[[nodiscard]] PairingMaterial derive_pairing_material(
    std::string_view pin,
    const fif::PairChallenge& challenge,
    const std::array<std::uint8_t, fif::kPairingNonceSize>& client_nonce);
[[nodiscard]] PairingKey make_host_proof(const PairingKey& session_key);
[[nodiscard]] PairingKey make_video_proof(
    const PairingKey& session_key,
    const std::array<std::uint8_t, fif::kPairingNonceSize>& video_nonce);
[[nodiscard]] bool secure_equal(std::span<const std::uint8_t> left,
                                std::span<const std::uint8_t> right);
void fill_secure_random(std::span<std::uint8_t> output);

}  // namespace fif::host
