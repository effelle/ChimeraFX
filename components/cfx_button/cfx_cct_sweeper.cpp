#include "cfx_cct_sweeper.h"

#include "cfx_dimmer_timing.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>

namespace esphome {
namespace cfx_cct_sweeper {

static const char *const TAG = "cfx_cct_sweeper";

static uint8_t to_u8_(float value) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= 1.0f) {
    return 255;
  }
  return static_cast<uint8_t>((value * 255.0f) + 0.5f);
}

static const char *color_mode_name(light::ColorMode mode) {
  switch (mode) {
    case light::ColorMode::WHITE:
      return "WHITE";
    case light::ColorMode::RGB:
      return "RGB";
    case light::ColorMode::RGB_WHITE:
      return "RGB_WHITE";
    default:
      return "OTHER";
  }
}

static const char *endpoint_name(CCTEndpoint endpoint) {
  switch (endpoint) {
    case CCTEndpoint::NATIVE:
      return "native";
    case CCTEndpoint::PREFERRED:
      return "preferred";
    case CCTEndpoint::NON_CCT:
    default:
      return "non_cct";
  }
}

static const char *short_action_name(CCTShortPressAction action) {
  switch (action) {
    case CCTShortPressAction::RESTORE_RETAINED:
      return "restore_retained";
    case CCTShortPressAction::APPLY_NATIVE:
      return "apply_native";
    case CCTShortPressAction::APPLY_PREFERRED:
    default:
      return "apply_preferred";
  }
}

