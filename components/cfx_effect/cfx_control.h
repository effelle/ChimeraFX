/*
 * ChimeraFX - WLED Effects for ESPHome
 * Copyright (c) 2026 Federico Leoni
 * Based on WLED by Aircoookie (https://github.com/wled/WLED)
 *
 * Licensed under the EUPL-1.2
 */

#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace chimera_fx {

class CFXNumber : public number::Number {
public:
  void control(float value) override { this->publish_state(value); }
};

class CFXSelect : public select::Select {
public:
  void control(const std::string &value) override {
    this->publish_state(value);
  }
};

class CFXSwitch : public switch_::Switch {
public:
  void write_state(bool state) override { this->publish_state(state); }
};

class CFXControl : public Component {
public:
  static std::vector<CFXControl *> instances;

  static CFXControl *find(light::LightState *light) {
    for (auto *c : instances) {
      if (c->light_ == light)
        return c;
    }
    return nullptr;
  }

  void setup() override {
    instances.push_back(this);
    if (this->timer_ != nullptr) {
      this->set_interval("cfx_timer", 60000,
                         [this]() { this->on_timer_tick_(); });
    }
  }

  void set_speed(number::Number *n) { speed_ = n; }
  void set_intensity(number::Number *n) { intensity_ = n; }
  void set_palette(select::Select *s) { palette_ = s; }
  void set_mirror(esphome::switch_::Switch *s) { mirror_ = s; }
  void set_intro_effect(select::Select *s) { intro_effect_ = s; }
  void set_intro_duration(number::Number *n) { intro_duration_ = n; }
  void set_intro_use_palette(esphome::switch_::Switch *s) {
    intro_use_palette_ = s;
  }
  void set_timer(number::Number *n) { timer_ = n; }
  void set_light(esphome::light::LightState *light) { light_ = light; }

  number::Number *get_speed() { return speed_; }
  number::Number *get_intensity() { return intensity_; }
  select::Select *get_palette() { return palette_; }
  esphome::switch_::Switch *get_mirror() { return mirror_; }
  select::Select *get_intro_effect() { return intro_effect_; }
  number::Number *get_intro_duration() { return intro_duration_; }
  esphome::switch_::Switch *get_intro_use_palette() {
    return intro_use_palette_;
  }

protected:
  number::Number *speed_{nullptr};
  number::Number *intensity_{nullptr};
  select::Select *palette_{nullptr};
  esphome::switch_::Switch *mirror_{nullptr};
  select::Select *intro_effect_{nullptr};
  number::Number *intro_duration_{nullptr};
  esphome::switch_::Switch *intro_use_palette_{nullptr};
  number::Number *timer_{nullptr};

  esphome::light::LightState *light_{nullptr};

  void on_timer_tick_() {
    if (timer_ == nullptr || light_ == nullptr)
      return;

    float val = timer_->state;
    if (val > 0) {
      val -= 1.0f;
      if (val <= 0) {
        val = 0;
        auto call = light_->turn_off();
        call.perform();
      }
      timer_->publish_state(val);
    }
  }
};

} // namespace chimera_fx
} // namespace esphome
