#include "cursor_position.hpp"

#include <cassert>
#include <optional>

using fif::host::CursorCoordinate;
using fif::host::CursorTargetBounds;
using fif::host::resolve_cursor_position;

int main() {
  const CursorTargetBounds left_target{-1920, 0, 1920, 1080};

  const auto physical = resolve_cursor_position(
      CursorCoordinate{470, 688}, CursorCoordinate{-1450, 688}, 0, left_target);
  assert(physical);
  assert(physical->screen.x == -1450 && physical->screen.y == 688);
  assert(!physical->used_target_local_fallback);

  const auto local_fallback = resolve_cursor_position(
      CursorCoordinate{470, 688}, std::nullopt, 5, left_target);
  assert(local_fallback);
  assert(local_fallback->screen.x == -1450 && local_fallback->screen.y == 688);
  assert(local_fallback->used_target_local_fallback);

  const auto absolute_fallback = resolve_cursor_position(
      CursorCoordinate{-1450, 688}, std::nullopt, 5, left_target);
  assert(absolute_fallback);
  assert(absolute_fallback->screen.x == -1450 && absolute_fallback->screen.y == 688);
  assert(!absolute_fallback->used_target_local_fallback);

  assert(!resolve_cursor_position(
      CursorCoordinate{470, 688}, std::nullopt, 87, left_target));
  assert(!resolve_cursor_position(
      CursorCoordinate{2000, 688}, std::nullopt, 5, left_target));

  const CursorTargetBounds origin_target{0, 0, 1920, 1080};
  const auto origin_absolute = resolve_cursor_position(
      CursorCoordinate{470, 688}, std::nullopt, 5, origin_target);
  assert(origin_absolute);
  assert(origin_absolute->screen.x == 470 && origin_absolute->screen.y == 688);
  assert(!origin_absolute->used_target_local_fallback);

  return 0;
}
