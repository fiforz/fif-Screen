#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <fif/protocol.hpp>

#include "screen_capture.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace fif::host {

struct TouchContactState {
  POINT point{};
  RECT area{};
  UINT32 pressure = 1;
};

class TouchInjector {
 public:
  TouchInjector() = default;
  ~TouchInjector();

  TouchInjector(const TouchInjector&) = delete;
  TouchInjector& operator=(const TouchInjector&) = delete;

  bool inject(const TouchFrame& frame, const ScreenTarget& target);
  void cancel_active();
 [[nodiscard]] std::size_t active_contact_count() const { return active_.size(); }

 private:
  bool ensure_initialized();
  bool inject_contacts(const POINTER_TOUCH_INFO* contacts, std::size_t count);

  bool initialized_ = false;
  bool initialization_failed_ = false;
  std::unordered_map<std::uint16_t, TouchContactState> active_;
};

}  // namespace fif::host
