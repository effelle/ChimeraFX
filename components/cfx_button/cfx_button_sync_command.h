#pragma once

#include <cstdint>

namespace esphome {
namespace cfx_button {

enum class CFXButtonInputAction : uint8_t {
  PRIMARY = 0,
  DIMMER_UP = 1,
  DIMMER_DOWN = 2,
};

enum class CFXButtonSyncKind : uint8_t {
  BINARY = 0,
  DIMMER = 1,
  HUE = 2,
  CCT = 3,
  EFFECT = 4,
};

struct CFXButtonSyncCommand {
  CFXButtonSyncKind kind{CFXButtonSyncKind::BINARY};
  CFXButtonInputAction action{CFXButtonInputAction::PRIMARY};
  bool pressed{false};
  bool has_power{false};
  bool power{false};
  bool toggle{false};
  bool has_brightness{false};
  uint8_t brightness{0};
  bool has_ramp{false};
  uint16_t ramp_ms{0};
  bool direction_up{false};
  bool direction_down{false};
  bool has_rgb{false};
  uint8_t red{0};
  uint8_t green{0};
  uint8_t blue{0};
  bool has_white{false};
  uint8_t white{0};
  bool has_color_brightness{false};
  uint8_t color_brightness{0};
  bool has_color_temperature{false};
  uint16_t color_temperature_mireds{0};
  bool has_cold_warm_white{false};
  uint8_t cold_white{0};
  uint8_t warm_white{0};
};

}  // namespace cfx_button
}  // namespace esphome
