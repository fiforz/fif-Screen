#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fif::host {

struct ScreenTarget {
  std::wstring device_name;
  std::wstring device_string;
  std::wstring device_id;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool primary = false;
};

struct RawFrame {
  int width = 0;
  int height = 0;
  const std::uint8_t* bgra = nullptr;
  int bgra_stride = 0;
  std::vector<std::uint8_t> rgb565;
};

std::optional<ScreenTarget> find_fifscreen_display(bool log_candidates = true);
std::string narrow(const std::wstring& value);
bool save_rgba_png(const std::wstring& path, const RawFrame& frame);

class TestOverlayWindow {
 public:
  TestOverlayWindow() = default;
  ~TestOverlayWindow();

  TestOverlayWindow(const TestOverlayWindow&) = delete;
  TestOverlayWindow& operator=(const TestOverlayWindow&) = delete;

  bool start(const ScreenTarget& target);
  void stop();

 private:
  void run(ScreenTarget target);

  std::thread thread_;
  std::atomic<HWND> hwnd_{nullptr};
  std::atomic<bool> running_{false};
};

class GdiScreenCapturer {
 public:
  GdiScreenCapturer(ScreenTarget target, int output_width, int output_height);
  ~GdiScreenCapturer();

  GdiScreenCapturer(const GdiScreenCapturer&) = delete;
  GdiScreenCapturer& operator=(const GdiScreenCapturer&) = delete;

  [[nodiscard]] const ScreenTarget& target() const { return target_; }
  [[nodiscard]] int output_width() const { return output_width_; }
  [[nodiscard]] int output_height() const { return output_height_; }
  bool capture(RawFrame& frame, bool generate_rgb565 = true) const;

 private:
  bool refresh_cursor_image(HCURSOR source_cursor) const;
  void draw_system_cursor() const;

  ScreenTarget target_;
  int output_width_;
  int output_height_;
  HDC screen_dc_ = nullptr;
  HDC memory_dc_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ old_bitmap_ = nullptr;
  void* bitmap_bits_ = nullptr;
  mutable HCURSOR cursor_source_ = nullptr;
  mutable HCURSOR cursor_copy_ = nullptr;
  mutable CURSORINFO last_cursor_info_{};
  mutable bool have_last_cursor_info_ = false;
  mutable unsigned int cursor_query_failures_ = 0;
  mutable int cursor_hotspot_x_ = 0;
  mutable int cursor_hotspot_y_ = 0;
  mutable int cursor_width_ = 0;
  mutable int cursor_height_ = 0;
};

}  // namespace fif::host
