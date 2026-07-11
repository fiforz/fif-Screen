#include "pairing_crypto.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace fif::host {

namespace {

constexpr std::string_view kControlLabel = "FifScreen/control/v1";
constexpr std::string_view kSessionLabel = "FifScreen/session/v1";
constexpr std::string_view kAcceptedLabel = "FifScreen/accepted/v1";
constexpr std::string_view kVideoLabel = "FifScreen/video/v1";

class AlgorithmHandle {
 public:
  explicit AlgorithmHandle(ULONG flags) {
    const NTSTATUS status = BCryptOpenAlgorithmProvider(
        &handle_, BCRYPT_SHA256_ALGORITHM, nullptr, flags);
    if (status < 0) {
      throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }
  }

  ~AlgorithmHandle() {
    if (handle_) {
      BCryptCloseAlgorithmProvider(handle_, 0);
    }
  }

  AlgorithmHandle(const AlgorithmHandle&) = delete;
  AlgorithmHandle& operator=(const AlgorithmHandle&) = delete;

  [[nodiscard]] BCRYPT_ALG_HANDLE get() const { return handle_; }

 private:
  BCRYPT_ALG_HANDLE handle_ = nullptr;
};

PairingKey derive_pin_key(std::string_view pin,
                          const fif::PairChallenge& challenge) {
  AlgorithmHandle algorithm(BCRYPT_ALG_HANDLE_HMAC_FLAG);
  PairingKey key{};
  const NTSTATUS status = BCryptDeriveKeyPBKDF2(
      algorithm.get(),
      reinterpret_cast<PUCHAR>(const_cast<char*>(pin.data())),
      static_cast<ULONG>(pin.size()),
      const_cast<PUCHAR>(challenge.salt.data()),
      static_cast<ULONG>(challenge.salt.size()),
      challenge.iterations,
      key.data(),
      static_cast<ULONG>(key.size()),
      0);
  if (status < 0) {
    throw std::runtime_error("BCryptDeriveKeyPBKDF2 failed");
  }
  return key;
}

PairingKey hmac_sha256(const PairingKey& key,
                       std::span<const std::uint8_t> message) {
  AlgorithmHandle algorithm(BCRYPT_ALG_HANDLE_HMAC_FLAG);
  DWORD object_length = 0;
  DWORD copied = 0;
  if (BCryptGetProperty(
          algorithm.get(), BCRYPT_OBJECT_LENGTH,
          reinterpret_cast<PUCHAR>(&object_length), sizeof(object_length),
          &copied, 0) < 0) {
    throw std::runtime_error("BCryptGetProperty failed");
  }

  std::vector<std::uint8_t> hash_object(object_length);
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptCreateHash(
          algorithm.get(), &hash, hash_object.data(), object_length,
          const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) < 0) {
    throw std::runtime_error("BCryptCreateHash failed");
  }

  PairingKey output{};
  const NTSTATUS hash_status = BCryptHashData(
      hash, const_cast<PUCHAR>(message.data()),
      static_cast<ULONG>(message.size()), 0);
  const NTSTATUS finish_status = hash_status < 0
      ? hash_status
      : BCryptFinishHash(hash, output.data(), static_cast<ULONG>(output.size()), 0);
  BCryptDestroyHash(hash);
  SecureZeroMemory(hash_object.data(), hash_object.size());
  if (finish_status < 0) {
    throw std::runtime_error("HMAC-SHA256 failed");
  }
  return output;
}

std::vector<std::uint8_t> labeled_message(
    std::string_view label,
    std::span<const std::uint8_t> first = {},
    std::span<const std::uint8_t> second = {}) {
  std::vector<std::uint8_t> message;
  message.reserve(label.size() + first.size() + second.size());
  message.insert(message.end(), label.begin(), label.end());
  message.insert(message.end(), first.begin(), first.end());
  message.insert(message.end(), second.begin(), second.end());
  return message;
}

}  // namespace

bool is_valid_pairing_pin(std::string_view pin) {
  return pin.size() == 4 &&
         std::all_of(pin.begin(), pin.end(), [](char value) {
           return value >= '0' && value <= '9';
         });
}

PairingMaterial derive_pairing_material(
    std::string_view pin,
    const fif::PairChallenge& challenge,
    const std::array<std::uint8_t, fif::kPairingNonceSize>& client_nonce) {
  if (!is_valid_pairing_pin(pin)) {
    throw std::runtime_error("invalid pairing PIN");
  }
  PairingKey pin_key = derive_pin_key(pin, challenge);
  PairingMaterial material;
  const auto control_message = labeled_message(
      kControlLabel, challenge.server_nonce, client_nonce);
  const auto session_message = labeled_message(
      kSessionLabel, challenge.server_nonce, client_nonce);
  material.control_proof = hmac_sha256(pin_key, control_message);
  material.session_key = hmac_sha256(pin_key, session_message);
  SecureZeroMemory(pin_key.data(), pin_key.size());
  return material;
}

PairingKey make_host_proof(const PairingKey& session_key) {
  return hmac_sha256(session_key, labeled_message(kAcceptedLabel));
}

PairingKey make_video_proof(
    const PairingKey& session_key,
    const std::array<std::uint8_t, fif::kPairingNonceSize>& video_nonce) {
  return hmac_sha256(session_key, labeled_message(kVideoLabel, video_nonce));
}

bool secure_equal(std::span<const std::uint8_t> left,
                  std::span<const std::uint8_t> right) {
  if (left.size() != right.size()) {
    return false;
  }
  std::uint8_t difference = 0;
  for (std::size_t index = 0; index < left.size(); ++index) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}

void fill_secure_random(std::span<std::uint8_t> output) {
  if (output.empty()) {
    return;
  }
  if (BCryptGenRandom(
          nullptr, output.data(), static_cast<ULONG>(output.size()),
          BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    throw std::runtime_error("BCryptGenRandom failed");
  }
}

}  // namespace fif::host