void CFXCCTSweeper::setup() {
  this->log_configured_colors_();
  if (global_preferences == nullptr) {
    return;
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
      ESP_LOGV(TAG,
               "[%s] restored preferred RGBW=%.3f/%.3f/%.3f/%.3f",
               this->id_.c_str(), this->preferred_white_.red,
               this->preferred_white_.green, this->preferred_white_.blue,
               this->preferred_white_.white);
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
  this->sweep_end_ms_ = now;
  this->sweep_started_ms_ = now;
  this->active_sweep_target_ = this->sweep_target_();
  this->emit_sync_color_(this->active_sweep_target_, this->sweep_time_ms_);
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
  this->emit_sync_color_(this->preferred_white_, 0);
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
  this->emit_sync_color_(this->preferred_white_, 0);
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
  for (auto *state : this->lights_) {
    this->log_light_state_(state, "short input");
  }
  const CCTEndpoint current_endpoint = this->current_endpoint_();
  const CCTShortPressAction action = select_cct_short_press_action(
      this->any_target_on_(), this->has_retained_state_,
      current_endpoint, this->last_endpoint_);
  ESP_LOGV(TAG,
           "[%s] short decision current=%s(%u) last=%s(%u) retained=%s "
           "action=%s(%u)",
           this->id_.c_str(), endpoint_name(current_endpoint),
           static_cast<unsigned>(current_endpoint),
           endpoint_name(this->last_endpoint_),
           static_cast<unsigned>(this->last_endpoint_),
           YESNO(this->has_retained_state_), short_action_name(action),
           static_cast<unsigned>(action));
  if (action == CCTShortPressAction::RESTORE_RETAINED) {
    this->restore_retained_state_();
    return;
  }
  if (action == CCTShortPressAction::APPLY_NATIVE) {
    this->emit_sync_color_(this->native_white_, USE_DEFAULT_TRANSITION);
    this->apply_color_to_all_(this->native_white_, USE_DEFAULT_TRANSITION);
    this->last_endpoint_ = CCTEndpoint::NATIVE;
    this->has_retained_state_ = true;
    return;
  }
  this->emit_sync_color_(this->preferred_white_, USE_DEFAULT_TRANSITION);
  this->apply_color_to_all_(this->preferred_white_, USE_DEFAULT_TRANSITION);
  this->last_endpoint_ = CCTEndpoint::PREFERRED;
  this->has_retained_state_ = true;
}

void CFXCCTSweeper::restore_retained_state_() {
  for (auto *state : this->lights_) {
    if (state == nullptr) {
      continue;
    }
    auto call = state->make_call();
    call.set_state(true);
    call.perform();
  }
}

void CFXCCTSweeper::emit_sync_color_(const CFXColor &color,
                                     uint32_t transition_ms) {
  const CFXColor c = this->clamp_color_(color);
  const CCTRGBCommand rgb = split_cct_rgb(c.red, c.green, c.blue);
  cfx_button::CFXButtonSyncCommand command;
  command.kind = cfx_button::CFXButtonSyncKind::CCT;
  command.pressed = true;
  command.has_rgb = true;
  command.red = to_u8_(rgb.red);
  command.green = to_u8_(rgb.green);
  command.blue = to_u8_(rgb.blue);
  command.has_white = true;
  command.white = to_u8_(c.white);
  command.has_color_brightness = true;
  command.color_brightness = to_u8_(rgb.color_brightness);
  if (transition_ms != USE_DEFAULT_TRANSITION) {
    command.has_ramp = true;
    command.ramp_ms = static_cast<uint16_t>(
        std::min<uint32_t>(transition_ms, UINT16_MAX));
  }
  for (auto &callback : this->sync_command_callbacks_) {
    callback(command);
  }
}

void CFXCCTSweeper::apply_color_(light::LightState *state,
                                 const CFXColor &color,
                                 uint32_t transition_ms) {
  if (state == nullptr) {
    return;
  }
  const CFXColor c = this->clamp_color_(color);
  const CCTRGBCommand rgb = split_cct_rgb(c.red, c.green, c.blue);
  const bool white_only =
      use_white_only_mode(c.red, c.green, c.blue, c.white) &&
      state->get_traits().supports_color_mode(light::ColorMode::WHITE);
  const bool use_default_transition =
      transition_ms == USE_DEFAULT_TRANSITION;
  const uint32_t effective_transition_ms =
      use_default_transition ? 0 : cct_transition_ms(white_only, transition_ms);
  const light::ColorMode command_mode =
      white_only ? light::ColorMode::WHITE
                 : (state->get_traits().supports_color_mode(
                        light::ColorMode::RGB_WHITE)
                        ? light::ColorMode::RGB_WHITE
                        : (state->get_traits().supports_color_mode(
                               light::ColorMode::RGB)
                               ? light::ColorMode::RGB
                               : light::ColorMode::WHITE));
  ESP_LOGV(TAG,
           "[%s] command light='%s' mode=%s(%u) transition=%ums "
           "RGBW=%.3f/%.3f/%.3f/%.3f color_brightness=%.3f",
           this->id_.c_str(), state->get_name().c_str(),
           color_mode_name(command_mode),
           static_cast<unsigned>(command_mode), effective_transition_ms, c.red,
           c.green, c.blue, c.white, rgb.color_brightness);
  auto call = state->make_call();
  if (use_default_transition && white_only) {
    // ESPHome's addressable default transformer snapshots the previous RGB
    // channels. Native white must switch immediately to keep RGB fully off.
    call.set_transition_length(0);
    cfx_dimmer::clear_light_timing_hint(state);
  } else if (!use_default_transition) {
    call.set_transition_length(effective_transition_ms);
    if (effective_transition_ms > 0) {
      cfx_dimmer::publish_light_ramp_hint(state,
                                          millis() + effective_transition_ms);
    } else {
      cfx_dimmer::clear_light_timing_hint(state);
    }
  } else {
    cfx_dimmer::clear_light_timing_hint(state);
  }
  call.set_state(true);
  call.set_effect("None");
  if (white_only) {
    call.set_color_mode(light::ColorMode::WHITE);
    call.set_white(c.white);
  } else if (state->get_traits().supports_color_mode(
                 light::ColorMode::RGB_WHITE)) {
    call.set_color_mode(light::ColorMode::RGB_WHITE);
    call.set_color_brightness(rgb.color_brightness);
    call.set_rgb(rgb.red, rgb.green, rgb.blue);
    call.set_white(c.white);
  } else if (state->get_traits().supports_color_mode(light::ColorMode::RGB)) {
    call.set_color_mode(light::ColorMode::RGB);
    call.set_color_brightness(rgb.color_brightness);
    call.set_rgb(rgb.red, rgb.green, rgb.blue);
  } else if (state->get_traits().supports_color_mode(light::ColorMode::WHITE)) {
    call.set_color_mode(light::ColorMode::WHITE);
    call.set_white(std::max({c.red, c.green, c.blue, c.white}));
  }
  call.perform();
  this->log_light_state_(state, "command applied");
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

void CFXCCTSweeper::log_configured_colors_() const {
  ESP_LOGV(TAG,
           "[%s] configured native RGBW=%.3f/%.3f/%.3f/%.3f "
           "preferred RGBW=%.3f/%.3f/%.3f/%.3f restore=%s",
           this->id_.c_str(), this->native_white_.red, this->native_white_.green,
           this->native_white_.blue, this->native_white_.white,
           this->preferred_white_.red, this->preferred_white_.green,
           this->preferred_white_.blue, this->preferred_white_.white,
           YESNO(this->restore_));
}

void CFXCCTSweeper::log_light_state_(light::LightState *state,
                                     const char *context) const {
  if (state == nullptr) {
    ESP_LOGV(TAG, "[%s] %s light=<null>", this->id_.c_str(), context);
    return;
  }
  const auto &values = state->remote_values;
  ESP_LOGV(TAG,
           "[%s] %s light='%s' on=%s mode=%s(%u) brightness=%.3f "
           "color_brightness=%.3f RGBW=%.3f/%.3f/%.3f/%.3f effect='%s'",
           this->id_.c_str(), context, state->get_name().c_str(),
           YESNO(values.is_on()), color_mode_name(values.get_color_mode()),
           static_cast<unsigned>(values.get_color_mode()),
           values.get_brightness(), values.get_color_brightness(),
           values.get_red(), values.get_green(), values.get_blue(),
           values.get_white(), state->get_effect_name().c_str());
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
  if (sweep_uses_preferred_start(
          this->matches_color_(state, this->native_white_)
              ? CCTEndpoint::NATIVE
              : CCTEndpoint::NON_CCT)) {
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
  const float color_brightness =
      state->remote_values.get_color_brightness();
  return {color_brightness * state->remote_values.get_red(),
          color_brightness * state->remote_values.get_green(),
          color_brightness * state->remote_values.get_blue(),
          state->remote_values.get_white()};
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
