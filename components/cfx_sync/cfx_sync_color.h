/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Canonical light snapshots and RGB/RGBW conversion for CFX sync.
 */

#pragma once

#ifdef USE_ESP32

#include "esphome/components/light/light_state.h"

#include <algorithm>
#include <cstdint>

namespace esphome {
namespace cfx_sync {

struct CFXSyncLightSnapshot {
  bool power{false};
  uint8_t brightness{0};
  uint8_t red{0};
  uint8_t green{0};
  uint8_t blue{0};
  uint8_t white{0};
  bool has_white{false};

  bool operator==(const CFXSyncLightSnapshot &other) const {
    return this->power == other.power &&
           this->brightness == other.brightness && this->red == other.red &&
           this->green == other.green && this->blue == other.blue &&
           this->white == other.white && this->has_white == other.has_white;
  }

  bool operator!=(const CFXSyncLightSnapshot &other) const {
    return !(*this == other);
  }
};

inline uint8_t quantize_light_value(float value) {
  const float clamped = std::max(0.0f, std::min(1.0f, value));
  return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
}

inline bool light_supports_white(light::LightState &state) {
  const auto traits = state.get_traits();
  return traits.supports_color_mode(light::ColorMode::RGB_WHITE) ||
         traits.supports_color_mode(light::ColorMode::WHITE);
}

inline bool light_supports_rgb_white(light::LightState &state) {
  return state.get_traits().supports_color_mode(light::ColorMode::RGB_WHITE);
}

inline bool light_supports_rgb(light::LightState &state) {
  const auto traits = state.get_traits();
  return traits.supports_color_mode(light::ColorMode::RGB) ||
         traits.supports_color_mode(light::ColorMode::RGB_WHITE);
}

inline CFXSyncLightSnapshot capture_light_snapshot(
    light::LightState &state) {
  const auto &values = state.remote_values;
  CFXSyncLightSnapshot snapshot;
  snapshot.power = values.is_on();
  snapshot.brightness = quantize_light_value(values.get_brightness());
  snapshot.red = quantize_light_value(values.get_red());
  snapshot.green = quantize_light_value(values.get_green());
  snapshot.blue = quantize_light_value(values.get_blue());
  snapshot.has_white = light_supports_white(state);
  snapshot.white =
      snapshot.has_white ? quantize_light_value(values.get_white()) : 0;
  return snapshot;
}

inline CFXSyncLightSnapshot convert_color_for_follower(
    CFXSyncLightSnapshot snapshot, bool follower_has_white) {
  if (!follower_has_white) {
    snapshot.red = static_cast<uint8_t>(
        std::min(255, static_cast<int>(snapshot.red) + snapshot.white));
    snapshot.green = static_cast<uint8_t>(
        std::min(255, static_cast<int>(snapshot.green) + snapshot.white));
    snapshot.blue = static_cast<uint8_t>(
        std::min(255, static_cast<int>(snapshot.blue) + snapshot.white));
    snapshot.white = 0;
    snapshot.has_white = false;
    return snapshot;
  }

  if (snapshot.has_white) {
    return snapshot;
  }

  const uint8_t neutral =
      std::min(snapshot.red, std::min(snapshot.green, snapshot.blue));
  snapshot.red -= neutral;
  snapshot.green -= neutral;
  snapshot.blue -= neutral;
  snapshot.white = static_cast<uint8_t>(
      std::min(255, static_cast<int>(snapshot.white) + neutral));
  snapshot.has_white = true;
  return snapshot;
}

}  // namespace cfx_sync
}  // namespace esphome

#endif  // USE_ESP32
