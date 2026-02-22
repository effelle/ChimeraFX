/*
 * ChimeraFX - WLED Effects for ESPHome
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Based on WLED by Aircoookie (https://github.com/wled/WLED)
 *
 * Licensed under the EUPL-1.2
 */

#pragma once

#include "CFXRunner.h"
#include "cfx_control.h"
#include "esphome/components/light/addressable_light_effect.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include <vector>

namespace esphome {
namespace chimera_fx {

class CFXAddressableLightEffect : public light::AddressableLightEffect {
public:
  CFXAddressableLightEffect(const char *name);

  void start() override;
  void stop() override;
  void apply(light::AddressableLight &it, const Color &current_color) override;

  void set_effect_id(uint8_t effect_id) { this->effect_id_ = effect_id; }
  void set_speed(number::Number *speed) { this->speed_ = speed; }
  void set_intensity(number::Number *intensity) {
    this->intensity_ = intensity;
  }
  void set_palette(select::Select *palette) { this->palette_ = palette; }
  void set_mirror(switch_::Switch *mirror) { this->mirror_ = mirror; }
  void set_autotune(switch_::Switch *autotune) { this->autotune_ = autotune; }
  void set_force_white(switch_::Switch *force_white) {
    this->force_white_ = force_white;
  }
  void set_update_interval(uint32_t update_interval) {
    this->update_interval_ = update_interval;
  }
  void set_transition_effect(select::Select *s) {
    this->transition_effect_ = s;
  }
  void set_transition_duration(number::Number *n) {
    this->transition_duration_ = n;
  }
  void set_intro_effect(select::Select *s) { this->intro_effect_ = s; }
  void set_intro_duration(number::Number *n) { this->intro_duration_ = n; }
  void set_intro_use_palette(switch_::Switch *s) {
    this->intro_use_palette_ = s;
  }
  void set_outro_effect(select::Select *s) { this->outro_effect_ = s; }
  void set_outro_duration(number::Number *n) { this->outro_duration_ = n; }
  void set_debug(switch_::Switch *s) { this->debug_switch_ = s; }

  select::Select *get_intro_effect() { return this->intro_effect_; }

  enum IntroMode {
    INTRO_MODE_NONE = 0,
    INTRO_MODE_WIPE = 1,
    INTRO_MODE_FADE = 2,
    INTRO_MODE_CENTER = 3,
    INTRO_MODE_GLITTER = 4
  };

  void run_intro(light::AddressableLight &it, const Color &target_color);
  bool run_outro_frame(light::AddressableLight &it, CFXRunner *runner);

  bool intro_active_{false};
  uint8_t active_intro_mode_{0};
  uint32_t intro_start_time_{0};

  bool outro_active_{false};
  uint8_t active_outro_mode_{0};
  uint32_t active_outro_duration_ms_{1500};
  uint8_t active_outro_intensity_{128};
  uint32_t outro_start_time_{0};

  void set_speed_preset(uint8_t v) { this->speed_preset_ = v; }
  void set_intro_preset(uint8_t v) { this->intro_preset_ = v; }
  void set_intro_duration_preset(float v) { this->intro_duration_preset_ = v; }
  void set_intro_use_palette_preset(bool v) {
    this->intro_use_palette_preset_ = v;
  }
  void set_outro_preset(uint8_t v) { this->outro_preset_ = v; }
  void set_outro_duration_preset(float v) { this->outro_duration_preset_ = v; }
  void set_timer_preset(uint16_t v) { this->timer_preset_ = v; }
  void set_intensity_preset(uint8_t v) { this->intensity_preset_ = v; }
  void set_palette_preset(uint8_t v) { this->palette_preset_ = v; }
  void set_mirror_preset(bool v) { this->mirror_preset_ = v; }
  void set_autotune_preset(bool v) { this->autotune_preset_ = v; }
  void set_force_white_preset(bool preset) {
    this->force_white_preset_ = preset;
  }

  void set_controller(CFXControl *controller) {
    this->controller_ = controller;
  }

protected:
  uint8_t effect_id_{0};
  number::Number *speed_{nullptr};
  number::Number *intensity_{nullptr};
  select::Select *palette_{nullptr};
  switch_::Switch *mirror_{nullptr};
  switch_::Switch *autotune_{nullptr};
  switch_::Switch *force_white_{nullptr};
  select::Select *transition_effect_{nullptr};
  number::Number *transition_duration_{nullptr};
  select::Select *intro_effect_{nullptr};
  number::Number *intro_duration_{nullptr};
  switch_::Switch *intro_use_palette_{nullptr};
  select::Select *outro_effect_{nullptr};
  number::Number *outro_duration_{nullptr};
  switch_::Switch *debug_switch_{nullptr};

  enum TransitionState {
    TRANSITION_NONE,
    TRANSITION_ENTRY,
    TRANSITION_EXIT,
    TRANSITION_RUNNING
  };
  TransitionState state_{TRANSITION_NONE};
  uint32_t transition_start_ms_{0};
  std::vector<Color> intro_snapshot_;

  CFXRunner *runner_{nullptr};

  uint32_t update_interval_{16};
  uint32_t last_run_{0};

  uint8_t get_palette_index_();
  uint8_t get_default_palette_id_(uint8_t effect_id);
  std::string get_palette_name_(uint8_t pal_id);
  uint8_t get_default_speed_(uint8_t effect_id);
  uint8_t get_default_intensity_(uint8_t effect_id);

  optional<uint8_t> speed_preset_{};
  optional<uint8_t> intensity_preset_{};
  optional<uint8_t> palette_preset_{};
  optional<bool> mirror_preset_{};
  optional<bool> autotune_preset_{};
  optional<bool> force_white_preset_{};
  optional<uint8_t> intro_preset_{};
  optional<float> intro_duration_preset_{};
  optional<bool> intro_use_palette_preset_{};
  optional<uint8_t> outro_preset_{};
  optional<float> outro_duration_preset_{};
  optional<uint16_t> timer_preset_{};

  CFXControl *controller_{nullptr};
  void run_controls_();

  bool intro_pending_{false};

  // INTRO_NONE fade-in: ramp brightness from 0→1 over default_transition_length
  uint32_t fade_in_duration_ms_{0};
  uint32_t fade_in_start_ms_{0};
  bool fade_in_active_{false};

  bool initial_preset_applied_{false};
};

} // namespace chimera_fx
} // namespace esphome
