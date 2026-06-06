#pragma once

#include <cstddef>

namespace esphome {
namespace cfx_hue_cycler {

constexpr size_t next_palette_index(size_t current, size_t count,
                                    bool current_known) {
  if (count == 0) {
    return 0;
  }
  return current_known ? (current + 1) % count : 0;
}

}  // namespace cfx_hue_cycler
}  // namespace esphome
