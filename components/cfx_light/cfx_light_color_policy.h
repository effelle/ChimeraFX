#pragma once

#include <cstdint>

namespace esphome {
namespace cfx_light {

struct CFXLightChannels {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t white;
};

constexpr CFXLightChannels mask_color_channels(CFXLightChannels channels,
                                                bool has_rgb,
                                                bool has_white) {
  return {
      has_rgb ? channels.red : uint8_t{0},
      has_rgb ? channels.green : uint8_t{0},
      has_rgb ? channels.blue : uint8_t{0},
      has_white ? channels.white : uint8_t{0},
  };
}

constexpr bool should_restore_rgb_white(bool force_white_active,
                                        bool current_mode_white,
                                        bool supports_rgb_white) {
  return !force_white_active && current_mode_white && supports_rgb_white;
}

}  // namespace cfx_light
}  // namespace esphome
