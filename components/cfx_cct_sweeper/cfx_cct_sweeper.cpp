#include "cfx_cct_sweeper.h"

#include "esphome/core/helpers.h"
#include <algorithm>
#include <cmath>

namespace esphome {
namespace cfx_cct_sweeper {

void CFXCCTSweeper::setup() {
  if (!this->restore_direction_ || global_preferences == nullptr) {
    return;
  }
  const std::string key = "cfx_cct_sweeper_" + this->id_ + "_direction";
  this->direction_pref_ =
      global_preferences->make_preference<uint8_t>(fnv1a_hash(key.c_str()),
                                                   true);
  this->direction_pref_ready_ = true;
  uint8_t restored = 1;
  if (this->direction_pref_.load(&restored)) {
    this->next_direction_warmer_ = restored != 0;
  }
}

void CFXCCTSweeper::add_light(light::LightState *state) {
  this->lights_.push_back(state);
  this->saved_colors_.push_back({});
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
  this->sweeping_ = false;
  this->sweep_finished_ = false;
  this->suppress_toggle_ = false;
  this->press_started_ms_ = now;
  this->sweep_end_ms_ = 0;
}

void CFXCCTSweeper::release() {
  if (!this->pressed_) {
    return;
  }
  if (this->sweeping_) {
    this->freeze_sweep_();
  }
  if (this->suppress_toggle_) {
    this->ignore_press_until_ms_ = millis() + POST_SWEEP_GUARD_MS;
  }
  const bool should_toggle = !this->suppress_toggle_ && !this->sweeping_;
  this->pressed_ = false;
  this->sweeping_ = false;
  this->sweep_finished_ = false;
  this->suppress_toggle_ = false;
  this->sweep_end_ms_ = 0;

  if (should_toggle) {
    this->toggle_favorite_white_();
  }
}

void CFXCCTSweeper::loop() {
  if (!this->pressed_ || this->sweep_finished_) {
    return;
  }
  const uint32_t now = millis();
  if (!this->sweeping_) {
    if ((now - this->press_started_ms_) < this->long_press_ms_) {
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
  this->save_current_colors_();
  this->sweeping_ = true;
  this->sweep_finished_ = false;
  this->suppress_toggle_ = true;
  this->sweep_direction_warmer_ = this->next_direction_warmer_;
  this->next_direction_warmer_ = !this->sweep_direction_warmer_;
  this->save_direction_();
  this->sweep_end_ms_ = now;
  const CFXColor target = this->sweep_target_();
  for (auto *state : this->lights_) {
    const CFXColor start = this->current_color_(state);
    const uint32_t duration = this->sweep_duration_ms_(start, target);
    this->sweep_end_ms_ = std::max(this->sweep_end_ms_, now + duration);
    this->apply_color_(state, target, duration);
  }
}

void CFXCCTSweeper::finish_sweep_() {
  if (!this->sweeping_) {
    return;
  }
  this->sweeping_ = false;
  this->sweep_finished_ = true;
  const CFXColor target = this->sweep_target_();
  for (auto *state : this->lights_) {
    this->apply_color_(state, target, 0);
  }
}

void CFXCCTSweeper::freeze_sweep_() {
  if (!this->sweeping_) {
    return;
  }
  for (auto *state : this->lights_) {
    this->apply_color_(state, this->current_color_(state), 0);
  }
  this->sweeping_ = false;
  this->sweep_finished_ = true;
}

void CFXCCTSweeper::toggle_favorite_white_() {
  const bool restore = !this->lights_.empty() &&
                       this->matches_favorite_white_(this->lights_[0]);
  if (!restore) {
    this->save_current_colors_();
  }
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *state = this->lights_[i];
    if (restore) {
      this->restore_saved_color_(i, state);
    } else {
      this->apply_color_(state, this->favorite_white_, 150);
    }
  }
}

void CFXCCTSweeper::save_current_colors_() {
  if (this->saved_colors_.size() < this->lights_.size()) {
    this->saved_colors_.resize(this->lights_.size());
  }
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *state = this->lights_[i];
    if (state == nullptr || !state->remote_values.is_on()) {
      continue;
    }
    this->saved_colors_[i].valid = true;
    this->saved_colors_[i].color = this->remote_color_(state);
  }
}

void CFXCCTSweeper::restore_saved_color_(size_t index,
                                         light::LightState *state) {
  if (index < this->saved_colors_.size() && this->saved_colors_[index].valid) {
    this->apply_color_(state, this->saved_colors_[index].color, 150);
    return;
  }
  this->apply_color_(state, this->warm_white_, 150);
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
  if (state->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE)) {
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

void CFXCCTSweeper::save_direction_() {
  if (!this->restore_direction_ || !this->direction_pref_ready_) {
    return;
  }
  uint8_t stored = this->next_direction_warmer_ ? 1 : 0;
  this->direction_pref_.save(&stored);
}

bool CFXCCTSweeper::matches_favorite_white_(light::LightState *state) const {
  if (state == nullptr || !state->remote_values.is_on()) {
    return false;
  }
  return this->color_distance_(this->remote_color_(state),
                               this->favorite_white_) <= WHITE_MATCH_TOLERANCE;
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

CFXColor CFXCCTSweeper::current_color_(light::LightState *state) const {
  if (state == nullptr) {
    return this->favorite_white_;
  }
  return {state->current_values.get_red(), state->current_values.get_green(),
          state->current_values.get_blue(), state->current_values.get_white()};
}

CFXColor CFXCCTSweeper::remote_color_(light::LightState *state) const {
  if (state == nullptr) {
    return this->favorite_white_;
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
