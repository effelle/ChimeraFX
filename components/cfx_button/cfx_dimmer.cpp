#include "cfx_dimmer.h"
#include "cfx_dimmer_timing.h"

#include "esphome/core/helpers.h"
#include <algorithm>
#include <cmath>

namespace esphome {
namespace cfx_dimmer {

void CFXDimmer::press() {
  if (this->pressed_ ||
      this->directional_press_ != DirectionalPress::NONE) {
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
  this->press_started_ms_ = now;
  this->ramp_started_ms_ = 0;
  this->ramp_end_ms_ = 0;
  this->last_ramp_update_ms_ = 0;
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
  this->gesture_.begin(this->any_target_on_());
}

void CFXDimmer::add_light(light::LightState *state) {
  this->lights_.push_back(state);
  this->saved_states_.emplace_back();
}

void CFXDimmer::release() {
  if (this->directional_press_ != DirectionalPress::NONE) {
    return;
  }
  if (!this->pressed_) {
    const uint32_t now = millis();
    if ((int32_t) (now - this->ignore_press_until_ms_) < 0) {
      this->ignore_press_until_ms_ = now + POST_ACTION_QUIET_MS;
    }
    return;
  }
  const uint32_t released_at_ms = millis();
  const bool threshold_reached =
      (released_at_ms - this->press_started_ms_) >= this->long_press_ms_;
  const DimmerReleaseAction action =
      this->gesture_.release_action(threshold_reached);
  if (action == DimmerReleaseAction::HOLD &&
      !this->gesture_.long_press_started() && !this->ramp_finished_) {
    this->start_ramp_(this->press_started_ms_ + this->long_press_ms_);
  }
  this->finalize_release_(action, released_at_ms);
}

void CFXDimmer::press_up() {
  this->press_direction_(true);
}

void CFXDimmer::release_up() {
  this->release_direction_(true);
}

void CFXDimmer::press_down() {
  this->press_direction_(false);
}

void CFXDimmer::release_down() {
  this->release_direction_(false);
}

void CFXDimmer::finalize_release_(DimmerReleaseAction action,
                                  uint32_t released_at_ms) {
  if (action == DimmerReleaseAction::HOLD && this->ramping_) {
    this->freeze_ramp_(released_at_ms);
  }
  this->pressed_ = false;
  this->ramping_ = false;
  this->ramp_finished_ = false;
  this->ramp_started_ms_ = 0;
  this->ramp_end_ms_ = 0;
  this->last_ramp_update_ms_ = 0;
  this->ignore_press_until_ms_ = millis() + POST_ACTION_QUIET_MS;
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
  this->gesture_.reset();

  if (action == DimmerReleaseAction::TURN_OFF) {
    this->turn_off_targets_();
  } else if (action == DimmerReleaseAction::TURN_ON) {
    this->turn_on_targets_();
  }
}

void CFXDimmer::finalize_directional_release_(uint32_t released_at_ms) {
  if (this->ramping_) {
    this->freeze_ramp_(released_at_ms);
  }
  this->pressed_ = false;
  this->ramping_ = false;
  this->ramp_finished_ = false;
  this->ramp_started_ms_ = 0;
  this->ramp_end_ms_ = 0;
  this->last_ramp_update_ms_ = 0;
  this->ignore_press_until_ms_ = millis() + POST_ACTION_QUIET_MS;
  this->directional_press_ = DirectionalPress::NONE;
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
  this->gesture_.reset();
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
    if (!this->gesture_.can_start_ramp()) {
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
  this->start_ramp_(now, false, false);
}

void CFXDimmer::start_ramp_(uint32_t now, bool forced_direction_up,
                            bool forced_direction) {
  if (!forced_direction && !this->gesture_.can_start_ramp()) {
    return;
  }
  if (forced_direction && !forced_direction_up && !this->any_target_on_()) {
    return;
  }
  this->ramping_ = true;
  this->ramp_finished_ = false;
  if (forced_direction) {
    this->ramp_direction_up_ = forced_direction_up;
  } else {
    this->gesture_.mark_long_press_started();
    this->ramp_direction_up_ = select_ramp_direction_up(
        this->average_target_brightness_(), this->next_direction_up_,
        this->first_ramp_after_boot_);
    this->first_ramp_after_boot_ = false;
    this->next_direction_up_ = !this->ramp_direction_up_;
  }
  this->ramp_started_ms_ = now;
  this->ramp_end_ms_ = now;
  this->last_ramp_update_ms_ = 0;
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
  const float target = this->ramp_target_brightness_();
  for (auto *state : this->lights_) {
    const float start = this->target_start_brightness_(state);
    const uint32_t duration = this->ramp_duration_ms_(start, target);
    const bool manual = this->target_has_effect_(state);
    this->ramp_start_brightness_.push_back(start);
    this->ramp_durations_ms_.push_back(duration);
    this->ramp_manual_.push_back(manual);
    this->ramp_end_ms_ = std::max(this->ramp_end_ms_, now + duration);
    if (!manual && duration != 0) {
      publish_light_ramp_hint(state, now + duration);
    }
    this->apply_brightness_(state, manual ? start : target,
                            manual ? 0 : duration);
  }
  this->service_manual_ramp_(now);
}

void CFXDimmer::press_direction_(bool direction_up) {
  if (this->pressed_ ||
      this->directional_press_ != DirectionalPress::NONE) {
    return;
  }
  const uint32_t now = millis();
  if ((int32_t) (now - this->ignore_press_until_ms_) < 0) {
    this->ignore_press_until_ms_ = now + POST_ACTION_QUIET_MS;
    return;
  }
  if (!direction_up && !this->any_target_on_()) {
    return;
  }
  this->pressed_ = true;
  this->directional_press_ =
      direction_up ? DirectionalPress::UP : DirectionalPress::DOWN;
  this->ramping_ = false;
  this->ramp_finished_ = false;
  this->press_started_ms_ = now;
  this->ramp_started_ms_ = 0;
  this->ramp_end_ms_ = 0;
  this->last_ramp_update_ms_ = 0;
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
  this->gesture_.reset();
  this->start_ramp_(now, direction_up, true);
}

void CFXDimmer::release_direction_(bool direction_up) {
  const DirectionalPress expected =
      direction_up ? DirectionalPress::UP : DirectionalPress::DOWN;
  if (this->directional_press_ != expected) {
    return;
  }
  this->finalize_directional_release_(millis());
}

void CFXDimmer::finish_ramp_() {
  if (!this->ramping_) {
    return;
  }
  this->ramping_ = false;
  this->ramp_finished_ = true;
  const float target = this->ramp_target_brightness_();
  for (auto *state : this->lights_) {
    this->apply_brightness_(state, target, 0);
  }
  this->ramp_start_brightness_.clear();
  this->ramp_durations_ms_.clear();
  this->ramp_manual_.clear();
}

void CFXDimmer::freeze_ramp_(uint32_t now) {
  if (!this->ramping_) {
    return;
  }
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *state = this->lights_[i];
    if (state == nullptr) {
      continue;
    }
    const float current = this->freeze_brightness_(state, i, now);
    publish_light_ramp_duration_hint(state, RAMP_FREEZE_TRANSITION_MS);
    this->apply_brightness_(state, current, RAMP_FREEZE_TRANSITION_MS);
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
  if (transition_ms == 0) {
    clear_light_timing_hint(state);
    publish_light_ramp_duration_hint(state, transition_ms);
  }
  auto call = state->make_call();
  call.set_transition_length(transition_ms);
  this->apply_color_values_(call, state, state->remote_values);
  call.set_state(true);
  call.set_brightness(this->clamp_brightness_(brightness));
  call.perform();
}

void CFXDimmer::apply_color_values_(
    light::LightCall &call, light::LightState *state,
    const light::LightColorValues &values) const {
  if (state == nullptr) {
    return;
  }

  const auto traits = state->get_traits();
  const auto mode = values.get_color_mode();
  const bool wants_white =
      mode == light::ColorMode::WHITE ||
      mode == light::ColorMode::RGB_WHITE ||
      mode == light::ColorMode::UNKNOWN;
  const bool wants_rgb =
      mode == light::ColorMode::RGB ||
      mode == light::ColorMode::RGB_WHITE ||
      mode == light::ColorMode::UNKNOWN;

  if (wants_rgb &&
      traits.supports_color_mode(light::ColorMode::RGB_WHITE)) {
    call.set_color_mode(light::ColorMode::RGB_WHITE);
    call.set_color_brightness(values.get_color_brightness());
    call.set_rgb(values.get_red(), values.get_green(), values.get_blue());
    call.set_white(values.get_white());
    return;
  }
  if (wants_rgb && traits.supports_color_mode(light::ColorMode::RGB)) {
    call.set_color_mode(light::ColorMode::RGB);
    call.set_color_brightness(values.get_color_brightness());
    call.set_rgb(values.get_red(), values.get_green(), values.get_blue());
    return;
  }
  if (wants_white && traits.supports_color_mode(light::ColorMode::WHITE)) {
    call.set_color_mode(light::ColorMode::WHITE);
    call.set_white(values.get_white());
  }
}

void CFXDimmer::turn_on_targets_() {
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *state = this->lights_[i];
    if (state == nullptr) {
      continue;
    }
    auto call = state->make_call();
    if (i >= this->saved_states_.size() ||
        !this->saved_states_[i].valid) {
      this->apply_color_values_(call, state, state->remote_values);
      call.set_state(true);
      call.perform();
      continue;
    }

    const auto &saved = this->saved_states_[i];
    if (saved.effect.empty()) {
      call.set_effect("None");
      this->apply_color_values_(call, state, saved.values);
      call.set_state(true);
    } else {
      call.set_state(true);
      call.set_brightness_if_supported(saved.values.get_brightness());
      call.set_effect(saved.effect);
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
    if (state->remote_values.is_on() && i < this->saved_states_.size()) {
      auto &saved = this->saved_states_[i];
      saved.values = state->remote_values;
      const std::string effect = state->get_effect_name();
      saved.effect = effect != "None" ? effect : "";
      saved.valid = true;
    }
    auto call = state->make_call();
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

float CFXDimmer::average_target_brightness_() const {
  float total = 0.0f;
  size_t count = 0;
  for (auto *state : this->lights_) {
    if (state == nullptr || !state->remote_values.is_on()) {
      continue;
    }
    total += state->remote_values.get_brightness();
    count++;
  }
  return count == 0 ? this->min_brightness_ : total / count;
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
  return this->ramp_direction_up_ ? this->max_brightness_
                                  : this->min_brightness_;
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

bool CFXDimmer::measured_ramp_brightness_(light::LightState *state,
                                          size_t index, uint32_t now,
                                          float &measured) const {
  if (state == nullptr || index >= this->ramp_start_brightness_.size() ||
      index >= this->ramp_durations_ms_.size() ||
      index >= this->ramp_manual_.size() || this->ramp_manual_[index]) {
    return false;
  }
  const uint32_t duration = this->ramp_durations_ms_[index];
  if (duration == 0 || !state->current_values.is_on()) {
    return false;
  }

  const float start = this->ramp_start_brightness_[index];
  const float target = this->ramp_target_brightness_();
  const uint32_t elapsed = now - this->ramp_started_ms_;
  const float progress =
      std::min(1.0f, static_cast<float>(elapsed) / duration);
  const float estimated =
      this->clamp_brightness_(start + ((target - start) * progress));
  measured = this->clamp_brightness_(
      state->current_values.get_brightness() *
      state->current_values.get_state());

  const float low =
      std::min(start, target) - RAMP_MEASURED_EDGE_EPSILON;
  const float high =
      std::max(start, target) + RAMP_MEASURED_EDGE_EPSILON;
  if (measured < low || measured > high) {
    return false;
  }
  if (std::abs(measured - estimated) > RAMP_MEASURED_MAX_DRIFT) {
    return false;
  }
  if (progress < RAMP_MEASURED_EDGE_PROGRESS &&
      std::abs(measured - target) <= RAMP_MEASURED_EDGE_EPSILON) {
    return false;
  }
  return true;
}

float CFXDimmer::freeze_brightness_(light::LightState *state, size_t index,
                                    uint32_t now) const {
  float measured = 0.0f;
  if (this->measured_ramp_brightness_(state, index, now, measured)) {
    return measured;
  }
  return this->ramp_current_brightness_(index, now);
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
