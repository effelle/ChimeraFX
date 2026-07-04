/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Canonical light snapshots and RGB/RGBW conversion for CFX sync.
 */

#pragma once

#if defined(USE_ESP32) || defined(USE_ESP8266)

#include "esphome/components/light/light_state.h"

#include <algorithm>
#include <cstdint>

namespace esphome {
namespace cfx_sync {

struct CFXSyncLightSnapshot {
  bool power{false};
  uint8_t brightness{0};
  bool has_color{true};
  bool has_color_brightness{true};
  uint8_t color_brightness{255};
  uint8_t red{0};
  uint8_t green{0};
  uint8_t blue{0};
  uint8_t white{0};
  bool has_white{false};
  bool has_color_temperature{false};
  uint16_t color_temperature_mireds{0};
  bool has_cold_warm_white{false};
  uint8_t cold_white{0};
  uint8_t warm_white{0};

  bool operator==(const CFXSyncLightSnapshot &other) const {
    return this->power == other.power &&
           this->brightness == other.brightness &&
           this->has_color == other.has_color &&
           this->has_color_brightness == other.has_color_brightness &&
           this->color_brightness == other.color_brightness &&
           this->red == other.red &&
           this->green == other.green && this->blue == other.blue &&
           this->white == other.white &&
           this->has_white == other.has_white &&
           this->has_color_temperature == other.has_color_temperature &&
           this->color_temperature_mireds ==
               other.color_temperature_mireds &&
           this->has_cold_warm_white == other.has_cold_warm_white &&
           this->cold_white == other.cold_white &&
           this->warm_white == other.warm_white;
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

inline bool light_supports_color_temperature(light::LightState &state) {
  return state.get_traits().supports_color_capability(
      light::ColorCapability::COLOR_TEMPERATURE);
}

inline bool light_supports_cold_warm_white(light::LightState &state) {
  return state.get_traits().supports_color_capability(
      light::ColorCapability::COLD_WARM_WHITE);
}

#if defined(USE_ESP32)
inline CFXSyncLightSnapshot capture_light_snapshot(
    light::LightState &state) {
  const auto &values = state.remote_values;
  const auto color_mode = values.get_color_mode();
  const bool wants_rgb_or_white =
      color_mode == light::ColorMode::RGB ||
      color_mode == light::ColorMode::RGB_WHITE ||
      color_mode == light::ColorMode::RGB_COLOR_TEMPERATURE ||
      color_mode == light::ColorMode::RGB_COLD_WARM_WHITE ||
      color_mode == light::ColorMode::WHITE;
  const bool wants_color_temperature =
      color_mode == light::ColorMode::COLOR_TEMPERATURE ||
      color_mode == light::ColorMode::COLD_WARM_WHITE ||
      color_mode == light::ColorMode::RGB_COLOR_TEMPERATURE ||
      color_mode == light::ColorMode::RGB_COLD_WARM_WHITE;
  const bool wants_cold_warm_white =
      color_mode == light::ColorMode::COLD_WARM_WHITE ||
      color_mode == light::ColorMode::RGB_COLD_WARM_WHITE;
  CFXSyncLightSnapshot snapshot;
  snapshot.power = values.is_on();
  snapshot.brightness = quantize_light_value(values.get_brightness());
  snapshot.has_color = wants_rgb_or_white;
  snapshot.has_color_brightness = wants_rgb_or_white;
  snapshot.color_brightness =
      quantize_light_value(values.get_color_brightness());
  snapshot.red = quantize_light_value(values.get_red());
  snapshot.green = quantize_light_value(values.get_green());
  snapshot.blue = quantize_light_value(values.get_blue());
  snapshot.has_white = light_supports_white(state);
  snapshot.white =
      snapshot.has_white ? quantize_light_value(values.get_white()) : 0;
  snapshot.has_color_temperature =
      wants_color_temperature && light_supports_color_temperature(state);
  snapshot.color_temperature_mireds =
      snapshot.has_color_temperature
          ? static_cast<uint16_t>(values.get_color_temperature())
          : 0;
  snapshot.has_cold_warm_white =
      wants_cold_warm_white && light_supports_cold_warm_white(state);
  snapshot.cold_white =
      snapshot.has_cold_warm_white
          ? quantize_light_value(values.get_cold_white())
          : 0;
  snapshot.warm_white =
      snapshot.has_cold_warm_white
          ? quantize_light_value(values.get_warm_white())
          : 0;
  return snapshot;
}
#endif

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

inline CFXSyncLightSnapshot convert_cold_warm_white_for_rgb(
    uint8_t cold_white, uint8_t warm_white) {
  CFXSyncLightSnapshot snapshot;
  snapshot.has_color = true;
  snapshot.has_color_brightness = true;
  snapshot.has_white = false;
  snapshot.red = 255;
  snapshot.green = 255;
  snapshot.blue = 255;
  snapshot.color_brightness = std::max(cold_white, warm_white);
  return snapshot;
}

}  // namespace cfx_sync
}  // namespace esphome

#endif  // defined(USE_ESP32) || defined(USE_ESP8266)
