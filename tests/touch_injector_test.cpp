#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include "touch_injector.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

std::atomic<int> g_pointer_down{0};
std::atomic<int> g_pointer_up{0};
std::atomic<int> g_mouse_down{0};
std::atomic<int> g_mouse_up{0};

LRESULT CALLBACK probe_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_POINTERDOWN) {
    ++g_pointer_down;
  } else if (message == WM_POINTERUP) {
    ++g_pointer_up;
  } else if (message == WM_LBUTTONDOWN) {
    ++g_mouse_down;
  } else if (message == WM_LBUTTONUP) {
    ++g_mouse_up;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

void pump_messages(std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  MSG message{};
  while (std::chrono::steady_clock::now() < deadline) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace

int main() {
  (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  const wchar_t* class_name = L"FifScreenTouchInjectionProbe";
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = probe_window_proc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = class_name;
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    std::cerr << "failed to register touch probe window\n";
    return 1;
  }

  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
    std::cerr << "failed to locate primary monitor\n";
    return 1;
  }
  const int x = monitor.rcWork.left + 40;
  const int y = monitor.rcWork.top + 40;
  constexpr int width = 320;
  constexpr int height = 200;
  HWND window = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW, class_name, L"FifScreen touch test",
      WS_POPUP | WS_VISIBLE, x, y, width, height, nullptr, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  if (!window) {
    std::cerr << "failed to create touch probe window\n";
    return 1;
  }
  ShowWindow(window, SW_SHOWNOACTIVATE);
  UpdateWindow(window);
  pump_messages(std::chrono::milliseconds(50));

  fif::host::ScreenTarget target;
  target.x = x;
  target.y = y;
  target.width = width;
  target.height = height;

  fif::host::TouchInjector injector;
  fif::TouchFrame down;
  down.contacts.push_back(
      {1, fif::TouchPhase::Down, 32768, 32768, 512, 2048, 2048});
  if (!injector.inject(down, target)) {
    DestroyWindow(window);
    std::cerr << "touch down injection failed\n";
    return 1;
  }
  pump_messages(std::chrono::milliseconds(30));

  fif::TouchFrame up;
  up.contacts.push_back(
      {1, fif::TouchPhase::Up, 32768, 32768, 0, 2048, 2048});
  if (!injector.inject(up, target)) {
    DestroyWindow(window);
    std::cerr << "touch up injection failed\n";
    return 1;
  }
  pump_messages(std::chrono::milliseconds(150));

  fif::TouchFrame first_down;
  first_down.contacts.push_back(
      {1, fif::TouchPhase::Down, 20000, 30000, 512, 2048, 2048});
  if (!injector.inject(first_down, target)) {
    DestroyWindow(window);
    std::cerr << "multi-touch first down failed\n";
    return 1;
  }
  pump_messages(std::chrono::milliseconds(20));

  fif::TouchFrame second_down;
  second_down.contacts.push_back(
      {1, fif::TouchPhase::Move, 21000, 30000, 512, 2048, 2048});
  second_down.contacts.push_back(
      {2, fif::TouchPhase::Down, 45000, 30000, 512, 2048, 2048});
  if (!injector.inject(second_down, target)) {
    DestroyWindow(window);
    std::cerr << "multi-touch second down failed\n";
    return 1;
  }
  pump_messages(std::chrono::milliseconds(20));

  fif::TouchFrame first_up;
  first_up.contacts.push_back(
      {1, fif::TouchPhase::Up, 21000, 30000, 0, 2048, 2048});
  first_up.contacts.push_back(
      {2, fif::TouchPhase::Move, 46000, 30000, 512, 2048, 2048});
  if (!injector.inject(first_up, target)) {
    DestroyWindow(window);
    std::cerr << "multi-touch first up failed\n";
    return 1;
  }
  pump_messages(std::chrono::milliseconds(20));

  fif::TouchFrame second_up;
  second_up.contacts.push_back(
      {2, fif::TouchPhase::Up, 46000, 30000, 0, 2048, 2048});
  if (!injector.inject(second_up, target)) {
    DestroyWindow(window);
    std::cerr << "multi-touch second up failed\n";
    return 1;
  }
  pump_messages(std::chrono::milliseconds(150));

  fif::TouchFrame disconnect_down;
  disconnect_down.contacts.push_back(
      {3, fif::TouchPhase::Down, 32768, 40000, 512, 2048, 2048});
  if (!injector.inject(disconnect_down, target)) {
    DestroyWindow(window);
    std::cerr << "disconnect cancellation down failed\n";
    return 1;
  }
  pump_messages(std::chrono::milliseconds(20));
  injector.cancel_active();
  pump_messages(std::chrono::milliseconds(150));

  DestroyWindow(window);
  if (g_pointer_down.load() < 4 || g_pointer_up.load() < 4 ||
      g_mouse_down.load() < 1 || g_mouse_up.load() < 1 ||
      injector.active_contact_count() != 0) {
    std::cerr << "touch messages missing down=" << g_pointer_down.load()
              << " up=" << g_pointer_up.load()
              << " mouse_down=" << g_mouse_down.load()
              << " mouse_up=" << g_mouse_up.load()
              << " active=" << injector.active_contact_count() << "\n";
    return 1;
  }
  std::cout << "touch injector test passed down=" << g_pointer_down.load()
            << " up=" << g_pointer_up.load()
            << " mouse_down=" << g_mouse_down.load()
            << " mouse_up=" << g_mouse_up.load() << "\n";
  return 0;
}
