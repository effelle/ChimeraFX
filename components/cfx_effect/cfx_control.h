#pragma once

#include "CFXRunner.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "cfx_addressable_light_effect.h"

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
      if (c->get_light() == light)
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

    if (this->speed_) {
      this->speed_->add_on_state_callback([this](float value) {
        for (auto *r : this->runners_)
          r->setSpeed((uint8_t)value);
      });
    }

    if (this->intensity_) {
      this->intensity_->add_on_state_callback([this](float value) {
        for (auto *r : this->runners_)
          r->setIntensity((uint8_t)value);
      });
    }

    if (this->mirror_) {
      this->mirror_->add_on_state_callback([this](bool value) {
        for (auto *r : this->runners_)
          r->setMirror(value);
      });
    }

    if (this->debug_) {
      this->debug_->add_on_state_callback([this](bool value) {
        for (auto *r : this->runners_)
          r->setDebug(value);
      });
    }

    if (this->palette_) {
      this->palette_->add_on_state_callback(
          [this](const std::string &value, size_t index) {
            uint8_t static_pal_idx = 0;
            if (value != "Default") {
              static_pal_idx = this->get_palette_index_(value);
            }

            for (auto *r : this->runners_) {
              uint8_t r_pal_idx;
              if (value == "Default") {
                r_pal_idx = this->get_default_palette_id_(r->getMode());
              } else {
                r_pal_idx = static_pal_idx;
              }
              r->setPalette(r_pal_idx);
            }
          });
    }
  }

  void loop() override {
    if (!light_)
      return;

    bool light_on = light_->remote_values.is_on();
    if (was_on_ && !light_on) {
      // Light turned off -> Reset Timer
      if (timer_ && timer_->state != 0.0f) {
        auto call = timer_->make_call();
        call.set_value(0);
        call.perform();
      }
    }
    was_on_ = light_on;
  }

  void set_speed(number::Number *n) { speed_ = n; }
  void set_intensity(number::Number *n) { intensity_ = n; }
  void set_palette(select::Select *s) { palette_ = s; }
  void set_mirror(esphome::switch_::Switch *s) { mirror_ = s; }
  void set_autotune(esphome::switch_::Switch *s) { autotune_ = s; }
  void set_force_white(esphome::switch_::Switch *s) { force_white_ = s; }
  void set_debug(esphome::switch_::Switch *s) { debug_ = s; }
  void set_intro_effect(select::Select *s) { intro_effect_ = s; }
  void set_intro_duration(number::Number *n) { intro_duration_ = n; }
  void set_intro_use_palette(esphome::switch_::Switch *s) {
    intro_use_palette_ = s;
  }
  void set_outro_effect(select::Select *s) { outro_effect_ = s; }
  void set_outro_duration(number::Number *n) { outro_duration_ = n; }
  void set_timer(number::Number *n) { timer_ = n; }

  void set_light(esphome::light::LightState *light) { light_ = light; }

  void register_runner(CFXRunner *runner) {
    for (auto *r : this->runners_) {
      if (r == runner)
        return;
    }
    this->runners_.push_back(runner);

    if (speed_ && speed_->has_state())
      runner->setSpeed((uint8_t)speed_->state);
    if (intensity_ && intensity_->has_state())
      runner->setIntensity((uint8_t)intensity_->state);
    if (mirror_ && mirror_->has_state())
      runner->setMirror(mirror_->state);
    if (debug_ && debug_->has_state())
      runner->setDebug(debug_->state);
    if (palette_ && palette_->has_state()) {
      std::string opt = palette_->state;
      if (opt.length() > 0) {
        uint8_t pal_idx = (opt == "Default")
                              ? this->get_default_palette_id_(runner->getMode())
                              : get_palette_index_(opt);
        runner->setPalette(pal_idx);
      }
    }
  }

  void unregister_runner(CFXRunner *runner) {
    this->runners_.erase(
        std::remove(this->runners_.begin(), this->runners_.end(), runner),
        this->runners_.end());
  }

  number::Number *get_speed() { return speed_; }
  number::Number *get_intensity() { return intensity_; }
  select::Select *get_palette() { return palette_; }
  esphome::switch_::Switch *get_mirror() { return mirror_; }
  esphome::switch_::Switch *get_autotune() { return autotune_; }
  esphome::switch_::Switch *get_force_white() { return force_white_; }

  esphome::switch_::Switch *get_debug() { return debug_; }
  select::Select *get_intro_effect() { return intro_effect_; }
  number::Number *get_intro_duration() { return intro_duration_; }
  esphome::switch_::Switch *get_intro_use_palette() {
    return intro_use_palette_;
  }
  select::Select *get_outro_effect() { return outro_effect_; }
  number::Number *get_outro_duration() { return outro_duration_; }
  number::Number *get_timer() { return timer_; }

  esphome::light::LightState *get_light() { return light_; }

