#include "cfx_hue_cycler.h"

#include "esphome/core/helpers.h"
#include <algorithm>
#include <cmath>

namespace esphome {
namespace cfx_hue_cycler {

void CFXHueCycler::setup() {
  if (!this->restore_ || global_preferences == nullptr) {
    return;
  }
  const bool palette_mode = !this->colors_.empty();
  const std::string key = "cfx_hue_cycler_" + this->id_ +
                          (palette_mode ? "_color_index" : "_hue");
  if (palette_mode) {
    this->selection_pref_ =
        global_preferences->make_preference<uint32_t>(
            fnv1a_hash(key.c_str()), true);
    this->selection_pref_ready_ = true;
    uint32_t restored = 0;
    if (this->selection_pref_.load(&restored) &&
        restored < this->colors_.size()) {
      this->active_color_index_ = restored;
      this->active_color_known_ = true;
    }
  } else {
    this->selection_pref_ =
        global_preferences->make_preference<float>(fnv1a_hash(key.c_str()),
                                                   true);
    this->selection_pref_ready_ = true;
    float restored = 0.0f;
    if (this->selection_pref_.load(&restored)) {
      this->base_hue_ = this->normalize_hue_(restored);
    }
  }
}

void CFXHueCycler::add_light(light::LightState *state) {
  this->lights_.push_back(state);
  this->saved_colors_.push_back({});
}

void CFXHueCycler::press() {
  if (this->pressed_) {
    return;
  }
  const uint32_t now = millis();
  if ((int32_t) (now - this->ignore_press_until_ms_) < 0) {
    this->ignore_press_until_ms_ = now + POST_CYCLE_GUARD_MS;
    return;
  }
  this->pressed_ = true;
  this->cycling_ = false;
  this->suppress_toggle_ = false;
  this->press_started_ms_ = now;
  this->cycle_started_ms_ = 0;
  this->last_cycle_update_ms_ = 0;
}

void CFXHueCycler::release() {
  const uint32_t now = millis();
  if (!this->pressed_) {
    if ((int32_t) (now - this->ignore_press_until_ms_) < 0) {
      this->ignore_press_until_ms_ = now + POST_CYCLE_GUARD_MS;
    }
    return;
  }
  if (this->cycling_) {
    this->freeze_cycle_();
  }
  const bool should_toggle = !this->suppress_toggle_ && !this->cycling_;
  this->pressed_ = false;
  this->cycling_ = false;
  this->suppress_toggle_ = false;
  this->cycle_started_ms_ = 0;
  this->last_cycle_update_ms_ = 0;

  if (should_toggle) {
    this->toggle_fixed_color_();
  }
  this->ignore_press_until_ms_ = millis() + POST_CYCLE_GUARD_MS;
}

void CFXHueCycler::loop() {
  if (!this->pressed_) {
    return;
  }
  const uint32_t now = millis();
  if (!this->cycling_) {
    if ((now - this->press_started_ms_) < this->long_press_ms_) {
      return;
    }
    this->start_cycle_(now);
    return;
  }
  this->apply_cycle_(now);
}

void CFXHueCycler::start_cycle_(uint32_t now) {
  this->cycling_ = true;
  this->suppress_toggle_ = true;
  this->cycle_started_ms_ = now;
  this->last_cycle_update_ms_ = 0;
  if (this->colors_.empty()) {
    this->apply_cycle_(now);
  } else {
    this->select_next_palette_color_(now);
  }
}

void CFXHueCycler::apply_cycle_(uint32_t now) {
  if (!this->colors_.empty()) {
    if ((now - this->last_cycle_update_ms_) >= this->color_interval_ms_) {
      this->select_next_palette_color_(now);
    }
    return;
  }
  if (this->last_cycle_update_ms_ != 0 &&
      (now - this->last_cycle_update_ms_) < HUE_UPDATE_INTERVAL_MS) {
    return;
  }
  this->last_cycle_update_ms_ = now;
  const CFXColor color = this->cycle_color_at_(now);
  for (auto *state : this->lights_) {
    this->apply_color_(state, color, HUE_TRANSITION_MS);
  }
}

void CFXHueCycler::select_next_palette_color_(uint32_t now) {
  if (this->colors_.empty()) {
    return;
  }
  this->active_color_index_ = next_palette_index(
      this->active_color_index_, this->colors_.size(),
      this->active_color_known_);
  this->active_color_known_ = true;
  this->last_cycle_update_ms_ = now;
  this->last_cycle_color_ = this->colors_[this->active_color_index_];
  for (auto *state : this->lights_) {
    this->apply_color_(state, this->last_cycle_color_, HUE_TRANSITION_MS);
  }
  this->save_selection_();
}

CFXColor CFXHueCycler::cycle_color_at_(uint32_t now) {
  float hue = this->base_hue_;
  if (this->cycle_time_ms_ > 0) {
    const float progress =
        static_cast<float>(now - this->cycle_started_ms_) /
        static_cast<float>(this->cycle_time_ms_);
    hue = this->normalize_hue_(this->base_hue_ + (progress * 360.0f));
  }
  this->last_cycle_hue_ = hue;
  this->last_cycle_color_ = this->color_from_hue_(hue);
  return this->last_cycle_color_;
}

void CFXHueCycler::freeze_cycle_() {
  if (!this->cycling_) {
    return;
  }
  if (this->colors_.empty()) {
    this->base_hue_ = this->last_cycle_hue_;
  }
  if (this->saved_colors_.size() < this->lights_.size()) {
    this->saved_colors_.resize(this->lights_.size());
  }
  this->save_selection_();
  for (size_t i = 0; i < this->lights_.size(); i++) {
    this->saved_colors_[i].valid = true;
    this->saved_colors_[i].color = this->last_cycle_color_;
    auto *state = this->lights_[i];
    this->apply_color_(state, this->last_cycle_color_, 0);
  }
  this->cycling_ = false;
}

void CFXHueCycler::toggle_fixed_color_() {
  const ShortPressOutput output = next_short_press_output(
      this->all_targets_match_fixed_color_(),
      this->all_targets_match_selected_hue_());
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *state = this->lights_[i];
    if (output == ShortPressOutput::SELECTED_HUE) {
      this->restore_saved_color_(i, state);
    } else {
      this->apply_color_(state, this->fixed_color_, USE_DEFAULT_TRANSITION);
    }
  }
}

void CFXHueCycler::restore_saved_color_(size_t index, light::LightState *state) {
  if (index < this->saved_colors_.size() &&
      this->saved_colors_[index].valid) {
    this->apply_color_(state, this->saved_colors_[index].color,
                       USE_DEFAULT_TRANSITION);
    return;
  }
  const CFXColor fallback = this->fallback_color_();
  if (index < this->saved_colors_.size()) {
    this->saved_colors_[index].valid = true;
    this->saved_colors_[index].color = fallback;
  }
  this->apply_color_(state, fallback, USE_DEFAULT_TRANSITION);
}

void CFXHueCycler::apply_color_(light::LightState *state, const CFXColor &color,
                                uint32_t transition_ms) {
  if (state == nullptr) {
    return;
  }
  const CFXColor c = this->clamp_color_(color);
  const float color_brightness = std::max({c.red, c.green, c.blue});
  const float divisor = color_brightness > 0.0f ? color_brightness : 1.0f;
  auto call = state->make_call();
  if (transition_ms != USE_DEFAULT_TRANSITION) {
    call.set_transition_length(transition_ms);
  }
  call.set_state(true);
  call.set_effect("None");
  if (state->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE)) {
    call.set_color_mode(light::ColorMode::RGB_WHITE);
    call.set_color_brightness(color_brightness);
    call.set_rgb(c.red / divisor, c.green / divisor, c.blue / divisor);
    call.set_white(c.white);
  } else if (state->get_traits().supports_color_mode(light::ColorMode::RGB)) {
    call.set_color_mode(light::ColorMode::RGB);
    call.set_color_brightness(color_brightness);
    call.set_rgb(c.red / divisor, c.green / divisor, c.blue / divisor);
  } else if (state->get_traits().supports_color_mode(light::ColorMode::WHITE)) {
    call.set_color_mode(light::ColorMode::WHITE);
    call.set_white(std::max({c.red, c.green, c.blue, c.white}));
  }
  call.perform();
}

