#pragma once

#include <optional>

namespace fif::host {

struct CursorCoordinate {
  int x = 0;
  int y = 0;
};

struct CursorTargetBounds {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct ResolvedCursorPosition {
  CursorCoordinate screen;
  bool used_target_local_fallback = false;
};

inline std::optional<ResolvedCursorPosition> resolve_cursor_position(
    CursorCoordinate reported, std::optional<CursorCoordinate> physical,
    unsigned long physical_error, CursorTargetBounds target) {
  if (physical) {
    return ResolvedCursorPosition{*physical, false};
  }

  constexpr unsigned long kAccessDenied = 5;
  if (physical_error != kAccessDenied) {
    return std::nullopt;
  }

  const bool reported_is_absolute =
      reported.x >= target.x && reported.x < target.x + target.width &&
      reported.y >= target.y && reported.y < target.y + target.height;
  if (reported_is_absolute) {
    return ResolvedCursorPosition{reported, false};
  }

  const bool reported_is_target_local =
      reported.x >= 0 && reported.x < target.width &&
      reported.y >= 0 && reported.y < target.height;
  if (!reported_is_target_local || (target.x == 0 && target.y == 0)) {
    return std::nullopt;
  }

  return ResolvedCursorPosition{
      CursorCoordinate{target.x + reported.x, target.y + reported.y}, true};
}

}  // namespace fif::host
