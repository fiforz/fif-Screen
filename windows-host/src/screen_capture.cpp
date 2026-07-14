#include "screen_capture.hpp"
#include "cursor_position.hpp"
#include "cursor_software.hpp"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <utility>

namespace fif::host {

namespace {

constexpr DWORD kDisplayActive = 0x00000001;
constexpr DWORD kDisplayPrimary = 0x00000004;
constexpr unsigned int kCursorQueryGraceFrames = 3;

enum class CursorDrawStatus {
  kDrawn,
  kQueryFailed,
  kHidden,
  kPositionFailed,
  kOutsideTarget,
  kDrawFailed,
  kDrawnTargetLocalFallback,
  kDrawnSoftwareCursor,
};

const char* cursor_draw_status_name(CursorDrawStatus status) {
  switch (status) {
    case CursorDrawStatus::kDrawn:
      return "drawn";
    case CursorDrawStatus::kQueryFailed:
      return "query_failed";
    case CursorDrawStatus::kHidden:
      return "hidden_or_suppressed";
    case CursorDrawStatus::kPositionFailed:
      return "physical_position_failed";
    case CursorDrawStatus::kOutsideTarget:
      return "outside_target";
    case CursorDrawStatus::kDrawFailed:
      return "draw_failed";
    case CursorDrawStatus::kDrawnTargetLocalFallback:
      return "drawn_target_local_fallback";
    case CursorDrawStatus::kDrawnSoftwareCursor:
      return "drawn_software_cursor";
  }
  return "unknown";
}

bool contains_case_insensitive(const std::wstring& haystack, const std::wstring& needle) {
  std::wstring lower_haystack = haystack;
  std::wstring lower_needle = needle;
  std::transform(lower_haystack.begin(), lower_haystack.end(), lower_haystack.begin(), towlower);
  std::transform(lower_needle.begin(), lower_needle.end(), lower_needle.begin(), towlower);
  return lower_haystack.find(lower_needle) != std::wstring::npos;
}

bool is_fifscreen_adapter_path(const std::wstring& path) {
  return contains_case_insensitive(path, L"FifScreenIdd") ||
         contains_case_insensitive(path, L"FifIddDriver");
}

std::wstring displayconfig_adapter_path(const LUID& adapter_id) {
  DISPLAYCONFIG_ADAPTER_NAME adapter{};
  adapter.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
  adapter.header.size = sizeof(adapter);
  adapter.header.adapterId = adapter_id;
  if (DisplayConfigGetDeviceInfo(&adapter.header) != ERROR_SUCCESS) {
    return L"";
  }
  return adapter.adapterDevicePath;
}

std::optional<std::wstring> find_fifscreen_displayconfig_source(bool log_candidates) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    LONG result = GetDisplayConfigBufferSizes(
        QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count);
    if (result != ERROR_SUCCESS) {
      return std::nullopt;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(),
                                &mode_count, modes.data(), nullptr);
    if (result == ERROR_INSUFFICIENT_BUFFER) {
      continue;
    }
    if (result != ERROR_SUCCESS) {
      return std::nullopt;
    }

    for (UINT32 index = 0; index < path_count; ++index) {
      const auto& path = paths[index];
      const std::wstring target_adapter =
          displayconfig_adapter_path(path.targetInfo.adapterId);
      if (!is_fifscreen_adapter_path(target_adapter)) {
        continue;
      }

      DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
      source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      source.header.size = sizeof(source);
      source.header.adapterId = path.sourceInfo.adapterId;
      source.header.id = path.sourceInfo.id;
      if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) {
        continue;
      }

