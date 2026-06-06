#include "cfx_cct_sweeper.h"

#include "esphome/core/helpers.h"
#include <algorithm>
#include <cmath>

namespace esphome {
namespace cfx_cct_sweeper {

void CFXCCTSweeper::setup() {
  if (global_preferences == nullptr) {
    return;
  }
  if (this->restore_direction_) {
    const std::string key = "cfx_cct_sweeper_" + this->id_ + "_direction";
    this->direction_pref_ =
        global_preferences->make_preference<uint8_t>(fnv1a_hash(key.c_str()),
                                                     true);
    this->direction_pref_ready_ = true;
    uint8_t restored = this->next_direction_warmer_ ? 1 : 0;
    if (this->direction_pref_.load(&restored)) {
      this->next_direction_warmer_ = restored != 0;
    }
  }
  if (this->restore_) {
    const std::string key =
        "cfx_cct_sweeper_" + this->id_ + "_preferred_white";
    this->preferred_white_pref_ =
        global_preferences->make_preference<StoredPreferredWhite>(
            fnv1a_hash(key.c_str()), true);
    this->preferred_white_pref_ready_ = true;
    StoredPreferredWhite restored;
    if (this->preferred_white_pref_.load(&restored) &&
        restored.version == PREFERRED_WHITE_VERSION &&
        std::isfinite(restored.red) && std::isfinite(restored.green) &&
        std::isfinite(restored.blue) && std::isfinite(restored.white)) {
      this->preferred_white_ =
          this->clamp_color_({restored.red, restored.green, restored.blue,
                              restored.white});
    }
  }
}

void CFXCCTSweeper::add_light(light::LightState *state) {
  this->lights_.push_back(state);
}

void CFXCCTSweeper::press() {
  if (this->pressed_) {
    return;
  }
  const uint32_t now = millis();
  if ((int32_t) (now - this->ignore_press_until_ms_) < 0) {
    return;
  }
  this->pressed_ = true;
  this->gesture_began_on_ = this->any_target_on_();
  if (this->gesture_began_on_) {
    this->has_retained_state_ = true;
  }
  this->sweeping_ = false;
  this->sweep_finished_ = false;
  this->suppress_toggle_ = false;
  this->press_started_ms_ = now;
  this->sweep_end_ms_ = 0;
  this->sweep_started_ms_ = 0;
  this->sweep_targets_.clear();
}

void CFXCCTSweeper::release() {
  if (!this->pressed_) {
    return;
  }
  const uint32_t now = millis();
  const bool held_long =
      (now - this->press_started_ms_) >= this->long_press_ms_;
  if (held_long && !this->sweeping_ && !this->sweep_finished_) {
    if (can_start_cct_sweep(this->gesture_began_on_)) {
      this->start_sweep_(this->press_started_ms_ + this->long_press_ms_);
    } else {
      this->suppress_toggle_ = true;
      this->sweep_finished_ = true;
    }
  }
  if (this->sweeping_) {
    this->freeze_sweep_();
  }
  if (this->suppress_toggle_) {
    this->ignore_press_until_ms_ = millis() + POST_SWEEP_GUARD_MS;
  }
  const bool should_toggle = !held_long && !this->suppress_toggle_;
  this->pressed_ = false;
  this->sweeping_ = false;
  this->sweep_finished_ = false;
  this->suppress_toggle_ = false;
  this->sweep_end_ms_ = 0;
  this->sweep_started_ms_ = 0;
  this->sweep_targets_.clear();

  if (should_toggle) {
    this->handle_short_press_();
  }
}

void CFXCCTSweeper::loop() {
  if (this->any_target_on_()) {
    this->has_retained_state_ = true;
  }
  if (!this->pressed_ || this->sweep_finished_) {
    return;
  }
  const uint32_t now = millis();
  if (!this->sweeping_) {
    if ((now - this->press_started_ms_) < this->long_press_ms_) {
      return;
    }
    if (!can_start_cct_sweep(this->gesture_began_on_)) {
      this->suppress_toggle_ = true;
      this->sweep_finished_ = true;
      return;
    }
    this->start_sweep_(now);
    return;
  }
  if ((int32_t) (now - this->sweep_end_ms_) >= 0) {
    this->finish_sweep_();
  }
}

void CFXCCTSweeper::start_sweep_(uint32_t now) {
  this->sweeping_ = true;
  this->sweep_finished_ = false;
  this->suppress_toggle_ = true;
  this->sweep_direction_warmer_ = select_sweep_direction_warmer(
      this->next_direction_warmer_, this->first_sweep_after_boot_);
  this->first_sweep_after_boot_ = false;
  this->next_direction_warmer_ = !this->sweep_direction_warmer_;
  this->save_direction_();
  this->sweep_end_ms_ = now;
  this->sweep_started_ms_ = now;
  this->active_sweep_target_ = this->sweep_target_();
  this->sweep_targets_.clear();
  this->sweep_targets_.reserve(this->lights_.size());
  for (auto *state : this->lights_) {
    const CFXColor start = this->sweep_start_color_(state);
    const uint32_t duration =
        this->sweep_duration_ms_(start, this->active_sweep_target_);
    this->sweep_targets_.push_back({true, start, duration});
    this->sweep_end_ms_ = std::max(this->sweep_end_ms_, now + duration);
    this->apply_color_(state, this->active_sweep_target_, duration);
  }
}

void CFXCCTSweeper::finish_sweep_() {
  if (!this->sweeping_) {
    return;
  }
  this->sweeping_ = false;
  this->sweep_finished_ = true;
  this->preferred_white_ = this->active_sweep_target_;
  this->last_endpoint_ = CCTEndpoint::PREFERRED;
  this->apply_color_to_all_(this->preferred_white_, 0);
  this->save_preferred_white_();
  this->sweep_targets_.clear();
}

void CFXCCTSweeper::freeze_sweep_() {
  if (!this->sweeping_) {
    return;
  }
  const uint32_t now = millis();
  CFXColor selected = this->active_sweep_target_;
  if (!this->sweep_targets_.empty() && this->sweep_targets_[0].valid) {
    selected = this->sweep_color_at_(this->sweep_targets_[0], now);
  }
  this->preferred_white_ = this->clamp_color_(selected);
  this->last_endpoint_ = CCTEndpoint::PREFERRED;
  this->apply_color_to_all_(this->preferred_white_, 0);
  this->save_preferred_white_();
  this->sweeping_ = false;
  this->sweep_finished_ = true;
  this->sweep_targets_.clear();
}

CFXColor CFXCCTSweeper::sweep_color_at_(const SweepTarget &target,
                                         uint32_t now) const {
  float progress = 1.0f;
  if (target.duration_ms > 0) {
    progress = static_cast<float>(now - this->sweep_started_ms_) /
               static_cast<float>(target.duration_ms);
  }
  progress = std::max(0.0f, std::min(1.0f, progress));
  const auto blend = [progress](float start, float end) {
    return start + ((end - start) * progress);
  };
  return {blend(target.start.red, this->active_sweep_target_.red),
          blend(target.start.green, this->active_sweep_target_.green),
          blend(target.start.blue, this->active_sweep_target_.blue),
          blend(target.start.white, this->active_sweep_target_.white)};
}

void CFXCCTSweeper::handle_short_press_() {
  const CCTShortPressAction action = select_cct_short_press_action(
      this->any_target_on_(), this->has_retained_state_,
      this->current_endpoint_(), this->last_endpoint_);
  if (action == CCTShortPressAction::RESTORE_RETAINED) {
    this->restore_retained_state_();
    return;
  }
  if (action == CCTShortPressAction::APPLY_NATIVE) {
    this->apply_color_to_all_(this->native_white_, 150);
    this->last_endpoint_ = CCTEndpoint::NATIVE;
    this->has_retained_state_ = true;
    return;
  }
  this->apply_color_to_all_(this->preferred_white_, 150);
  this->last_endpoint_ = CCTEndpoint::PREFERRED;
  this->has_retained_state_ = true;
}

void CFXCCTSweeper::restore_retained_state_() {
  for (auto *state : this->lights_) {
    if (state == nullptr) {
      continue;
    }
    auto call = state->make_call();
    call.set_transition_length(0);
    call.set_state(true);
    call.perform();
  }
}

void CFXCCTSweeper::apply_color_(light::LightState *state,
                                 const CFXColor &color,
                                 uint32_t transition_ms) {
  if (state == nullptr) {
    return;
  }
  const CFXColor c = this->clamp_color_(color);
  auto call = state->make_call();
  call.set_transition_length(transition_ms);
  call.set_state(true);
  call.set_effect("None");
  if (use_white_only_mode(c.red, c.green, c.blue, c.white) &&
      state->get_traits().supports_color_mode(light::ColorMode::WHITE)) {
    call.set_color_mode(light::ColorMode::WHITE);
    call.set_white(c.white);
  } else if (state->get_traits().supports_color_mode(
                 light::ColorMode::RGB_WHITE)) {
    call.set_color_mode(light::ColorMode::RGB_WHITE);
    call.set_rgb(c.red, c.green, c.blue);
    call.set_white(c.white);
  } else if (state->get_traits().supports_color_mode(light::ColorMode::RGB)) {
    call.set_color_mode(light::ColorMode::RGB);
    call.set_rgb(c.red, c.green, c.blue);
  } else if (state->get_traits().supports_color_mode(light::ColorMode::WHITE)) {
    call.set_color_mode(light::ColorMode::WHITE);
    call.set_white(std::max({c.red, c.green, c.blue, c.white}));
  }
  call.perform();
}

void CFXCCTSweeper::apply_color_to_all_(const CFXColor &color,
                                        uint32_t transition_ms) {
  for (auto *state : this->lights_) {
    this->apply_color_(state, color, transition_ms);
  }
}

void CFXCCTSweeper::save_preferred_white_() {
  if (!this->restore_ || !this->preferred_white_pref_ready_) {
    return;
  }
  StoredPreferredWhite stored{
      PREFERRED_WHITE_VERSION, this->preferred_white_.red,
      this->preferred_white_.green, this->preferred_white_.blue,
      this->preferred_white_.white};
  this->preferred_white_pref_.save(&stored);
}

void CFXCCTSweeper::save_direction_() {
  if (!this->restore_direction_ || !this->direction_pref_ready_) {
    return;
  }
  uint8_t stored = this->next_direction_warmer_ ? 1 : 0;
  this->direction_pref_.save(&stored);
}

bool CFXCCTSweeper::any_target_on_() const {
  for (auto *state : this->lights_) {
    if (state != nullptr && state->remote_values.is_on()) {
      return true;
    }
  }
  return false;
}

CCTEndpoint CFXCCTSweeper::current_endpoint_() const {
  CCTEndpoint endpoint = CCTEndpoint::NON_CCT;
  bool found = false;
  for (auto *state : this->lights_) {
    if (state == nullptr || !state->remote_values.is_on()) {
      return CCTEndpoint::NON_CCT;
    }
    CCTEndpoint current = CCTEndpoint::NON_CCT;
    if (this->matches_color_(state, this->native_white_)) {
      current = CCTEndpoint::NATIVE;
    } else if (this->matches_color_(state, this->preferred_white_)) {
      current = CCTEndpoint::PREFERRED;
    }
    if (current == CCTEndpoint::NON_CCT) {
      return current;
    }
    if (found && current != endpoint) {
      return CCTEndpoint::NON_CCT;
    }
    endpoint = current;
    found = true;
  }
  return found ? endpoint : CCTEndpoint::NON_CCT;
}

bool CFXCCTSweeper::matches_color_(light::LightState *state,
                                   const CFXColor &color) const {
  return state != nullptr && state->remote_values.is_on() &&
         this->color_distance_(this->remote_color_(state), color) <=
             WHITE_MATCH_TOLERANCE;
}

uint32_t CFXCCTSweeper::sweep_duration_ms_(const CFXColor &start,
                                           const CFXColor &target) const {
  if (this->sweep_time_ms_ == 0) {
    return 0;
  }
  const float full_distance =
      std::max(0.001f, this->color_distance_(this->warm_white_,
                                             this->cool_white_));
  const float distance = this->color_distance_(start, target);
  const uint32_t duration =
      static_cast<uint32_t>((distance / full_distance) * this->sweep_time_ms_);
  return std::max(MIN_SWEEP_TRANSITION_MS, duration);
}

CFXColor CFXCCTSweeper::sweep_start_color_(light::LightState *state) const {
  if (state == nullptr || !state->remote_values.is_on()) {
    return this->preferred_white_;
  }
  return this->remote_color_(state);
}

CFXColor CFXCCTSweeper::remote_color_(light::LightState *state) const {
  if (state == nullptr) {
    return this->preferred_white_;
  }
  if (state->remote_values.get_color_mode() == light::ColorMode::WHITE) {
    return {0.0f, 0.0f, 0.0f, state->remote_values.get_white()};
  }
  return {state->remote_values.get_red(), state->remote_values.get_green(),
          state->remote_values.get_blue(), state->remote_values.get_white()};
}

CFXColor CFXCCTSweeper::sweep_target_() const {
  return this->sweep_direction_warmer_ ? this->warm_white_ : this->cool_white_;
}

CFXColor CFXCCTSweeper::clamp_color_(const CFXColor &color) const {
  auto clamp = [](float value) { return std::max(0.0f, std::min(1.0f, value)); };
  return {clamp(color.red), clamp(color.green), clamp(color.blue),
          clamp(color.white)};
}

float CFXCCTSweeper::color_distance_(const CFXColor &a,
                                     const CFXColor &b) const {
  return std::sqrt(((a.red - b.red) * (a.red - b.red)) +
                   ((a.green - b.green) * (a.green - b.green)) +
                   ((a.blue - b.blue) * (a.blue - b.blue)) +
                   ((a.white - b.white) * (a.white - b.white)));
}

}  // namespace cfx_cct_sweeper
}  // namespace esphome
