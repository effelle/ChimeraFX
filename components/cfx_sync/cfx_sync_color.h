/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Canonical light snapshots and RGB/RGBW conversion for CFX sync.
 */

#pragma once

#if defined(USE_ESP32) || defined(USE_ESP8266)

#if defined(USE_ESP32)
#include "esphome/components/light/light_state.h"
#endif

#include <algorithm>
#include <cstdint>

namespace esphome {
namespace cfx_sync {

struct CFXSyncLightSnapshot {
  bool power{false};
  uint8_t brightness{0};
  uint8_t color_brightness{255};
  uint8_t red{0};
  uint8_t green{0};
  uint8_t blue{0};
  uint8_t white{0};
  bool has_white{false};

  bool operator==(const CFXSyncLightSnapshot &other) const {
    return this->power == other.power &&
           this->brightness == other.brightness &&
           this->color_brightness == other.color_brightness &&
           this->red == other.red &&
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

#if defined(USE_ESP32)
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
  snapshot.color_brightness =
      quantize_light_value(values.get_color_brightness());
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
  if (snapshot.has_white == follower_has_white) {
    return snapshot;
  }

  auto effective_channel = [&snapshot](uint8_t channel) {
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(channel) * snapshot.color_brightness + 127) /
        255);
  };
  uint8_t red = effective_channel(snapshot.red);
  uint8_t green = effective_channel(snapshot.green);
  uint8_t blue = effective_channel(snapshot.blue);

  if (follower_has_white) {
    const uint8_t neutral = std::min(red, std::min(green, blue));
    red -= neutral;
    green -= neutral;
    blue -= neutral;
    snapshot.white = neutral;
  } else {
    red = static_cast<uint8_t>(
        std::min(255, static_cast<int>(red) + snapshot.white));
    green = static_cast<uint8_t>(
        std::min(255, static_cast<int>(green) + snapshot.white));
    blue = static_cast<uint8_t>(
        std::min(255, static_cast<int>(blue) + snapshot.white));
    snapshot.white = 0;
  }

  snapshot.color_brightness = std::max(red, std::max(green, blue));
  if (snapshot.color_brightness == 0) {
    snapshot.red = 255;
    snapshot.green = 255;
    snapshot.blue = 255;
  } else {
    auto normalize_channel = [&snapshot](uint8_t channel) {
      return static_cast<uint8_t>(
          (static_cast<uint16_t>(channel) * 255 +
           snapshot.color_brightness / 2) /
          snapshot.color_brightness);
    };
    snapshot.red = normalize_channel(red);
    snapshot.green = normalize_channel(green);
    snapshot.blue = normalize_channel(blue);
  }
  snapshot.has_white = follower_has_white;
  return snapshot;
}
#endif

}  // namespace cfx_sync
}  // namespace esphome

#endif  // defined(USE_ESP32) || defined(USE_ESP8266)