      const std::wstring source_adapter =
          displayconfig_adapter_path(path.sourceInfo.adapterId);
      const bool extended = (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0 &&
                            is_fifscreen_adapter_path(source_adapter) &&
                            source.viewGdiDeviceName[0] != L'\0';
      if (log_candidates) {
        std::cout << "displayconfig FifScreen target source="
                  << narrow(source.viewGdiDeviceName)
                  << " source_adapter=" << narrow(source_adapter)
                  << " target_adapter=" << narrow(target_adapter)
                  << " extended=" << (extended ? "true" : "false") << "\n";
      }
      if (extended) {
        return source.viewGdiDeviceName;
      }
    }
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<ScreenTarget> display_from_device(const DISPLAY_DEVICEW& device) {
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (!EnumDisplaySettingsExW(device.DeviceName, ENUM_CURRENT_SETTINGS, &mode, 0)) {
    return std::nullopt;
  }

  ScreenTarget target;
  target.device_name = device.DeviceName;
  target.device_string = device.DeviceString;
  target.device_id = device.DeviceID;
  target.x = mode.dmPosition.x;
  target.y = mode.dmPosition.y;
  target.width = static_cast<int>(mode.dmPelsWidth);
  target.height = static_cast<int>(mode.dmPelsHeight);
  target.primary = (device.StateFlags & kDisplayPrimary) != 0;
  if (target.width <= 0 || target.height <= 0) {
    return std::nullopt;
  }
  return target;
}

int get_png_encoder_clsid(CLSID* clsid) {
  UINT count = 0;
  UINT bytes = 0;
  Gdiplus::GetImageEncodersSize(&count, &bytes);
  if (bytes == 0) {
    return -1;
  }

  std::vector<std::uint8_t> storage(bytes);
  auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
  Gdiplus::GetImageEncoders(count, bytes, encoders);
  for (UINT i = 0; i < count; ++i) {
    if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
      *clsid = encoders[i].Clsid;
      return static_cast<int>(i);
    }
  }
  return -1;
}

LRESULT CALLBACK overlay_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      SetTimer(hwnd, 1, 100, nullptr);
      return 0;
    case WM_TIMER:
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(hwnd, &paint);
      RECT rect{};
      GetClientRect(hwnd, &rect);

      const HBRUSH background = CreateSolidBrush(RGB(16, 22, 31));
      FillRect(dc, &rect, background);
      DeleteObject(background);

      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(246, 248, 250));

      HFONT title_font = CreateFontW(52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, L"Segoe UI");
      HFONT body_font = CreateFontW(34, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH, L"Consolas");

      HFONT old_font = static_cast<HFONT>(SelectObject(dc, title_font));
      TextOutW(dc, 34, 30, L"FIFSCREEN USB MVP", 18);

      SYSTEMTIME time{};
      GetLocalTime(&time);
      static int counter = 0;
      ++counter;

      std::wostringstream line;
      line << L"FRAME TEST " << counter << L"   "
           << time.wHour << L":" << time.wMinute << L":" << time.wSecond << L"." << time.wMilliseconds;

      SelectObject(dc, body_font);
      const std::wstring text = line.str();
      TextOutW(dc, 38, 105, text.c_str(), static_cast<int>(text.size()));

      const int width = rect.right - rect.left;
      const int x = 40 + ((counter * 17) % std::max(1, width - 180));
      const HBRUSH accent = CreateSolidBrush(RGB(0, 196, 140));
      RECT moving{x, 175, x + 120, 245};
      FillRect(dc, &moving, accent);
      DeleteObject(accent);

      SelectObject(dc, old_font);
      DeleteObject(title_font);
      DeleteObject(body_font);
      EndPaint(hwnd, &paint);
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, message, wparam, lparam);
  }
}

}  // namespace

std::string narrow(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                        nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      out.data(), bytes, nullptr, nullptr);
  return out;
}

