#include "screen_capture.hpp"

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

bool contains_case_insensitive(const std::wstring& haystack, const std::wstring& needle) {
  std::wstring lower_haystack = haystack;
  std::wstring lower_needle = needle;
  std::transform(lower_haystack.begin(), lower_haystack.end(), lower_haystack.begin(), towlower);
  std::transform(lower_needle.begin(), lower_needle.end(), lower_needle.begin(), towlower);
  return lower_haystack.find(lower_needle) != std::wstring::npos;
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

void draw_system_cursor(HDC dc, const ScreenTarget& target,
                        int output_width, int output_height) {
  CURSORINFO cursor_info{};
  cursor_info.cbSize = sizeof(cursor_info);
  if (!GetCursorInfo(&cursor_info) || cursor_info.flags != CURSOR_SHOWING ||
      cursor_info.hCursor == nullptr) {
    return;
  }

  const POINT screen_pos = cursor_info.ptScreenPos;
  if (screen_pos.x < target.x || screen_pos.x >= target.x + target.width ||
      screen_pos.y < target.y || screen_pos.y >= target.y + target.height) {
    return;
  }

  HCURSOR cursor = CopyIcon(cursor_info.hCursor);
  if (!cursor) {
    return;
  }

  int hotspot_x = 0;
  int hotspot_y = 0;
  int cursor_width = GetSystemMetrics(SM_CXCURSOR);
  int cursor_height = GetSystemMetrics(SM_CYCURSOR);

  ICONINFO icon_info{};
  if (GetIconInfo(cursor, &icon_info)) {
    hotspot_x = static_cast<int>(icon_info.xHotspot);
    hotspot_y = static_cast<int>(icon_info.yHotspot);

    BITMAP bitmap{};
    if (icon_info.hbmColor &&
        GetObjectW(icon_info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
      cursor_width = bitmap.bmWidth;
      cursor_height = bitmap.bmHeight;
    } else if (icon_info.hbmMask &&
               GetObjectW(icon_info.hbmMask, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
      cursor_width = bitmap.bmWidth;
      cursor_height = icon_info.fIcon ? bitmap.bmHeight : bitmap.bmHeight / 2;
    }

    if (icon_info.hbmColor) {
      DeleteObject(icon_info.hbmColor);
    }
    if (icon_info.hbmMask) {
      DeleteObject(icon_info.hbmMask);
    }
  }

  const double scale_x = static_cast<double>(output_width) / target.width;
  const double scale_y = static_cast<double>(output_height) / target.height;
  const int tip_x = static_cast<int>(std::lround((screen_pos.x - target.x) * scale_x));
  const int tip_y = static_cast<int>(std::lround((screen_pos.y - target.y) * scale_y));
  const int draw_x = tip_x - static_cast<int>(std::lround(hotspot_x * scale_x));
  const int draw_y = tip_y - static_cast<int>(std::lround(hotspot_y * scale_y));
  const int draw_width = std::max(1, static_cast<int>(std::lround(cursor_width * scale_x)));
  const int draw_height = std::max(1, static_cast<int>(std::lround(cursor_height * scale_y)));

  DrawIconEx(dc, draw_x, draw_y, cursor, draw_width, draw_height, 0, nullptr, DI_NORMAL);
  DestroyIcon(cursor);
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

std::optional<ScreenTarget> find_fifscreen_display() {
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
      std::cout << "display candidate name=" << narrow(target->device_name)
                << " string=" << narrow(target->device_string)
                << " id=" << narrow(target->device_id)
                << " pos=" << target->x << "," << target->y
                << " size=" << target->width << "x" << target->height
                << " primary=" << (target->primary ? "true" : "false") << "\n";
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
  screen_dc_ = GetDC(nullptr);
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
    ReleaseDC(nullptr, screen_dc_);
  }
}

bool GdiScreenCapturer::capture(RawFrame& frame, bool generate_rgb565) const {
  if (!screen_dc_ || !memory_dc_ || !bitmap_ || !bitmap_bits_ ||
      !old_bitmap_ || old_bitmap_ == HGDI_ERROR) {
    return false;
  }
  BOOL copied = FALSE;
  if (output_width_ == target_.width && output_height_ == target_.height) {
    copied = BitBlt(memory_dc_, 0, 0, output_width_, output_height_,
                    screen_dc_, target_.x, target_.y, SRCCOPY | CAPTUREBLT);
  } else {
    SetStretchBltMode(memory_dc_, COLORONCOLOR);
    copied = StretchBlt(memory_dc_, 0, 0, output_width_, output_height_,
                        screen_dc_, target_.x, target_.y, target_.width, target_.height,
                        SRCCOPY | CAPTUREBLT);
  }

  if (!copied) {
    return false;
  }

  draw_system_cursor(memory_dc_, target_, output_width_, output_height_);
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
