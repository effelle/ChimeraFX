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
  if ((int32_t) (now - this->ignore_press_until_ms_) < 0) {
    this->ignore_press_until_ms_ = now + POST_ACTION_QUIET_MS;
    return;
  }
  this->pressed_ = true;
  this->ramping_ = false;
  this->ramp_finished_ = false;
  this->suppress_toggle_ = false;
  this->press_started_ms_ = now;
  this->ramp_started_ms_ = 0;
  this->ramp_end_ms_ = 0;
  this->last_ramp_update_ms_ = 0;
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
}

void CFXDimmer::add_light(light::LightState *state) {
  this->lights_.push_back(state);
  this->saved_states_.push_back({});
}

void CFXDimmer::release() {
  if (!this->pressed_) {
    const uint32_t now = millis();
    if ((int32_t) (now - this->ignore_press_until_ms_) < 0) {
      this->ignore_press_until_ms_ = now + POST_ACTION_QUIET_MS;
    }
    return;
  }
  const uint32_t released_at_ms = millis();
  if (!this->ramping_ &&
      (released_at_ms - this->press_started_ms_) >= this->long_press_ms_) {
    this->start_ramp_(this->press_started_ms_ + this->long_press_ms_);
  }
  this->finalize_release_(released_at_ms);
}

void CFXDimmer::finalize_release_(uint32_t released_at_ms) {
  if (this->ramping_) {
    this->freeze_ramp_(released_at_ms);
  }
  const bool should_toggle = !this->suppress_toggle_ && !this->ramping_;
  this->pressed_ = false;
  this->ramping_ = false;
  this->ramp_finished_ = false;
  this->suppress_toggle_ = false;
  this->ramp_started_ms_ = 0;
  this->ramp_end_ms_ = 0;
  this->last_ramp_update_ms_ = 0;
  this->ignore_press_until_ms_ = millis() + POST_ACTION_QUIET_MS;
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();

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
    return;
  }
  this->service_manual_ramp_(now);
}

void CFXDimmer::start_ramp_(uint32_t now) {
  this->ramping_ = true;
  this->ramp_finished_ = false;
  this->suppress_toggle_ = true;
  this->ramp_direction_up_ =
      this->any_target_visible_() ? this->next_direction_up_ : true;
  this->next_direction_up_ = !this->ramp_direction_up_;
  this->save_direction_();
  this->ramp_started_ms_ = now;
  this->ramp_end_ms_ = now;
  this->last_ramp_update_ms_ = 0;
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
  const float target = this->ramp_target_brightness_();
  if (!this->ramp_direction_up_) {
    for (size_t i = 0; i < this->lights_.size(); i++) {
      this->save_target_state_(i, this->lights_[i]);
    }
  }
  for (auto *state : this->lights_) {
    const float start = this->target_start_brightness_(state);
    const uint32_t duration = this->ramp_duration_ms_(start, target);
    const bool manual = this->target_has_effect_(state);
    this->ramp_start_brightness_.push_back(start);
    this->ramp_durations_ms_.push_back(duration);
    this->ramp_manual_.push_back(manual);
    this->ramp_end_ms_ = std::max(this->ramp_end_ms_, now + duration);
    this->apply_brightness_(state, manual ? start : target,
                            manual ? 0 : duration);
  }
  this->service_manual_ramp_(now);
}

void CFXDimmer::finish_ramp_() {
  if (!this->ramping_) {
    return;
  }
  this->ramping_ = false;
  this->ramp_finished_ = true;
  if (!this->ramp_direction_up_) {
    this->turn_off_targets_();
    this->ramp_start_brightness_.clear();
    this->ramp_durations_ms_.clear();
    this->ramp_manual_.clear();
    return;
  }
  for (auto *state : this->lights_) {
    this->apply_brightness_(state, this->max_brightness_, 0);
  }
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
}

