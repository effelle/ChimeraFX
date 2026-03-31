/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * CFXVirtualSegmentLight - Zero-copy AddressableLight slice over parent buffer
 * Each instance maps virtual pixel [0..N] to physical pixel [start..stop]
 * in the parent CFXLightOutput's buffer. Enables per-segment light entities.
 */

#pragma once

#ifdef USE_ESP32

#include "../cfx_effect/cfx_utils.h"
#include "cfx_light.h"
#include "esphome/components/light/addressable_light.h"
#include "esphome/core/component.h"

#include <string>

namespace esphome {
namespace cfx_light {

class CFXVirtualSegmentLight : public light::AddressableLight {
public:
  CFXVirtualSegmentLight(CFXLightOutput *parent, uint16_t start, uint16_t stop,
                         const std::string &seg_id)
      : parent_(parent), start_(start), stop_(stop), seg_id_(seg_id) {}

  // --- AddressableLight interface ---

  int32_t size() const override { return stop_ - start_; }

  light::ESPColorView get_view_internal(int32_t index) const override {
    // Zero-copy: access parent's buffer via public operator[]
    return (*parent_)[start_ + index];
  }

  void loop() override {
    light::LightState *st = this->state_parent_;
    bool effect_active = false;
    if (st != nullptr) {
      effect_active = (st->get_effect_name() != "None");
    }
    if (!effect_active && parent_->get_master_light_state() != nullptr) {
      effect_active =
          (parent_->get_master_light_state()->get_effect_name() != "None");
    }

    // BUG FIX: ESPHome's AddressableLight::loop() aggressively overwrites
    // the pixel buffer with `current_values` if a transition (transformer) is
    // active. This causes transition spikes (Bug 11) on segments if CFX is
    // running.
    if (effect_active || parent_->has_outro()) {
      return;
    }

    // Normal behavior for solid colors (no effect, no outro)
    light::AddressableLight::loop();
  }

  // BUG 8 & 11 FIX: Intercept update_state. If CFX is active on master,
  // it takes full ownership of brightness and pixels. Creating a Transformer
  // here would corrupt the pixels (flashes/spikes).
  void update_state(light::LightState *state) override {
    if (parent_->has_outro()) return;
    if (parent_->get_master_light_state() != nullptr &&
        parent_->get_master_light_state()->get_effect_name() != "None") return;
    if (this->is_effect_active()) return;
    // Only handle the transformer-driven fade path here (transition_length > 0).
    // When no transformer is active the solid-color paint happens in
    // write_state() using remote_values instead. (CFX-032)
    if (!state->is_transformer_active()) return;
    auto val = state->current_values;
    auto max_brightness =
        light::to_uint8_scale(val.get_brightness() * val.get_state());
    this->correction_.set_local_brightness(max_brightness);
    Color c = light::color_from_light_color_values(val);
    // BUG 13 FIX: Apply force_white to solid segment colors
    if (parent_->get_force_white_switch() != nullptr &&
        parent_->get_force_white_switch()->state &&
        parent_->has_white_channel()) {
      cfx::apply_force_white(c.r, c.g, c.b, c.w);
    }
    this->all() = c;
    this->schedule_show();
  }

  void write_state(light::LightState *state) override {
    if (parent_->has_outro()) return;
    // CFX-032: Paint solid color using remote_values (the commanded target)
    // rather than current_values (transformer-interpolated). current_values
    // can be 0 on the first frame even with 0ms transition_length, causing
    // segments to stay black or ignore brightness changes. remote_values
    // always reflects the final desired state. Skip when an effect or master
    // CFX effect owns the pixels.
    if (state != nullptr &&
        !this->is_effect_active() &&
        (parent_->get_master_light_state() == nullptr ||
         parent_->get_master_light_state()->get_effect_name() == "None")) {
      auto val = state->remote_values;
      auto max_brightness =
          light::to_uint8_scale(val.get_brightness() * val.get_state());
      this->correction_.set_local_brightness(max_brightness);
      Color c = light::color_from_light_color_values(val);
      if (parent_->get_force_white_switch() != nullptr &&
          parent_->get_force_white_switch()->state &&
          parent_->has_white_channel()) {
        cfx::apply_force_white(c.r, c.g, c.b, c.w);
      }
      this->all() = c;
    }
    parent_->request_segment_flush();
  }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    // Inherit the physical capabilities of the strip (restore RGB segment
    // controls)
    if (parent_->has_white_channel()) {
      traits.set_supported_color_modes(
          {light::ColorMode::RGB_WHITE, light::ColorMode::WHITE});
    } else {
      traits.set_supported_color_modes({light::ColorMode::RGB});
    }
    return traits;
  }

  void clear_effect_data() override {
    for (int32_t i = 0; i < this->size(); i++)
      parent_->get_effect_data()[start_ + i] = 0;
  }

  float get_setup_priority() const override {
    // After parent setup
    return setup_priority::HARDWARE - 1.0f;
  }

  void setup() override {
    // Nothing to do — parent owns all hardware
  }

  void dump_config() override {
    ESP_LOGCONFIG("cfx_vseg", "  Virtual Segment '%s': LEDs %u-%u (%d pixels)",
                  seg_id_.c_str(), start_, stop_, this->size());
  }

  // --- Segment identity ---

  const std::string &get_segment_id() const { return seg_id_; }
  uint16_t get_start() const { return start_; }
  uint16_t get_stop() const { return stop_; }
  CFXLightOutput *get_parent() const { return parent_; }

protected:
  CFXLightOutput *parent_;
  uint16_t start_;
  uint16_t stop_;
  std::string seg_id_;
};

} // namespace cfx_light
} // namespace esphome

#endif // USE_ESP32
