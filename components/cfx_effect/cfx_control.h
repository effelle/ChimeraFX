#pragma once

#include "CFXRunner.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/application.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "../cfx_light/cfx_light.h"
#include "../cfx_light/cfx_virtual_segment_light.h"
#include "cfx_addressable_light_effect.h"
#ifdef USE_CFX_EVENTS
#include "cfx_event_manager.h"
#endif
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

      // Segment controls can point either at the physical strip output or at a
      // zero-copy virtual segment wrapper. Resolve wrappers back to the parent
      // strip before touching CFXLightOutput-only APIs.
      cfx_light::CFXLightOutput *cfx_out = nullptr;
#ifdef USE_ESP32
      for (auto *seg_out : cfx_light::CFXVirtualSegmentLight::all_segments) {
        if (seg_out == output) {
          cfx_out = seg_out->get_parent();
          break;
        }
      }
#endif
      if (cfx_out == nullptr)
        cfx_out = static_cast<cfx_light::CFXLightOutput *>(output);
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
    this->sync_debug_output_();
    this->bind_speed_callback_();
    this->bind_intensity_callback_();
    this->bind_mirror_callback_();
    this->bind_palette_callback_();
    this->bind_autotune_callback_();
    this->bind_force_white_callback_();
    this->sync_force_white_output_();
    this->schedule_force_white_sync_(25, false);

    if (this->debug_) {
      this->debug_->add_on_state_callback([this](bool value) {
        ESP_LOGD("chimera_fx",
                 "Debug switch toggled: %d. Applying to own runners (%zu).",
                 value, this->runners_.size());
        global_debug_enabled_ = value;
        for (auto *r : this->runners_)
          r->setDebug(value);
        this->sync_debug_output_();

#ifdef USE_ESP32
        // Fix 1: Broadcast globally to all active virtual segments
        for (auto *seg_out : cfx_light::CFXVirtualSegmentLight::all_segments) {
          if (seg_out != nullptr && seg_out->get_parent() != nullptr) {
            auto *effect = seg_out->get_parent()->get_parent_owned_segment_effect(seg_out->get_state_parent());
            if (effect != nullptr && effect->get_act() != nullptr && effect->get_act()->runner != nullptr) {
              effect->get_act()->runner->setDebug(value);
            }
          }
        }
#endif
      });
    }

  }

  void loop() override {
    if (!light_)
      return;

    bool light_on = light_->remote_values.is_on();
    was_on_ = light_on;
  }

  void set_ha_events_enabled(bool enabled) {
#ifdef USE_CFX_EVENTS
    CFXEventManager::get().set_ha_events_enabled(enabled);
#endif
  }

  void set_speed(number::Number *n) {
    speed_ = n;
    this->bind_speed_callback_();
  }
  void set_intensity(number::Number *n) {
    intensity_ = n;
    this->bind_intensity_callback_();
  }
  void set_palette(select::Select *s) {
    palette_ = s;
    this->bind_palette_callback_();
  }
  void set_mirror(esphome::switch_::Switch *s) {
    mirror_ = s;
    this->bind_mirror_callback_();
  }
  void set_autotune(esphome::switch_::Switch *s) {
    autotune_ = s;
    this->bind_autotune_callback_();
  }
  void set_force_white(esphome::switch_::Switch *s) {
    force_white_ = s;
    this->bind_force_white_callback_();
    this->sync_force_white_output_();
  }
  void set_debug(esphome::switch_::Switch *s) { debug_ = s; }
  void set_intro_effect(select::Select *s) { intro_effect_ = s; }
  void set_inout_duration(number::Number *n) { inout_duration_ = n; }
  void set_outro_effect(select::Select *s) { outro_effect_ = s; }
  void set_light(esphome::light::LightState *light) {
    light_ = light;
    this->sync_debug_output_();
    this->sync_force_white_output_();
  }

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
    if (mirror_ && mirror_->has_state() && !runner->sequence_owns_mirror_)
      runner->setMirror(mirror_->state);
    if (debug_ && debug_->has_state())
      runner->setDebug(debug_->state);
    else
      runner->setDebug(global_debug_enabled_);
    if (palette_ && palette_->has_state() && !runner->sequence_owns_palette_) {
      std::string opt_str(palette_->current_option());
      const char *opt_ptr = opt_str.c_str();
      std::string opt = opt_ptr ? opt_ptr : "";
      if (!opt.empty() && opt != "Default") {
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
  void bind_speed_callback_() {
    if (this->speed_ == nullptr || this->speed_cb_number_ == this->speed_) {
      return;
    }
    this->speed_cb_number_ = this->speed_;
    this->speed_->add_on_state_callback([this](float value) {
      this->apply_to_live_runners_([value](CFXRunner *r) {
        if (!r->sequence_owns_speed_) {
          r->setSpeed((uint8_t)value);
        }
      });
      this->wake_mono_idle_output_();
    });
  }

  void bind_intensity_callback_() {
    if (this->intensity_ == nullptr ||
        this->intensity_cb_number_ == this->intensity_) {
      return;
    }
    this->intensity_cb_number_ = this->intensity_;
    this->intensity_->add_on_state_callback([this](float value) {
      this->apply_to_live_runners_([value](CFXRunner *r) {
        if (!r->sequence_owns_intensity_) {
          r->setIntensity((uint8_t)value);
        }
      });
      this->wake_mono_idle_output_();
    });
  }

  void bind_mirror_callback_() {
    if (this->mirror_ == nullptr || this->mirror_cb_switch_ == this->mirror_) {
      return;
    }
    this->mirror_cb_switch_ = this->mirror_;
    this->mirror_->add_on_state_callback([this](bool value) {
      this->apply_to_live_runners_([value](CFXRunner *r) {
        if (!r->sequence_owns_mirror_) {
          r->setMirror(value);
        }
      });
      this->wake_mono_idle_output_();
    });
  }

  void bind_palette_callback_() {
    if (this->palette_ == nullptr ||
        this->palette_cb_select_ == this->palette_) {
      return;
    }
    this->palette_cb_select_ = this->palette_;
    this->palette_->add_on_state_callback(
        [this](size_t) {
          const char *opt_ptr = this->palette_->current_option().c_str();
          if (opt_ptr == nullptr || strcmp(opt_ptr, "Default") == 0) {
            // "Default" is effect-specific. Let the effect resolve and push
            // its own natural palette on the next control pass instead of
            // forcing a generic control-layer palette index here.
            this->wake_mono_idle_output_();
            return;
          }

          uint8_t r_pal_idx = this->get_palette_index_(opt_ptr);
          this->apply_to_live_runners_([r_pal_idx](CFXRunner *r) {
            if (!r->sequence_owns_palette_) {
              r->setPalette(r_pal_idx);
            }
          });
          this->wake_mono_idle_output_();
        });
  }

  void bind_autotune_callback_() {
    if (this->autotune_ == nullptr ||
        this->autotune_cb_switch_ == this->autotune_) {
      return;
    }
    this->autotune_cb_switch_ = this->autotune_;
    this->autotune_->add_on_state_callback([this](bool value) {
      this->apply_to_live_effects_([value](CFXAddressableLightEffect *effect) {
        effect->apply_live_autotune_state(value);
      });
      this->wake_mono_idle_output_();
    });
  }

  template <typename Fn> void apply_to_live_effects_(Fn fn) {
    std::vector<CFXAddressableLightEffect *> seen;
    seen.reserve(2);

    auto apply_once = [this, &seen, &fn](CFXAddressableLightEffect *effect) {
      if (effect == nullptr || effect->get_light_state() != this->light_) {
        return;
      }
      if (std::find(seen.begin(), seen.end(), effect) != seen.end()) {
        return;
      }
      seen.push_back(effect);
      fn(effect);
    };

    for (auto *effect : CFXAddressableLightEffect::all_effects) {
      apply_once(effect);
    }
    for (auto *effect : CFXAddressableLightEffect::all_segment_effects) {
      apply_once(effect);
    }
  }

  template <typename Fn> void apply_to_live_runners_(Fn fn) {
    std::vector<CFXRunner *> seen;
    seen.reserve(this->runners_.size() + 2);

    auto apply_once = [&seen, &fn](CFXRunner *runner) {
      if (runner == nullptr)
        return;
      if (std::find(seen.begin(), seen.end(), runner) != seen.end())
        return;
      seen.push_back(runner);
      fn(runner);
    };

    for (auto *r : this->runners_) {
      apply_once(r);
    }

    auto apply_effect = [this, &apply_once](
                            CFXAddressableLightEffect *effect) {
      if (effect == nullptr || effect->get_light_state() != this->light_) {
        return;
      }
      auto *act = effect->get_act();
      if (act == nullptr) {
        return;
      }
      if (!act->segment_runners.empty()) {
        for (auto *r : act->segment_runners) {
          apply_once(r);
        }
      } else {
        apply_once(act->runner);
      }
    };

    this->apply_to_live_effects_([&apply_effect](CFXAddressableLightEffect *effect) {
      apply_effect(effect);
    });
  }

  bool debug_enabled_() const {
    if (this->debug_ != nullptr && this->debug_->has_state()) {
      return this->debug_->state;
    }
    return global_debug_enabled_;
  }

  void sync_debug_output_() {
#ifdef USE_ESP32
    if (this->light_ == nullptr) {
      return;
    }
    auto *output = this->light_->get_output();
    if (output == nullptr) {
      return;
    }
    cfx_light::CFXLightOutput *cfx_out = nullptr;
    for (auto *seg_out : cfx_light::CFXVirtualSegmentLight::all_segments) {
      if (seg_out == output) {
        cfx_out = seg_out->get_parent();
        break;
      }
    }
    if (cfx_out == nullptr) {
      cfx_out = static_cast<cfx_light::CFXLightOutput *>(output);
    }
    if (cfx_out != nullptr) {
      cfx_out->set_runtime_debug_enabled(this->debug_enabled_());
    }
#endif
  }

  void bind_force_white_callback_() {
    if (this->force_white_ == nullptr)
      return;
    if (this->force_white_cb_switch_ == this->force_white_)
      return;

    this->force_white_cb_switch_ = this->force_white_;
    this->force_white_->add_on_state_callback(
        [this](bool) {
          this->wake_mono_idle_output_();
          this->repaint_force_white_segment_();
          this->schedule_force_white_sync_(25, true);
        });
  }

  void wake_mono_idle_output_() {
    if (this->light_ == nullptr) {
      return;
    }

    auto *output = this->light_->get_output();
    if (output == nullptr) {
      return;
    }

#ifdef USE_ESP32
    for (auto *seg_out : cfx_light::CFXVirtualSegmentLight::all_segments) {
      if (seg_out == output) {
        seg_out->get_parent()->wake_mono_idle(this->light_);
        return;
      }
    }
#endif

    auto *cfx_out = static_cast<cfx_light::CFXLightOutput *>(output);
    cfx_out->wake_mono_idle(this->light_);
  }

  void schedule_force_white_sync_(uint32_t delay_ms, bool repaint_after) {
    uint32_t hash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)) ^
                    (repaint_after ? 0xF0A5u : 0x0F5Au) ^ delay_ms;
    esphome::App.scheduler.set_timeout(
        this, hash, delay_ms, [this, repaint_after]() {
          this->sync_force_white_output_();
          if (repaint_after)
            this->repaint_force_white_segment_now_();
        });
  }

  void repaint_force_white_segment_() {
    this->sync_force_white_output_();
    this->repaint_force_white_segment_now_();
  }

  void repaint_force_white_segment_now_() {
    if (this->light_ == nullptr) {
      return;
    }

    auto *output = this->light_->get_output();
    if (output == nullptr) {
      return;
    }

#ifdef USE_ESP32
    for (auto *seg_out : cfx_light::CFXVirtualSegmentLight::all_segments) {
      if (seg_out != output)
        continue;

      if (seg_out->is_effect_active() || seg_out->get_parent()->has_outro()) {
        return;
      }

      auto *master = seg_out->get_parent()->get_master_light_state();
      if (master != nullptr && master->get_effect_name() != "None") {
        return;
      }

      seg_out->repaint_force_white_current_state();
      return;
    }
#endif
  }

  void sync_force_white_output_() {
    if (this->light_ == nullptr || this->force_white_ == nullptr) {
      return;
    }

    auto *output = this->light_->get_output();
    if (output == nullptr) {
      return;
    }

#ifdef USE_ESP32
    for (auto *seg_out : cfx_light::CFXVirtualSegmentLight::all_segments) {
      if (seg_out == output) {
        seg_out->set_force_white_switch(this->force_white_);
        return;
      }
    }
#endif

    auto *cfx_out = static_cast<cfx_light::CFXLightOutput *>(output);
    cfx_out->set_force_white_switch(this->force_white_);
  }

  number::Number *speed_{nullptr};
  number::Number *intensity_{nullptr};
  select::Select *palette_{nullptr};
  esphome::switch_::Switch *mirror_{nullptr};
  esphome::switch_::Switch *autotune_{nullptr};
  esphome::switch_::Switch *force_white_{nullptr};
  number::Number *speed_cb_number_{nullptr};
  number::Number *intensity_cb_number_{nullptr};
  select::Select *palette_cb_select_{nullptr};
  esphome::switch_::Switch *mirror_cb_switch_{nullptr};
  esphome::switch_::Switch *autotune_cb_switch_{nullptr};
  esphome::switch_::Switch *force_white_cb_switch_{nullptr};
  esphome::switch_::Switch *debug_{nullptr};
  select::Select *intro_effect_{nullptr};
  number::Number *inout_duration_{nullptr};
  select::Select *outro_effect_{nullptr};

  esphome::light::LightState *light_{nullptr};
  std::vector<CFXRunner *> runners_;
  bool was_on_{false};


  // audit 2.1: accept const char* directly to avoid std::string comparisons.
  // Callers that hold a std::string pass .c_str(); callers with a const char*
  // pass it straight through.
  uint8_t get_palette_index_(const char *name) {
    if (!name) return 0;
    if (strcmp(name, "Aurora") == 0)      return 1;
    if (strcmp(name, "Forest") == 0)      return 2;
    if (strcmp(name, "Halloween") == 0)   return 3;
    if (strcmp(name, "Rainbow") == 0)     return 4;
    if (strcmp(name, "Fire") == 0)        return 5;
    if (strcmp(name, "Sunset") == 0)      return 6;
    if (strcmp(name, "Ice") == 0)         return 7;
    if (strcmp(name, "Party") == 0)       return 8;
    if (strcmp(name, "Pastel") == 0)      return 10;
    if (strcmp(name, "Ocean") == 0)       return 11;
    if (strcmp(name, "HeatColors") == 0)  return 12;
    if (strcmp(name, "Sakura") == 0)      return 13;
    if (strcmp(name, "Rivendell") == 0)   return 14;
    if (strcmp(name, "Cyberpunk") == 0)   return 15;
    if (strcmp(name, "OrangeTeal") == 0)  return 16;
    if (strcmp(name, "Christmas") == 0)   return 17;
    if (strcmp(name, "RedBlue") == 0)     return 18;
    if (strcmp(name, "Matrix") == 0)      return 19;
    if (strcmp(name, "SunnyGold") == 0)   return 20;
    if (strcmp(name, "Solid") == 0)       return 255;
    if (strcmp(name, "Fairy") == 0)       return 22;
    if (strcmp(name, "Twilight") == 0)    return 9;
    if (strcmp(name, "Smart Random") == 0) return 254;  // CFX-019
    return 0;
  }
  // Overload for callers that already hold a std::string (avoids changing
  // every call site while still eliminating the comparison overhead).
  uint8_t get_palette_index_(const std::string &name) {
    return get_palette_index_(name.c_str());
  }
};

} // namespace chimera_fx
} // namespace esphome
