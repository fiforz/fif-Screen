#include "touch_injector.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fif::host {

namespace {

LONG map_axis(std::uint16_t normalized, int origin, int extent) {
  if (extent <= 1) {
    return origin;
  }
  const double position = static_cast<double>(normalized) * (extent - 1) / 65535.0;
  return static_cast<LONG>(origin + std::lround(position));
}

LONG clamp_to_virtual_x(LONG value) {
  const LONG left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const LONG width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  return std::clamp(value, left, left + std::max<LONG>(1, width) - 1);
}

LONG clamp_to_virtual_y(LONG value) {
  const LONG top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const LONG height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  return std::clamp(value, top, top + std::max<LONG>(1, height) - 1);
}

TouchContactState make_contact_state(const TouchContact& contact,
                                     const ScreenTarget& target) {
  TouchContactState state;
  state.point.x = clamp_to_virtual_x(map_axis(contact.x, target.x, target.width));
  state.point.y = clamp_to_virtual_y(map_axis(contact.y, target.y, target.height));

  const int diameter_x = contact.major == 0
                             ? 8
                             : std::max(1, static_cast<int>(std::lround(
                                               static_cast<double>(contact.major) *
                                               target.width / 65535.0)));
  const int diameter_y = contact.minor == 0
                             ? 8
                             : std::max(1, static_cast<int>(std::lround(
                                               static_cast<double>(contact.minor) *
                                               target.height / 65535.0)));
  const LONG half_x = std::clamp<LONG>(diameter_x / 2, 2, 64);
  const LONG half_y = std::clamp<LONG>(diameter_y / 2, 2, 64);
  state.area.left = clamp_to_virtual_x(state.point.x - half_x);
  state.area.right = clamp_to_virtual_x(state.point.x + half_x);
  state.area.top = clamp_to_virtual_y(state.point.y - half_y);
  state.area.bottom = clamp_to_virtual_y(state.point.y + half_y);
  state.pressure = std::clamp<UINT32>(contact.pressure, 1, 1024);
  return state;
}

POINTER_TOUCH_INFO make_touch_info(std::uint16_t pointer_id,
                                   const TouchContactState& state,
                                   POINTER_FLAGS flags) {
  POINTER_TOUCH_INFO info{};
  info.pointerInfo.pointerType = PT_TOUCH;
  info.pointerInfo.pointerId = pointer_id - 1;
  info.pointerInfo.pointerFlags = flags;
  info.pointerInfo.ptPixelLocation = state.point;
  info.touchFlags = TOUCH_FLAG_NONE;
  info.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
  info.rcContact = state.area;
  info.orientation = 90;
  info.pressure = state.pressure;
  return info;
}

bool is_transition(TouchPhase phase) {
  return phase == TouchPhase::Down || phase == TouchPhase::Up ||
         phase == TouchPhase::Cancel;
}

}  // namespace

TouchInjector::~TouchInjector() {
  cancel_active();
}

bool TouchInjector::ensure_initialized() {
  if (initialized_) {
    return true;
  }
  if (initialization_failed_) {
    return false;
  }
  if (!InitializeTouchInjection(static_cast<UINT32>(kMaxTouchContacts),
                                TOUCH_FEEDBACK_DEFAULT)) {
    initialization_failed_ = true;
    std::cerr << "FIFSCREEN_HOST event=touch_initialize_failed error="
              << GetLastError() << "\n";
    return false;
  }
  initialized_ = true;
  std::cout << "FIFSCREEN_HOST event=touch_initialized max_contacts="
            << kMaxTouchContacts << "\n";
  return true;
}

bool TouchInjector::inject_contacts(const POINTER_TOUCH_INFO* contacts,
                                    std::size_t count) {
  if (count == 0) {
    return true;
  }
  if (InjectTouchInput(static_cast<UINT32>(count), contacts)) {
    return true;
  }
  DWORD error = GetLastError();
  if (error == ERROR_NOT_READY) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (InjectTouchInput(static_cast<UINT32>(count), contacts)) {
      return true;
    }
    error = GetLastError();
  }
  std::cerr << "FIFSCREEN_HOST event=touch_inject_failed error=" << error
            << " contacts=" << count << "\n";
  return false;
}

bool TouchInjector::inject(const TouchFrame& frame, const ScreenTarget& target) {
  if (!ensure_initialized() || target.width <= 0 || target.height <= 0) {
    return false;
  }

  std::unordered_set<std::uint16_t> incoming_ids;
  for (const auto& contact : frame.contacts) {
    incoming_ids.insert(contact.pointer_id);
  }
  for (const auto& [pointer_id, state] : active_) {
    static_cast<void>(state);
    if (!incoming_ids.contains(pointer_id)) {
      std::cerr << "FIFSCREEN_HOST event=touch_sequence_invalid reason=missing_active_pointer id="
                << pointer_id << "\n";
      cancel_active();
      return false;
    }
  }

  auto next_active = active_;
  std::vector<POINTER_TOUCH_INFO> contacts;
  contacts.reserve(frame.contacts.size());
  bool has_transition = false;
  for (const auto& contact : frame.contacts) {
    has_transition = has_transition || is_transition(contact.phase);
    const auto active = active_.find(contact.pointer_id);
    if (contact.phase == TouchPhase::Down) {
      if (active != active_.end()) {
        std::cerr << "FIFSCREEN_HOST event=touch_sequence_invalid reason=duplicate_down id="
                  << contact.pointer_id << "\n";
        cancel_active();
        return false;
      }
      const auto state = make_contact_state(contact, target);
      contacts.push_back(make_touch_info(
          contact.pointer_id, state,
          POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT));
      next_active[contact.pointer_id] = state;
      continue;
    }

    if (active == active_.end()) {
      std::cerr << "FIFSCREEN_HOST event=touch_sequence_invalid reason=unknown_pointer id="
                << contact.pointer_id << "\n";
      cancel_active();
      return false;
    }
    if (contact.phase == TouchPhase::Move) {
      const auto state = make_contact_state(contact, target);
      contacts.push_back(make_touch_info(
          contact.pointer_id, state,
          POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT));
      next_active[contact.pointer_id] = state;
      continue;
    }

    POINTER_FLAGS flags = POINTER_FLAG_UP;
    if (contact.phase == TouchPhase::Cancel) {
      flags |= POINTER_FLAG_CANCELED;
    }
    contacts.push_back(make_touch_info(contact.pointer_id, active->second, flags));
    next_active.erase(contact.pointer_id);
  }

  if (!inject_contacts(contacts.data(), contacts.size())) {
    active_.clear();
    return false;
  }
  active_ = std::move(next_active);
  if (has_transition) {
    std::cout << "FIFSCREEN_HOST event=touch_frame contacts=" << contacts.size()
              << " active=" << active_.size() << "\n";
  }
  return true;
}

void TouchInjector::cancel_active() {
  if (!initialized_ || active_.empty()) {
    active_.clear();
    return;
  }
  std::vector<POINTER_TOUCH_INFO> contacts;
  contacts.reserve(active_.size());
  for (const auto& [pointer_id, state] : active_) {
    contacts.push_back(make_touch_info(
        pointer_id, state, POINTER_FLAG_UP | POINTER_FLAG_CANCELED));
  }
  (void)inject_contacts(contacts.data(), contacts.size());
  active_.clear();
  std::cout << "FIFSCREEN_HOST event=touch_cancelled\n";
}

}  // namespace fif::host