std::optional<ScreenTarget> find_fifscreen_display(bool log_candidates) {
  const auto displayconfig_source =
      find_fifscreen_displayconfig_source(log_candidates);
  std::vector<ScreenTarget> active;
  for (DWORD i = 0; i < 16; ++i) {
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    if (!EnumDisplayDevicesW(nullptr, i, &device, 0)) {
      break;
    }
    if ((device.StateFlags & kDisplayActive) == 0) {
      continue;
    }
    if (auto target = display_from_device(device)) {
      if (log_candidates) {
        std::cout << "display candidate name=" << narrow(target->device_name)
                  << " string=" << narrow(target->device_string)
                  << " id=" << narrow(target->device_id)
                  << " pos=" << target->x << "," << target->y
                  << " size=" << target->width << "x" << target->height
                  << " primary=" << (target->primary ? "true" : "false") << "\n";
      }
      active.push_back(*target);
    }
  }

  auto fifscreen = std::find_if(active.begin(), active.end(), [](const ScreenTarget& target) {
    return contains_case_insensitive(target.device_string, L"FifScreen") ||
           contains_case_insensitive(target.device_id, L"FIFSCREEN") ||
           contains_case_insensitive(target.device_name, L"FifScreen");
  });
  if (fifscreen != active.end()) {
    return *fifscreen;
  }

  if (displayconfig_source) {
    auto displayconfig_match = std::find_if(
        active.begin(), active.end(), [&](const ScreenTarget& target) {
          return _wcsicmp(target.device_name.c_str(), displayconfig_source->c_str()) == 0;
        });
    if (displayconfig_match != active.end()) {
      std::cout << "FifScreen display resolved from DisplayConfig source "
                << narrow(*displayconfig_source) << "\n";
      return *displayconfig_match;
    }
  }

  const char* fallback = std::getenv("FIF_ALLOW_SECONDARY_FALLBACK");
  if (fallback && (std::string(fallback) == "1" || std::string(fallback) == "true")) {
    auto secondary = std::find_if(active.begin(), active.end(), [](const ScreenTarget& target) {
      return !target.primary;
    });
    if (secondary != active.end()) {
      std::cout << "FifScreen display name not exposed; using first non-primary display\n";
      return *secondary;
    }
  }

  return std::nullopt;
}

bool save_rgba_png(const std::wstring& path, const RawFrame& frame) {
  if (!frame.bgra || frame.width <= 0 || frame.height <= 0 || frame.bgra_stride <= 0) {
    return false;
  }

  std::filesystem::create_directories(std::filesystem::path(path).parent_path());

  Gdiplus::GdiplusStartupInput startup_input;
  ULONG_PTR token = 0;
  if (Gdiplus::GdiplusStartup(&token, &startup_input, nullptr) != Gdiplus::Ok) {
    return false;
  }

  CLSID png_clsid{};
  const bool have_encoder = get_png_encoder_clsid(&png_clsid) >= 0;
  Gdiplus::Bitmap bitmap(frame.width, frame.height, frame.bgra_stride,
                         PixelFormat32bppARGB, const_cast<BYTE*>(frame.bgra));
  const bool ok = have_encoder && bitmap.Save(path.c_str(), &png_clsid, nullptr) == Gdiplus::Ok;
  Gdiplus::GdiplusShutdown(token);
  return ok;
}

TestOverlayWindow::~TestOverlayWindow() {
  stop();
}