void CFXDimmer::freeze_ramp_(uint32_t now) {
  if (!this->ramping_) {
    return;
  }
  const float off_cutoff = this->ramp_target_brightness_() +
                           OFF_BRIGHTNESS_HYSTERESIS;
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *state = this->lights_[i];
    if (state == nullptr) {
      continue;
    }
    const float current = this->ramp_current_brightness_(i, now);
    if (!this->ramp_direction_up_ && current <= off_cutoff) {
      this->save_target_state_(i, state);
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
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
}

void CFXDimmer::service_manual_ramp_(uint32_t now) {
  if (this->last_ramp_update_ms_ != 0 &&
      (now - this->last_ramp_update_ms_) < RAMP_UPDATE_INTERVAL_MS) {
    return;
  }
  this->last_ramp_update_ms_ = now;
  for (size_t i = 0; i < this->lights_.size(); i++) {
    if (i >= this->ramp_manual_.size() || !this->ramp_manual_[i]) {
      continue;
    }
    this->apply_brightness_(this->lights_[i],
                            this->ramp_current_brightness_(i, now), 0);
  }
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

void CFXDimmer::restore_saved_state_(size_t index, light::LightState *state) {
  if (state == nullptr) {
    return;
  }
  if (index >= this->saved_states_.size() || !this->saved_states_[index].valid) {
    this->apply_brightness_(state, this->toggle_on_brightness_(state), 0);
    return;
  }
  auto call = state->make_call();
  call.set_state(true);
  call.set_brightness(this->saved_states_[index].brightness);
  if (!this->saved_states_[index].effect.empty()) {
    call.set_transition_length(0);
    call.perform();
    auto effect_call = state->make_call();
    effect_call.set_effect(this->saved_states_[index].effect);
    effect_call.perform();
  } else {
    call.set_transition_length(0);
    call.perform();
  }
}

void CFXDimmer::save_target_state_(size_t index, light::LightState *state) {
  if (state == nullptr || !state->remote_values.is_on()) {
    return;
  }
  if (this->saved_states_.size() < this->lights_.size()) {
    this->saved_states_.resize(this->lights_.size());
  }
  const float brightness = state->remote_values.get_brightness();
  if (brightness <= this->off_visibility_cutoff_()) {
    return;
  }
  this->saved_states_[index].valid = true;
  this->saved_states_[index].brightness = this->clamp_brightness_(brightness);
  const std::string effect = state->get_effect_name();
  this->saved_states_[index].effect = effect == "None" ? std::string() : effect;
}

void CFXDimmer::save_direction_() {
  if (!this->restore_direction_ || !this->direction_pref_ready_) {
    return;
  }
  uint8_t stored = this->next_direction_up_ ? 1 : 0;
  this->direction_pref_.save(&stored);
}

void CFXDimmer::toggle_targets_() {
  const bool turn_off = this->any_target_visible_();
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *state = this->lights_[i];
    if (state == nullptr) {
      continue;
    }
    if (turn_off) {
      this->save_target_state_(i, state);
    }
    auto call = state->make_call();
    call.set_transition_length(0);
    call.set_state(!turn_off);
    if (!turn_off) {
      this->restore_saved_state_(i, state);
      continue;
    }
    call.perform();
  }
}

void CFXDimmer::turn_off_targets_() {
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *state = this->lights_[i];
    if (state == nullptr) {
      continue;
    }
    this->save_target_state_(i, state);
    auto call = state->make_call();
    call.set_transition_length(0);
    call.set_state(false);
    call.perform();
  }
}

float CFXDimmer::off_visibility_cutoff_() const {
  return std::max(this->min_brightness_,
                  std::min(this->max_brightness_,
                           this->off_brightness_ +
                               OFF_BRIGHTNESS_HYSTERESIS));
}

bool CFXDimmer::any_target_visible_() const {
  for (auto *state : this->lights_) {
    if (state != nullptr && state->remote_values.is_on() &&
        state->remote_values.get_brightness() > this->off_visibility_cutoff_()) {
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

float CFXDimmer::ramp_current_brightness_(size_t index, uint32_t now) const {
  const float target = this->ramp_target_brightness_();
  if (index >= this->ramp_start_brightness_.size() ||
      index >= this->ramp_durations_ms_.size()) {
    return target;
  }
  const float start = this->ramp_start_brightness_[index];
  const uint32_t duration = this->ramp_durations_ms_[index];
  if (duration == 0) {
    return target;
  }
  const uint32_t elapsed = now - this->ramp_started_ms_;
  const float progress =
      std::min(1.0f, static_cast<float>(elapsed) / duration);
  return this->clamp_brightness_(start + ((target - start) * progress));
}

float CFXDimmer::toggle_on_brightness_(light::LightState *state) const {
  if (state == nullptr) {
    return this->max_brightness_;
  }
  const float brightness = state->remote_values.get_brightness();
  if (brightness <= this->off_visibility_cutoff_()) {
    return this->max_brightness_;
  }
  return this->clamp_brightness_(brightness);
}

bool CFXDimmer::target_has_effect_(light::LightState *state) const {
  if (state == nullptr || !state->remote_values.is_on()) {
    return false;
  }
  return state->get_effect_name() != "None";
}

float CFXDimmer::clamp_brightness_(float value) const {
  return std::max(this->min_brightness_,
                  std::min(this->max_brightness_, value));
}

}  // namespace cfx_dimmer
}  // namespace esphome
