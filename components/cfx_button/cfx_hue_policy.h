#pragma once

#include <cstddef>

namespace esphome {
namespace cfx_hue_cycler {

enum class ShortPressOutput {
  SELECTED_HUE,
  FIXED_COLOR,
};

constexpr size_t next_palette_index(size_t current, size_t count,
                                    bool current_known) {
  if (count == 0) {
    return 0;
  }
  return current_known ? (current + 1) % count : 0;
}

constexpr ShortPressOutput next_short_press_output(bool matches_fixed_color,
                                                   bool matches_selected_hue) {
  if (matches_fixed_color || !matches_selected_hue) {
    return ShortPressOutput::SELECTED_HUE;
  }
  return ShortPressOutput::FIXED_COLOR;
}

}  // namespace cfx_hue_cycler
}  // namespace esphome