void CFXHueCycler::save_selection_() {
  if (!this->restore_ || !this->selection_pref_ready_) {
    return;
  }
  if (!this->colors_.empty()) {
    uint32_t stored = static_cast<uint32_t>(this->active_color_index_);
    this->selection_pref_.save(&stored);
  } else {
    float stored = this->base_hue_;
    this->selection_pref_.save(&stored);
  }
}

bool CFXHueCycler::all_targets_match_fixed_color_() const {
  if (this->lights_.empty()) {
    return false;
  }
  for (auto *state : this->lights_) {
    if (!this->matches_color_(state, this->fixed_color_)) {
      return false;
    }
  }
  return true;
}

bool CFXHueCycler::all_targets_match_selected_hue_() const {
  if (this->lights_.empty()) {
    return false;
  }
  for (size_t i = 0; i < this->lights_.size(); i++) {
    if (!this->matches_color_(this->lights_[i], this->selected_color_(i))) {
      return false;
    }
  }
  return true;
}

bool CFXHueCycler::matches_color_(light::LightState *state,
                                 const CFXColor &color) const {
  if (state == nullptr || !state->remote_values.is_on()) {
    return false;
  }
  return this->color_distance_(this->remote_color_(state), color) <=
         COLOR_MATCH_TOLERANCE;
}

