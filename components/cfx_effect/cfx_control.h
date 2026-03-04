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
  struct SegmentState {
    uint8_t speed = 128;
    uint8_t intensity = 128;
    uint8_t palette = 0;
    bool mirror = false;
    bool autotune = false;
    bool debug = false;
  };
  std::map<std::string, SegmentState> segment_states_;

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

    // Initialize the cache state for each light segment
    for (auto *l : this->lights_) {
      this->segment_states_[l->get_name()] = SegmentState();
    }

    // --- PUSH: Speed ---
    if (this->speed_) {
      this->speed_->add_on_state_callback([this](float value) {
        std::string target =
            (this->target_segment_ && this->target_segment_->has_state())
                ? this->target_segment_->current_option()
                : "All Segments";
        if (target == "All Segments") {
          for (auto &pair : this->segment_states_)
            pair.second.speed = (uint8_t)value;
        } else {
          this->segment_states_[target].speed = (uint8_t)value;
        }

        for (auto *r : this->runners_) {
          if (should_target_runner_(r))
            r->setSpeed((uint8_t)value);
        }
      });
    }

    // --- PUSH: Intensity ---
    if (this->intensity_) {
      this->intensity_->add_on_state_callback([this](float value) {
        std::string target =
            (this->target_segment_ && this->target_segment_->has_state())
                ? this->target_segment_->current_option()
                : "All Segments";
        if (target == "All Segments") {
          for (auto &pair : this->segment_states_)
            pair.second.intensity = (uint8_t)value;
        } else {
          this->segment_states_[target].intensity = (uint8_t)value;
        }

        for (auto *r : this->runners_) {
          if (should_target_runner_(r))
            r->setIntensity((uint8_t)value);
        }
      });
    }

    // --- PUSH: Mirror ---
    if (this->mirror_) {
      this->mirror_->add_on_state_callback([this](bool value) {
        std::string target =
            (this->target_segment_ && this->target_segment_->has_state())
                ? this->target_segment_->current_option()
                : "All Segments";
        if (target == "All Segments") {
          for (auto &pair : this->segment_states_)
            pair.second.mirror = value;
        } else {
          this->segment_states_[target].mirror = value;
        }

        for (auto *r : this->runners_) {
          if (should_target_runner_(r))
            r->setMirror(value);
        }
      });
    }

    // --- PUSH: Debug ---
    if (this->debug_) {
      this->debug_->add_on_state_callback([this](bool value) {
        // Debug applies globally across all segments
        for (auto &pair : this->segment_states_)
          pair.second.debug = value;

        for (auto *r : this->runners_) {
          r->setDebug(value);
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
            uint8_t static_pal_idx;
            if (value != "Default") {
              static_pal_idx = this->get_palette_index_(value);
            }

            uint8_t pal_idx = (value == "Default") ? 0 : static_pal_idx;
            std::string target =
                (this->target_segment_ && this->target_segment_->has_state())
                    ? this->target_segment_->current_option()
                    : "All Segments";
            if (target == "All Segments") {
              for (auto &pair : this->segment_states_)
                pair.second.palette = pal_idx;
            } else {
              this->segment_states_[target].palette = pal_idx;
            }

            for (auto *r : this->runners_) {
              if (should_target_runner_(r)) {
                uint8_t r_pal_idx;
                if (value == "Default") {
                  r_pal_idx = this->get_default_palette_id_(r->getMode());
                } else {
                  r_pal_idx = static_pal_idx;
                }
                r->setPalette(r_pal_idx);
              }
            }
          });
    }

    // --- PULL: Target Segment Selection ---
    if (this->target_segment_) {
      this->target_segment_->add_on_state_callback(
          [this](const std::string &value, size_t index) {
            if (value == "All Segments")
              return; // Keep broad changes visible in the UI

            auto &state = this->segment_states_[value];
            if (this->speed_ && this->speed_->state != state.speed) {
              this->speed_->publish_state(state.speed);
            }
            if (this->intensity_ &&
                this->intensity_->state != state.intensity) {
              this->intensity_->publish_state(state.intensity);
            }
            if (this->mirror_ && this->mirror_->state != state.mirror) {
              this->mirror_->publish_state(state.mirror);
            }
            // Palette is string-based, so reverse mapping is needed if we
            // wanted to pull it correctly, but for now slider sync is the main
            // focus per the user report.
          });
    }
  }

  void loop() override {
    if (lights_.empty())
      return;

    // Use ONLY the primary Master light (index 0) to determine global ON/OFF
    // state Do not aggregate segment states here, as Master sync handles
    // aggregation.
    bool master_on = lights_[0]->remote_values.is_on();

    // Detect falling edge (Master light went from ON -> OFF)
    if (was_on_ && !master_on) {
      // Reset Timer to 0 (Abort Sleep Timer)
      if (timer_ && timer_->state != 0.0f) {
        ESP_LOGD("chimera_fx",
                 "CFXControl: Master light turned off -> Resetting Timer");
        auto call = timer_->make_call();
        call.set_value(0);
        call.perform();
      }
    }
    was_on_ = master_on;
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
  void set_target_segment(select::Select *s) { target_segment_ = s; }
  select::Select *get_target_segment() { return target_segment_; }

  // Replaces set_light
  void add_light(esphome::light::LightState *light) {
    lights_.push_back(light);
  }

  void register_runner(CFXRunner *runner) {
    // Safety: Prevent duplicate registration (called every frame from
    // run_controls_)
    for (auto *r : this->runners_) {
      if (r == runner)
        return; // Already registered
    }
    this->runners_.push_back(runner);
    // Push current state to new runner
    if (speed_ && speed_->has_state())
      runner->setSpeed((uint8_t)speed_->state);
    if (intensity_ && intensity_->has_state())
      runner->setIntensity((uint8_t)intensity_->state);
    if (mirror_ && mirror_->has_state())
      runner->setMirror(mirror_->state);
    if (debug_ && debug_->has_state())
      runner->setDebug(debug_->state);
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

  std::vector<esphome::light::LightState *> get_lights() { return lights_; }

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
  select::Select *target_segment_{nullptr};

  std::vector<esphome::light::LightState *> lights_;
  std::vector<CFXRunner *> runners_; // Registered active runners
  bool was_on_{false};

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

  // Target Segment filter: returns true if the runner matches the current
  // target, or if target is "All Segments" / unset (broadcast to all).
  bool should_target_runner_(CFXRunner *r) const {
    if (!target_segment_ || !target_segment_->has_state())
      return true;
    const char *opt = target_segment_->current_option();
    if (opt == nullptr || strcmp(opt, "All Segments") == 0)
      return true;
    return r->get_segment_id() == opt;
  }

  // Target Light filter: Used to lookup the cached configuration of a Segment
  // even when it is currently "turned off" and has no active Runner.
  bool should_target_light_(esphome::light::LightState *l) const {
    if (!target_segment_ || !target_segment_->has_state())
      return true;
    const char *opt = target_segment_->current_option();
    if (opt == nullptr || strcmp(opt, "All Segments") == 0)
      return true;
    return l->get_name() == opt;
  }
};

} // namespace chimera_fx
} // namespace esphome
