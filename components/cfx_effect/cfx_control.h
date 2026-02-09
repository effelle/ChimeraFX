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
#include <vector>

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
      for (auto *l : c->lights_) {
        if (l == light)
          return c;
      }
    }
    return nullptr;
  }

  void setup() override {
    instances.push_back(this);
    if (this->timer_ != nullptr) {
      this->set_interval("cfx_timer", 60000,
                         [this]() { this->on_timer_tick_(); });
    }

    // --- PUSH: Speed ---
    if (this->speed_) {
      this->speed_->add_on_state_callback([this](float value) {
        for (auto *r : this->runners_) {
          r->setSpeed((uint8_t)value);
        }
      });
    }

    // --- PUSH: Intensity ---
    if (this->intensity_) {
      this->intensity_->add_on_state_callback([this](float value) {
        for (auto *r : this->runners_) {
          r->setIntensity((uint8_t)value);
        }
      });
    }

    // --- PUSH: Mirror ---
    if (this->mirror_) {
      this->mirror_->add_on_state_callback([this](bool value) {
        for (auto *r : this->runners_) {
          r->setMirror(value);
        }
      });
    }

    // Note: Palette push requires string mapping, handled by individual effect
    // pull for now or we can duplicate mapping logic here. For strict 1-to-N,
    // pull works fine too if effects update frequently. But prompt asked for
    // PUSH. Implementing Palette PUSH logic:
    if (this->palette_) {
      this->palette_->add_on_state_callback(
          [this](const std::string &value, size_t index) {
            for (auto *r : this->runners_) {
              uint8_t pal_idx;
              if (value == "Default") {
                pal_idx = this->get_default_palette_id_(r->getMode());
              } else {
                pal_idx = this->get_palette_index_(value);
              }
              r->setPalette(pal_idx);
            }
          });
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

  // Replaces set_light
  void add_light(esphome::light::LightState *light) {
    lights_.push_back(light);
  }

  void register_runner(CFXRunner *runner) {
    this->runners_.push_back(runner);
    // Push current state to new runner
    if (speed_ && speed_->has_state())
      runner->setSpeed((uint8_t)speed_->state);
    if (intensity_ && intensity_->has_state())
      runner->setIntensity((uint8_t)intensity_->state);
    if (mirror_ && mirror_->has_state())
      runner->setMirror(mirror_->state);
    if (palette_ && palette_->has_state()) {
      auto opt = palette_->current_option();
      if (opt)
        runner->setPalette(get_palette_index_(opt));
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
  select::Select *get_intro_effect() { return intro_effect_; }
  number::Number *get_intro_duration() { return intro_duration_; }
  esphome::switch_::Switch *get_intro_use_palette() {
    return intro_use_palette_;
  }
  number::Number *get_timer() { return timer_; }

  std::vector<esphome::light::LightState *> get_lights() { return lights_; }

protected:
  number::Number *speed_{nullptr};
  number::Number *intensity_{nullptr};
  select::Select *palette_{nullptr};
  esphome::switch_::Switch *mirror_{nullptr};
  select::Select *intro_effect_{nullptr};
  number::Number *intro_duration_{nullptr};
  esphome::switch_::Switch *intro_use_palette_{nullptr};
  number::Number *timer_{nullptr};

  std::vector<esphome::light::LightState *> lights_;
  std::vector<CFXRunner *> runners_; // Registered active runners

  void on_timer_tick_() {
    if (timer_ == nullptr || lights_.empty())
      return;

    float val = timer_->state;
    if (val > 0) {
      val -= 1.0f;
      if (val <= 0) {
        val = 0;
        // Turn off ALL associated lights
        for (auto *light : lights_) {
          auto call = light->turn_off();
          call.perform();
        }
      }
      timer_->publish_state(val);
    }
  }

  uint8_t get_default_palette_id_(uint8_t effect_id) {
    // Basic defaults for PUSH logic
    if (effect_id == 18)
      return 255;
    if (effect_id == 38)
      return 1;
    if (effect_id == 53)
      return 5;
    return 1;
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
