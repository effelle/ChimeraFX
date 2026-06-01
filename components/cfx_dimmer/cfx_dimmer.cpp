#include "cfx_dimmer.h"

#include "esphome/core/helpers.h"
#include <algorithm>

namespace esphome {
namespace cfx_dimmer {

void CFXDimmer::setup() {
  if (!this->restore_direction_ || global_preferences == nullptr) {
    return;
  }
  const std::string key = "cfx_dimmer_" + this->id_ + "_direction";
  this->direction_pref_ =
      global_preferences->make_preference<uint8_t>(fnv1a_hash(key.c_str()),
                                                   true);
  this->direction_pref_ready_ = true;
  uint8_t restored = 1;
  if (this->direction_pref_.load(&restored)) {
    this->next_direction_up_ = restored != 0;
  }
}

void CFXDimmer::press() {
  if (this->pressed_) {
    return;
  }
  const uint32_t now = millis();
  this->pressed_ = true;
  this->ramping_ = false;
  this->suppress_toggle_ = false;
  this->press_started_ms_ = now;
  this->ramp_started_ms_ = 0;
  this->last_ramp_update_ms_ = 0;
}

void CFXDimmer::release() {
  if (!this->pressed_) {
    return;
  }
  const bool should_toggle = !this->suppress_toggle_ && !this->ramping_;
  this->pressed_ = false;
  this->ramping_ = false;
  this->suppress_toggle_ = false;
  this->ramp_started_ms_ = 0;
  this->last_ramp_update_ms_ = 0;
  this->ramp_start_brightness_.clear();

  if (should_toggle) {
    this->toggle_targets_();
  }
}

void CFXDimmer::loop() {
  if (!this->pressed_) {
    return;
  }
  const uint32_t now = millis();
  if (!this->ramping_) {
    if ((now - this->press_started_ms_) < this->long_press_ms_) {
      return;
    }
    this->start_ramp_(now);
  }
  this->apply_ramp_(now);
}

void CFXDimmer::start_ramp_(uint32_t now) {
  this->ramping_ = true;
  this->suppress_toggle_ = true;
  this->ramp_direction_up_ = this->next_direction_up_;
  this->next_direction_up_ = !this->next_direction_up_;
  this->save_direction_();
  this->ramp_started_ms_ = now;
  this->last_ramp_update_ms_ = 0;
  this->ramp_start_brightness_.clear();
  this->ramp_start_brightness_.reserve(this->lights_.size());
  for (auto *state : this->lights_) {
    this->ramp_start_brightness_.push_back(
        this->target_start_brightness_(state));
  }
}

void CFXDimmer::apply_ramp_(uint32_t now) {
  if (this->ramp_time_ms_ == 0) {
    this->last_ramp_update_ms_ = now;
  } else if (this->last_ramp_update_ms_ != 0 &&
             (now - this->last_ramp_update_ms_) < RAMP_UPDATE_INTERVAL_MS) {
    return;
  } else {
    this->last_ramp_update_ms_ = now;
  }

  float progress = 1.0f;
  if (this->ramp_time_ms_ > 0) {
    progress = static_cast<float>(now - this->ramp_started_ms_) /
               static_cast<float>(this->ramp_time_ms_);
    if (progress > 1.0f) {
      progress = 1.0f;
    }
  }

  const float target =
      this->ramp_direction_up_ ? this->max_brightness_ : this->min_brightness_;
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *state = this->lights_[i];
    if (state == nullptr) {
      continue;
    }
    const float start =
        i < this->ramp_start_brightness_.size()
            ? this->ramp_start_brightness_[i]
            : this->target_start_brightness_(state);
    const float brightness = start + ((target - start) * progress);
    this->apply_brightness_(state, brightness);
  }
}

void CFXDimmer::apply_brightness_(light::LightState *state, float brightness) {
  if (state == nullptr) {
    return;
  }
  auto call = state->make_call();
  call.set_transition_length(0);
  call.set_state(true);
  call.set_brightness(this->clamp_brightness_(brightness));
  call.perform();
}

void CFXDimmer::save_direction_() {
  if (!this->restore_direction_ || !this->direction_pref_ready_) {
    return;
  }
  uint8_t stored = this->next_direction_up_ ? 1 : 0;
  this->direction_pref_.save(&stored);
}

void CFXDimmer::toggle_targets_() {
  const bool turn_off = this->any_target_on_();
  for (auto *state : this->lights_) {
    if (state == nullptr) {
      continue;
    }
    auto call = state->make_call();
    call.set_transition_length(0);
    call.set_state(!turn_off);
    if (!turn_off) {
      const float brightness =
          this->clamp_brightness_(state->remote_values.get_brightness());
      call.set_brightness(brightness);
    }
    call.perform();
  }
}

bool CFXDimmer::any_target_on_() const {
  for (auto *state : this->lights_) {
    if (state != nullptr && state->remote_values.is_on()) {
      return true;
    }
  }
  return false;
}

float CFXDimmer::target_start_brightness_(light::LightState *state) const {
  if (state == nullptr || !state->remote_values.is_on()) {
    return this->min_brightness_;
  }
  return this->clamp_brightness_(state->remote_values.get_brightness());
}

float CFXDimmer::clamp_brightness_(float value) const {
  return std::max(this->min_brightness_,
                  std::min(this->max_brightness_, value));
}

}  // namespace cfx_dimmer
}  // namespace esphome