CFXColor CFXHueCycler::selected_color_(size_t index) const {
  if (index < this->saved_colors_.size() &&
      this->saved_colors_[index].valid) {
    return this->saved_colors_[index].color;
  }
  return this->fallback_color_();
}

CFXColor CFXHueCycler::fallback_color_() const {
  if (!this->colors_.empty()) {
    const size_t index =
        this->active_color_known_ ? this->active_color_index_ : 0;
    return this->colors_[index];
  }
  return this->color_from_hue_(this->base_hue_);
}

CFXColor CFXHueCycler::remote_color_(light::LightState *state) const {
  if (state == nullptr) {
    return this->fixed_color_;
  }
  const float color_brightness =
      state->remote_values.get_color_brightness();
  return {color_brightness * state->remote_values.get_red(),
          color_brightness * state->remote_values.get_green(),
          color_brightness * state->remote_values.get_blue(),
          state->remote_values.get_white()};
}

CFXColor CFXHueCycler::color_from_hue_(float hue) const {
  hue = this->normalize_hue_(hue);
  const float c = this->saturation_;
  const float x = c * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  if (hue < 60.0f) {
    r = c;
    g = x;
  } else if (hue < 120.0f) {
    r = x;
    g = c;
  } else if (hue < 180.0f) {
    g = c;
    b = x;
  } else if (hue < 240.0f) {
    g = x;
    b = c;
  } else if (hue < 300.0f) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }
  const float m = 1.0f - c;
  return {r + m, g + m, b + m, 0.0f};
}

CFXColor CFXHueCycler::clamp_color_(const CFXColor &color) const {
  auto clamp = [](float value) { return std::max(0.0f, std::min(1.0f, value)); };
  return {clamp(color.red), clamp(color.green), clamp(color.blue),
          clamp(color.white)};
}

float CFXHueCycler::normalize_hue_(float hue) const {
  while (hue < 0.0f) {
    hue += 360.0f;
  }
  while (hue >= 360.0f) {
    hue -= 360.0f;
  }
  return hue;
}

float CFXHueCycler::color_distance_(const CFXColor &a,
                                    const CFXColor &b) const {
  return std::sqrt(((a.red - b.red) * (a.red - b.red)) +
                   ((a.green - b.green) * (a.green - b.green)) +
                   ((a.blue - b.blue) * (a.blue - b.blue)) +
                   ((a.white - b.white) * (a.white - b.white)));
}

}  // namespace cfx_hue_cycler
}  // namespace esphome
