/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Pure synchronization policy for segmented master lights.
 */

#pragma once

#include <cstddef>

namespace esphome {
namespace cfx_light {

constexpr float MASTER_BRIGHTNESS_EPSILON = 0.01f;

constexpr bool master_brightness_changed(
    float previous, float current,
    float epsilon = MASTER_BRIGHTNESS_EPSILON) {
  return (previous > current ? previous - current : current - previous) >
         epsilon;
}

class SegmentBrightnessAggregate {
 public:
  constexpr void add(bool is_on, float brightness) {
    if (!is_on) {
      return;
    }
    this->total_ += brightness;
    this->count_++;
  }

  constexpr bool any_on() const { return this->count_ != 0; }
  constexpr size_t count() const { return this->count_; }

  constexpr float average(float fallback) const {
    return this->count_ == 0 ? fallback : this->total_ / this->count_;
  }

 private:
  float total_{0.0f};
  size_t count_{0};
};

}  // namespace cfx_light
}  // namespace esphome
