#pragma once

#include <cstdint>

namespace esphome {
namespace cfx_dimmer {

enum class DimmerReleaseAction : uint8_t {
  NONE,
  TURN_ON,
  TURN_OFF,
  HOLD,
};

constexpr bool select_ramp_direction_up(float brightness,
                                        bool alternating_direction_up,
                                        bool first_ramp_after_boot) {
  if (brightness <= 0.30f) {
    return true;
  }
  if (brightness >= 0.70f || first_ramp_after_boot) {
    return false;
  }
  return alternating_direction_up;
}

class DimmerGesture {
 public:
  constexpr void begin(bool was_on) {
    this->active_ = true;
    this->was_on_ = was_on;
    this->long_press_started_ = false;
  }

  constexpr bool active() const { return this->active_; }
  constexpr bool was_on() const { return this->was_on_; }
  constexpr bool can_start_ramp() const {
    return this->active_ && this->was_on_;
  }
  constexpr bool long_press_started() const {
    return this->long_press_started_;
  }

  constexpr void mark_long_press_started() {
    if (this->can_start_ramp()) {
      this->long_press_started_ = true;
    }
  }

  constexpr DimmerReleaseAction release_action(
      bool long_press_threshold_reached) const {
    if (!this->active_) {
      return DimmerReleaseAction::NONE;
    }
    if (this->long_press_started_ || long_press_threshold_reached) {
      return this->was_on_ ? DimmerReleaseAction::HOLD
                           : DimmerReleaseAction::NONE;
    }
    return this->was_on_ ? DimmerReleaseAction::TURN_OFF
                         : DimmerReleaseAction::TURN_ON;
  }

  constexpr void reset() {
    this->active_ = false;
    this->was_on_ = false;
    this->long_press_started_ = false;
  }

 protected:
  bool active_{false};
  bool was_on_{false};
  bool long_press_started_{false};
};

}  // namespace cfx_dimmer
}  // namespace esphome
