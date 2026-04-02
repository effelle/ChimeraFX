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

#include "../cfx_light/cfx_light.h"
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
  static std::vector<CFXControl *> &get_instances() {
    static std::vector<CFXControl *> inst;
    return inst;
  }
  static bool global_debug_enabled_;

  static CFXControl *find(light::LightState *light) {
    // 1. Direct master match
    for (auto *c : get_instances()) {
      if (c->get_light() == light)
        return c;
    }

    // 2. Segment match fallback: iterate masters and check segment configs.
    // Fix-3: Guard the static_cast with has_segments(). If the output is a
    // CFXVirtualSegmentLight (not a CFXLightOutput), the cast is undefined
    // behaviour and crashes under rapid concurrent effect-start calls.
    for (auto *c : get_instances()) {
      if (c->get_light() == nullptr)
        continue;
      auto *output = c->get_light()->get_output();
      if (output == nullptr)
        continue;

      // Only cast when we know the output is a CFXLightOutput with segments.
      // Virtual segment lights share the same base type but are NOT castable.
      auto *cfx_out = static_cast<cfx_light::CFXLightOutput *>(output);
      if (!cfx_out->has_segments())
        continue;
      for (auto *seg_state : cfx_out->get_segment_light_states()) {
        if (seg_state == light) {
          return c;
        }
      }
    }
    ESP_LOGD("chimera_fx",
             "CFXControl::find failed for light %p. Instances: %zu", light,
             get_instances().size());
    for (auto *c : get_instances()) {
      ESP_LOGD("chimera_fx", "  Instance %p light: %p", c, c->get_light());
    }
    return nullptr;
  }

  CFXControl() { get_instances().push_back(this); }

  void setup() override {
    if (this->speed_) {
      this->speed_->add_on_state_callback([this](float value) {
        for (auto *r : this->runners_)
          if (!r->sequence_owns_speed_)
            r->setSpeed((uint8_t)value);
      });
    }

    if (this->intensity_) {
      this->intensity_->add_on_state_callback([this](float value) {
        for (auto *r : this->runners_)
          if (!r->sequence_owns_intensity_)
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
        ESP_LOGD("chimera_fx",
                 "Debug switch toggled: %d. Applying to all instances.", value);
        global_debug_enabled_ = value;
        for (auto *c : get_instances()) {
          for (auto *r : c->runners_)
            r->setDebug(value);
        }
      });
    }

    if (this->palette_) {
      for (auto &opt : this->palette_->traits.get_options()) {
        this->palette_mapping_.push_back(this->get_palette_index_(opt));
      }

      this->palette_->add_on_state_callback(
          [this](size_t index) {
            if (index < this->palette_mapping_.size()) {
              uint8_t r_pal_idx = this->palette_mapping_[index];
              for (auto *r : this->runners_) {
                if (!r->sequence_owns_palette_)
                  r->setPalette(r_pal_idx);
              }
            }
          });
    }
  }

  void loop() override {
    if (!light_)
      return;

    bool light_on = light_->remote_values.is_on();
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
  void set_inout_duration(number::Number *n) { inout_duration_ = n; }
  void set_outro_effect(select::Select *s) { outro_effect_ = s; }
  void set_light(esphome::light::LightState *light) { light_ = light; }

  void register_runner(CFXRunner *runner) {
    for (auto *r : this->runners_) {
      if (r == runner)
        return;
    }
    ESP_LOGD("chimera_fx",
             "Registering runner %p to control center %p. Initial debug: %d",
             runner, this, global_debug_enabled_);
    this->runners_.push_back(runner);

    if (speed_ && speed_->has_state())
      runner->setSpeed((uint8_t)speed_->state);
    if (intensity_ && intensity_->has_state())
      runner->setIntensity((uint8_t)intensity_->state);
    if (mirror_ && mirror_->has_state())
      runner->setMirror(mirror_->state);
    if (debug_ && debug_->has_state())
      runner->setDebug(debug_->state);
    else
      runner->setDebug(global_debug_enabled_);
    if (palette_ && palette_->has_state()) {
      std::string opt_str(palette_->current_option());
      const char *opt_ptr = opt_str.c_str();
      std::string opt = opt_ptr ? opt_ptr : "";
      if (opt.length() > 0) {
        uint8_t pal_idx = get_palette_index_(opt);
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
  esphome::light::LightState *get_light() { return light_; }
  esphome::switch_::Switch *get_mirror() { return mirror_; }
  esphome::switch_::Switch *get_autotune() { return autotune_; }
  esphome::switch_::Switch *get_force_white() { return force_white_; }
  esphome::switch_::Switch *get_debug() { return debug_; }
  select::Select *get_intro_effect() { return intro_effect_; }
  number::Number *get_intro_duration() { return inout_duration_; }
  select::Select *get_outro_effect() { return outro_effect_; }
  number::Number *get_outro_duration() { return inout_duration_; }

protected:
  number::Number *speed_{nullptr};
  number::Number *intensity_{nullptr};
  select::Select *palette_{nullptr};
  esphome::switch_::Switch *mirror_{nullptr};
  esphome::switch_::Switch *autotune_{nullptr};
  esphome::switch_::Switch *force_white_{nullptr};
  esphome::switch_::Switch *debug_{nullptr};
  select::Select *intro_effect_{nullptr};
  number::Number *inout_duration_{nullptr};
  select::Select *outro_effect_{nullptr};

  esphome::light::LightState *light_{nullptr};
  std::vector<CFXRunner *> runners_;
  bool was_on_{false};
  std::vector<uint8_t> palette_mapping_;


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
      return 255;
    if (name == "Fairy")
      return 22;
    if (name == "Twilight")
      return 9;
    if (name == "Smart Random")
      return 254;  // CFX-019: routes to _currentRandomPaletteBuffer in getPaletteByIndex
    return 0;
  }
};

} // namespace chimera_fx
} // namespace esphome
