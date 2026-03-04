/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * CFXVirtualSegmentLight - Zero-copy AddressableLight slice over parent buffer
 * Each instance maps virtual pixel [0..N] to physical pixel [start..stop]
 * in the parent CFXLightOutput's buffer. Enables per-segment light entities.
 */

#pragma once

#ifdef USE_ESP32

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

  void write_state(light::LightState *state) override {
    // Suppress transition-engine flushes when an effect is active.
    // ESPHome's transition engine calls write_state(state) repeatedly during
    // the ON-transition period, painting white to the buffer each time.
    // Flushing during this window causes the white flash before the effect
    // renders. An active effect drives DMA via the post-apply() write_state.
    if (state != nullptr && !state->get_effect_name().empty()) {
      return; // Effect active — suppress transition-engine flush
    }
    parent_->request_segment_flush();
  }

  light::LightTraits get_traits() override {
    // Same RGBW/RGB capability as parent
    return parent_->get_traits();
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
