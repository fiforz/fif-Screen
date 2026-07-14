#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace fif::host {

struct BgraSurface {
  std::uint8_t* pixels = nullptr;
  int width = 0;
  int height = 0;
  int stride = 0;
};

inline std::uint64_t rgb_signature(const BgraSurface& surface,
                                   int x, int y, int width, int height) {
  if (!surface.pixels || surface.width <= 0 || surface.height <= 0 ||
      surface.stride < surface.width * 4 || width <= 0 || height <= 0) {
    return 0;
  }

  const int left = std::clamp(x, 0, surface.width);
  const int top = std::clamp(y, 0, surface.height);
  const int right = std::clamp(x + width, 0, surface.width);
  const int bottom = std::clamp(y + height, 0, surface.height);
  std::uint64_t hash = 1469598103934665603ull;
  for (int row = top; row < bottom; ++row) {
    const auto* pixel = surface.pixels + static_cast<std::size_t>(row) * surface.stride +
                        static_cast<std::size_t>(left) * 4;
    for (int column = left; column < right; ++column, pixel += 4) {
      for (int channel = 0; channel < 3; ++channel) {
        hash ^= pixel[channel];
        hash *= 1099511628211ull;
      }
    }
  }
  return hash;
}

inline bool draw_software_arrow(const BgraSurface& surface,
                                int tip_x, int tip_y, int scale) {
  if (!surface.pixels || surface.width <= 0 || surface.height <= 0 ||
      surface.stride < surface.width * 4) {
    return false;
  }

  constexpr std::array<const char*, 19> kArrow = {
      "B...........",
      "BB..........",
      "BWB.........",
      "BWWB........",
      "BWWWB.......",
      "BWWWWB......",
      "BWWWWWB.....",
      "BWWWWWWB....",
      "BWWWWWWWB...",
      "BWWWWBBBB...",
      "BWWBWB......",
      "BWB.BWB.....",
      "BB..BWB.....",
      "B...BWWB....",
      "....BWWB....",
      ".....BB.....",
      "............",
      "............",
      "............",
  };

  scale = std::clamp(scale, 1, 4);
  bool changed = false;
  for (int source_y = 0; source_y < static_cast<int>(kArrow.size()); ++source_y) {
    for (int source_x = 0; kArrow[source_y][source_x] != '\0'; ++source_x) {
      const char value = kArrow[source_y][source_x];
      if (value == '.') {
        continue;
      }
      const std::uint8_t color = value == 'W' ? 255 : 0;
      for (int offset_y = 0; offset_y < scale; ++offset_y) {
        const int y = tip_y + source_y * scale + offset_y;
        if (y < 0 || y >= surface.height) {
          continue;
        }
        for (int offset_x = 0; offset_x < scale; ++offset_x) {
          const int x = tip_x + source_x * scale + offset_x;
          if (x < 0 || x >= surface.width) {
            continue;
          }
          auto* pixel = surface.pixels + static_cast<std::size_t>(y) * surface.stride +
                        static_cast<std::size_t>(x) * 4;
          changed = changed || pixel[0] != color || pixel[1] != color ||
                    pixel[2] != color;
          pixel[0] = color;
          pixel[1] = color;
          pixel[2] = color;
          pixel[3] = 255;
        }
      }
    }
  }
  return changed;
}

}  // namespace fif::host
