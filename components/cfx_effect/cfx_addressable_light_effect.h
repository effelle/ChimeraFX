/*
 * ChimeraFX - WLED Effects for ESPHome
 * Copyright (c) 2026 Federico Leoni
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

  select::Select *get_intro_effect() { return this->intro_effect_; }

  enum IntroMode {
    INTRO_MODE_NONE = 0,
    INTRO_MODE_WIPE = 1,
    INTRO_MODE_FADE = 2,
    INTRO_MODE_CENTER = 3,
    INTRO_MODE_GLITTER = 4
  };

  void run_intro(light::AddressableLight &it, const Color &target_color);
  bool intro_active_{false};
  uint8_t active_intro_mode_{0};
  uint32_t intro_start_time_{0};

  void set_speed_preset(uint8_t v) { this->speed_preset_ = v; }
  void set_intensity_preset(uint8_t v) { this->intensity_preset_ = v; }
  void set_palette_preset(uint8_t v) { this->palette_preset_ = v; }
  void set_mirror_preset(bool v) { this->mirror_preset_ = v; }

  void set_controller(CFXControl *controller) {
    this->controller_ = controller;
  }

protected:
  uint8_t effect_id_{0};
  number::Number *speed_{nullptr};
  number::Number *intensity_{nullptr};
  select::Select *palette_{nullptr};
  switch_::Switch *mirror_{nullptr};
  select::Select *transition_effect_{nullptr};
  number::Number *transition_duration_{nullptr};
  select::Select *intro_effect_{nullptr};
  number::Number *intro_duration_{nullptr};
  switch_::Switch *intro_use_palette_{nullptr};

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

  optional<uint8_t> speed_preset_{};
  optional<uint8_t> intensity_preset_{};
  optional<uint8_t> palette_preset_{};
  optional<bool> mirror_preset_{};

  CFXControl *controller_{nullptr};
  void run_controls_();

  bool intro_pending_{false};
};

} // namespace chimera_fx
} // namespace esphome
