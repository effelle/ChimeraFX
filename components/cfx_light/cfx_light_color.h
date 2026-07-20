#pragma once

#include "cfx_light_color_policy.h"
#include "esphome/components/light/light_color_values.h"
#include "esphome/core/color.h"

namespace esphome {
namespace cfx_light {

inline Color mode_aware_color(const light::LightColorValues &values) {
  const auto mode = values.get_color_mode();
  const CFXLightChannels channels = mask_color_channels(
      {
          light::to_uint8_scale(values.get_color_brightness() *
                                values.get_red()),
          light::to_uint8_scale(values.get_color_brightness() *
                                values.get_green()),
          light::to_uint8_scale(values.get_color_brightness() *
                                values.get_blue()),
          light::to_uint8_scale(values.get_white()),
      },
      mode & light::ColorCapability::RGB,
      mode & light::ColorCapability::WHITE);
  return Color(channels.red, channels.green, channels.blue, channels.white);
}

}  // namespace cfx_light
}  // namespace esphome
