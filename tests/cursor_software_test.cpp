#include "cursor_software.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

using fif::host::BgraSurface;
using fif::host::draw_software_arrow;
using fif::host::rgb_signature;

int main() {
  constexpr int width = 64;
  constexpr int height = 64;
  std::vector<std::uint8_t> pixels(width * height * 4, 127);
  BgraSurface surface{pixels.data(), width, height, width * 4};

  const auto before = rgb_signature(surface, 5, 7, 24, 38);
  assert(draw_software_arrow(surface, 5, 7, 2));
  const auto after = rgb_signature(surface, 5, 7, 24, 38);
  assert(before != after);

  bool have_black = false;
  bool have_white = false;
  for (std::size_t index = 0; index < pixels.size(); index += 4) {
    have_black = have_black || pixels[index] == 0;
    have_white = have_white || pixels[index] == 255;
  }
  assert(have_black && have_white);

  assert(draw_software_arrow(surface, width - 2, height - 2, 1));
  assert(!draw_software_arrow(BgraSurface{}, 0, 0, 1));
  return 0;
}
