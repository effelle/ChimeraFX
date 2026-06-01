#include "cfx_dimmer.h"

#include "esphome/core/helpers.h"
#include <algorithm>
#include <cmath>

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
  this->ramp_finished_ = false;
  this->suppress_toggle_ = false;
  this->press_started_ms_ = now;
  this->ramp_started_ms_ = 0;
  this->ramp_end_ms_ = 0;
}

void CFXDimmer::release() {
  if (!this->pressed_) {
    return;
  }
  if (this->ramping_) {
    this->freeze_ramp_();
  }
  const bool should_toggle = !this->suppress_toggle_ && !this->ramping_;
  this->pressed_ = false;
  this->ramping_ = false;
  this->ramp_finished_ = false;
  this->suppress_toggle_ = false;
  this->ramp_started_ms_ = 0;
  this->ramp_end_ms_ = 0;

  if (should_toggle) {
    this->toggle_targets_();
  }
}

void CFXDimmer::loop() {
  if (!this->pressed_) {
    return;
  }
  const uint32_t now = millis();
  if (this->ramp_finished_) {
    return;
  }
  if (!this->ramping_) {
    if ((now - this->press_started_ms_) < this->long_press_ms_) {
      return;
    }
    this->start_ramp_(now);
    return;
  }
  if ((int32_t) (now - this->ramp_end_ms_) >= 0) {
    this->finish_ramp_();
  }
}

void CFXDimmer::start_ramp_(uint32_t now) {
  this->ramping_ = true;
  this->ramp_finished_ = false;
  this->suppress_toggle_ = true;
  this->ramp_direction_up_ =
      this->any_target_on_() ? this->next_direction_up_ : true;
  this->next_direction_up_ = !this->ramp_direction_up_;
  this->save_direction_();
  this->ramp_started_ms_ = now;
  this->ramp_end_ms_ = now;
  const float target = this->ramp_target_brightness_();
  for (auto *state : this->lights_) {
    const float start = this->target_start_brightness_(state);
    const uint32_t duration = this->ramp_duration_ms_(start, target);
    this->ramp_end_ms_ = std::max(this->ramp_end_ms_, now + duration);
    this->apply_brightness_(state, target, duration);
  }
}

void CFXDimmer::finish_ramp_() {
  if (!this->ramping_) {
    return;
  }
  this->ramping_ = false;
  this->ramp_finished_ = true;
  if (!this->ramp_direction_up_) {
    this->turn_off_targets_();
    return;
  }
  for (auto *state : this->lights_) {
    this->apply_brightness_(state, this->max_brightness_, 0);
  }
}

void CFXDimmer::freeze_ramp_() {
  if (!this->ramping_) {
    return;
  }
  const float off_cutoff = this->ramp_target_brightness_() +
                           OFF_BRIGHTNESS_HYSTERESIS;
  for (auto *state : this->lights_) {
    if (state == nullptr) {
      continue;
    }
    const float current = this->current_brightness_(state);
    if (!this->ramp_direction_up_ && current <= off_cutoff) {
      auto call = state->make_call();
      call.set_transition_length(0);
      call.set_state(false);
      call.perform();
    } else {
      this->apply_brightness_(state, current, 0);
    }
  }
  this->ramping_ = false;
  this->ramp_finished_ = true;
}

void CFXDimmer::apply_brightness_(light::LightState *state, float brightness,
                                  uint32_t transition_ms) {
  if (state == nullptr) {
    return;
  }
  auto call = state->make_call();
  call.set_transition_length(transition_ms);
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

void CFXDimmer::turn_off_targets_() {
  for (auto *state : this->lights_) {
    if (state == nullptr) {
      continue;
    }
    auto call = state->make_call();
    call.set_transition_length(0);
    call.set_state(false);
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

uint32_t CFXDimmer::ramp_duration_ms_(float start, float target) const {
  if (this->ramp_time_ms_ == 0) {
    return 0;
  }
  const float range =
      std::max(0.001f, this->max_brightness_ - this->min_brightness_);
  const float distance = std::abs(target - start);
  const uint32_t duration =
      static_cast<uint32_t>((distance / range) * this->ramp_time_ms_);
  return std::max(MIN_RAMP_TRANSITION_MS, duration);
}

float CFXDimmer::ramp_target_brightness_() const {
  if (this->ramp_direction_up_) {
    return this->max_brightness_;
  }
  return std::max(this->min_brightness_,
                  std::min(this->max_brightness_, this->off_brightness_));
}

float CFXDimmer::target_start_brightness_(light::LightState *state) const {
  if (state == nullptr || !state->remote_values.is_on()) {
    return this->min_brightness_;
  }
  return this->clamp_brightness_(state->remote_values.get_brightness());
}

float CFXDimmer::current_brightness_(light::LightState *state) const {
  if (state == nullptr || !state->current_values.is_on()) {
    return this->min_brightness_;
  }
  return this->clamp_brightness_(state->current_values.get_brightness());
}

float CFXDimmer::clamp_brightness_(float value) const {
  return std::max(this->min_brightness_,
                  std::min(this->max_brightness_, value));
}

}  // namespace cfx_dimmer
}  // namespace esphome