protected:
  number::Number *speed_{nullptr};
  number::Number *intensity_{nullptr};
  select::Select *palette_{nullptr};
  esphome::switch_::Switch *mirror_{nullptr};
  esphome::switch_::Switch *autotune_{nullptr};
  esphome::switch_::Switch *force_white_{nullptr};

  esphome::switch_::Switch *debug_{nullptr};
  select::Select *intro_effect_{nullptr};
  number::Number *intro_duration_{nullptr};
  esphome::switch_::Switch *intro_use_palette_{nullptr};
  select::Select *outro_effect_{nullptr};
  number::Number *outro_duration_{nullptr};
  number::Number *timer_{nullptr};

  esphome::light::LightState *light_{nullptr};
  std::vector<CFXRunner *> runners_; // Registered active runners
  bool was_on_{false};

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

  uint8_t get_default_palette_id_(uint8_t effect_id) {
    // Monochromatic series always defaults to Solid
    if (effect_id == 161 || effect_id == 162 || effect_id == 163)
      return 255;

    switch (effect_id) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 6:
    case 15:
    case 16:
    case 18:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 28:
    case 40:
    case 54:
    case 60:
    case 68:
    case 76:
    case 91:
    case 95:
    case 96:
    case 98:
    case 100:
    case 152:
    case 154:
    case 156:
    case 157:
    case 164:
      return 255; // Solid

    case 7:
    case 8:
    case 9:
    case 64:
    case 74:
    case 79:
    case 87:
    case 90:
    case 105:
    case 107:
    case 110:
    case 155:
      return 4; // Rainbow

    case 63:
    case 97:
      return 8; // Party

    case 66:
    case 53:
      return 5; // Fire

    case 101:
    case 151:
    case 160:
      return 11; // Ocean

    case 38:
      return 1; // Aurora

    case 104:
      return 12; // HeatColors

    case 52:
      return 13; // Sakura

    default:
      return 1; // General fallback to Aurora
    }
  }

  uint8_t get_palette_index_(const std::string &name) {
    if (name == "Aurora")
      return 1;
    if (name == "Forest")
      return 2;
    if (name == "Halloween")
      return 3;
    if (name == "Rainbow")
      return 4;
    if (name == "Fire")
      return 5;
    if (name == "Sunset")
      return 6;
    if (name == "Ice")
      return 7;
    if (name == "Party")
      return 8;
    if (name == "Lava")
      return 9;
    if (name == "Pastel")
      return 10;
    if (name == "Ocean")
      return 11;
    if (name == "HeatColors")
      return 12;
    if (name == "Sakura")
      return 13;
    if (name == "Rivendell")
      return 14;
    if (name == "Cyberpunk")
      return 15;
    if (name == "OrangeTeal")
      return 16;
    if (name == "Christmas")
      return 17;
    if (name == "RedBlue")
      return 18;
    if (name == "Matrix")
      return 19;
    if (name == "SunnyGold")
      return 20;
    if (name == "Solid")
      return 255; // 21 in select, 255 internal
    if (name == "Fairy")
      return 22;
    if (name == "Twilight")
      return 23;
    if (name == "Default")
      return 0; // Handled by fallback in effect
    return 0;
  }
};

} // namespace chimera_fx
} // namespace esphome