bool TestOverlayWindow::start(const ScreenTarget& target) {
  if (running_.exchange(true)) {
    return true;
  }
  thread_ = std::thread([this, target] { run(target); });
  for (int i = 0; i < 50; ++i) {
    if (hwnd_.load() != nullptr) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return hwnd_.load() != nullptr;
}

void TestOverlayWindow::stop() {
  running_ = false;
  if (HWND hwnd = hwnd_.load()) {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  hwnd_ = nullptr;
}

void TestOverlayWindow::run(ScreenTarget target) {
  const wchar_t* class_name = L"FifScreenUsbMvpOverlay";
  WNDCLASSW wc{};
  wc.lpfnWndProc = overlay_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = class_name;
  RegisterClassW(&wc);

  const int window_width = std::min(920, std::max(520, target.width - 120));
  const int window_height = 310;
  const int x = target.x + std::max(20, (target.width - window_width) / 2);
  const int y = target.y + std::max(20, (target.height - window_height) / 3);

  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, class_name,
                              L"FIFSCREEN USB MVP", WS_POPUP | WS_VISIBLE,
                              x, y, window_width, window_height,
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  hwnd_ = hwnd;
  if (!hwnd) {
    running_ = false;
    return;
  }

  ShowWindow(hwnd, SW_SHOWNA);
  UpdateWindow(hwnd);

  MSG msg{};
  while (running_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  hwnd_ = nullptr;
}

GdiScreenCapturer::GdiScreenCapturer(ScreenTarget target, int output_width, int output_height)
    : target_(std::move(target)), output_width_(output_width), output_height_(output_height) {
  screen_dc_ = CreateDCW(L"DISPLAY", target_.device_name.c_str(), nullptr, nullptr);
  if (!screen_dc_) {
    return;
  }

  memory_dc_ = CreateCompatibleDC(screen_dc_);
  if (!memory_dc_) {
    return;
  }

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = output_width_;
  info.bmiHeader.biHeight = -output_height_;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  bitmap_ = CreateDIBSection(screen_dc_, &info, DIB_RGB_COLORS,
                             &bitmap_bits_, nullptr, 0);
  if (!bitmap_ || !bitmap_bits_) {
    return;
  }
  old_bitmap_ = SelectObject(memory_dc_, bitmap_);
}

GdiScreenCapturer::~GdiScreenCapturer() {
  if (memory_dc_ && old_bitmap_ && old_bitmap_ != HGDI_ERROR) {
    SelectObject(memory_dc_, old_bitmap_);
  }
  if (bitmap_) {
    DeleteObject(bitmap_);
  }
  if (memory_dc_) {
    DeleteDC(memory_dc_);
  }
  if (screen_dc_) {
    DeleteDC(screen_dc_);
  }
}

void GdiScreenCapturer::draw_system_cursor() const {
  CURSORINFO cursor_info{};
  cursor_info.cbSize = sizeof(cursor_info);
  POINT physical_pos{};
  DWORD cursor_error = ERROR_SUCCESS;
  auto log_status = [&](CursorDrawStatus status) {
    const int value = static_cast<int>(status);
    if (last_cursor_draw_status_ == value) {
      return;
    }
    last_cursor_draw_status_ = value;
    std::cout << "FIFSCREEN_HOST event=cursor_state state="
              << cursor_draw_status_name(status)
              << " flags=" << cursor_info.flags
              << " logical_pos=" << cursor_info.ptScreenPos.x << ","
              << cursor_info.ptScreenPos.y
              << " physical_pos=" << physical_pos.x << "," << physical_pos.y
              << " target=" << target_.x << "," << target_.y << ","
              << target_.width << "x" << target_.height;
    if (cursor_error != ERROR_SUCCESS) {
      std::cout << " error=" << cursor_error;
    }
    std::cout << "\n";
  };

  if (GetCursorInfo(&cursor_info)) {
    last_cursor_info_ = cursor_info;
    have_last_cursor_info_ = true;
    cursor_query_failures_ = 0;
  } else {
    if (!have_last_cursor_info_ ||
        cursor_query_failures_ >= kCursorQueryGraceFrames) {
      cursor_error = GetLastError();
      log_status(CursorDrawStatus::kQueryFailed);
      return;
    }
    ++cursor_query_failures_;
    cursor_info = last_cursor_info_;
  }

  if ((cursor_info.flags & CURSOR_SHOWING) == 0 ||
      cursor_info.hCursor == nullptr) {
    log_status(CursorDrawStatus::kHidden);
    return;
  }

  std::optional<CursorCoordinate> physical;
  if (GetPhysicalCursorPos(&physical_pos)) {
    physical = CursorCoordinate{physical_pos.x, physical_pos.y};
  } else {
    cursor_error = GetLastError();
  }
  const auto resolved = resolve_cursor_position(
      CursorCoordinate{cursor_info.ptScreenPos.x, cursor_info.ptScreenPos.y},
      physical, cursor_error,
      CursorTargetBounds{target_.x, target_.y, target_.width, target_.height});
  if (!resolved) {
    log_status(CursorDrawStatus::kPositionFailed);
    return;
  }

  const POINT screen_pos{resolved->screen.x, resolved->screen.y};
  physical_pos = screen_pos;
  if (screen_pos.x < target_.x || screen_pos.x >= target_.x + target_.width ||
      screen_pos.y < target_.y || screen_pos.y >= target_.y + target_.height) {
    log_status(CursorDrawStatus::kOutsideTarget);
    return;
  }

  const double scale_x = static_cast<double>(output_width_) / target_.width;
  const double scale_y = static_cast<double>(output_height_) / target_.height;
  const int tip_x = static_cast<int>(
      std::lround((screen_pos.x - target_.x) * scale_x));
  const int tip_y = static_cast<int>(
      std::lround((screen_pos.y - target_.y) * scale_y));
  const BgraSurface surface{
      static_cast<std::uint8_t*>(bitmap_bits_), output_width_, output_height_,
      output_width_ * 4};
  const int scaled_system_cursor_height = std::max(
      1, static_cast<int>(std::lround(GetSystemMetrics(SM_CYCURSOR) * scale_y)));
  const int arrow_scale = std::max(
      1, static_cast<int>(std::lround(scaled_system_cursor_height / 24.0)));
  if (!draw_software_arrow(surface, tip_x, tip_y, arrow_scale)) {
    log_status(CursorDrawStatus::kDrawFailed);
    return;
  }
  log_status(resolved->used_target_local_fallback
                 ? CursorDrawStatus::kDrawnTargetLocalFallback
                 : CursorDrawStatus::kDrawnSoftwareCursor);
}

bool GdiScreenCapturer::capture(RawFrame& frame, bool generate_rgb565) const {
  if (!screen_dc_ || !memory_dc_ || !bitmap_ || !bitmap_bits_ ||
      !old_bitmap_ || old_bitmap_ == HGDI_ERROR) {
    return false;
  }
  BOOL copied = FALSE;
  if (output_width_ == target_.width && output_height_ == target_.height) {
    copied = BitBlt(memory_dc_, 0, 0, output_width_, output_height_,
                    screen_dc_, 0, 0, SRCCOPY);
  } else {
    SetStretchBltMode(memory_dc_, COLORONCOLOR);
    copied = StretchBlt(memory_dc_, 0, 0, output_width_, output_height_,
                        screen_dc_, 0, 0, target_.width, target_.height,
                        SRCCOPY);
  }

  if (!copied) {
    return false;
  }

  draw_system_cursor();
  frame.width = output_width_;
  frame.height = output_height_;
  frame.bgra = static_cast<const std::uint8_t*>(bitmap_bits_);
  frame.bgra_stride = output_width_ * 4;
  if (!generate_rgb565) {
    frame.rgb565.clear();
    return true;
  }

  const std::size_t pixel_count = static_cast<std::size_t>(output_width_) * output_height_;
  frame.rgb565.resize(pixel_count * 2);
  const auto* bgra = static_cast<const std::uint32_t*>(bitmap_bits_);
  auto* rgb565 = reinterpret_cast<std::uint16_t*>(frame.rgb565.data());
  for (std::size_t i = 0; i < pixel_count; ++i) {
    const std::uint32_t pixel = bgra[i];
    rgb565[i] = static_cast<std::uint16_t>(
        ((pixel >> 8u) & 0xf800u) |
        ((pixel >> 5u) & 0x07e0u) |
        ((pixel >> 3u) & 0x001fu));
  }
  return true;
}

}  // namespace fif::host
