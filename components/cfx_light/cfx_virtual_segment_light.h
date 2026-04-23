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
  static std::vector<CFXVirtualSegmentLight *> all_segments;

  CFXVirtualSegmentLight(CFXLightOutput *parent, uint16_t start, uint16_t stop,
                         const std::string &seg_id)
      : parent_(parent), start_(start), stop_(stop), seg_id_(seg_id) {
    all_segments.push_back(this);
  }

  ~CFXVirtualSegmentLight() {
    all_segments.erase(
        std::remove(all_segments.begin(), all_segments.end(), this),
        all_segments.end());
  }

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
    if (parent_->has_outro())
      return;

    if (parent_->get_master_light_state() != nullptr &&
        parent_->get_master_light_state()->get_effect_name() != "None") {
      return;
    }

    auto val = state->current_values;

    if (this->is_effect_active())
      return;

    // CFX-032: correction_ is held at 255; bake brightness into channels.
    // For a real fade (bri > 0 and transformer active), let ESPHome drive
    // the pixels. Always fall through when bri==0 so turning OFF paints
    // black even if a 0ms transformer exists on that frame.
    float bri = val.get_brightness() * val.get_state();
    if (state->is_transformer_active() && bri > 0.0f)
      return;
    Color c = light::color_from_light_color_values(val);
    c.r = (uint8_t)(c.r * bri);
    c.g = (uint8_t)(c.g * bri);
    c.b = (uint8_t)(c.b * bri);
    c.w = (uint8_t)(c.w * bri);

    // Resolve force_white from this segment's own control first, then fall back
    // to the parent strip-level switch when no segment-local override exists.
    if (parent_->is_force_white_active_for(state)) {
      cfx::apply_force_white(c.r, c.g, c.b, c.w);
    }

    this->all() = c;
    // Do NOT call schedule_show() here. ESPHome will call write_state()
    // as part of its normal output pipeline. Calling schedule_show() causes
    // write_state to fire twice per update, making pending=2 instead of 1
    // and breaking the flush counter logic. (CFX-032)
  }

  void write_state(light::LightState *state) override {
    // Delegate DMA to parent via the segment-flush path.
    // Suppress flush while parent has an outro in progress.
    if (parent_->has_outro())
      return;
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
