#include "cfx_sequence.h"
#include "esphome/core/log.h"
#include <cmath>
#include <vector>

#include "../cfx_effect/cfx_addressable_light_effect.h"

namespace esphome {
namespace cfx_sequence {

static const char *const TAG = "cfx_sequence";

std::vector<CFXSequence *> CFXSequence::instances;
CFXSequenceSelect *CFXSequenceSelect::instance = nullptr;

void CFXSequenceSelect::setup() {
  CFXSequenceSelect::instance = this;
  this->add_on_state_callback([this](const std::string &value, size_t index) {
    // Re-entrancy guard: publish_state() inside start()/stop() would re-fire
    // this callback, causing an infinite loop and a brownout crash.
    static bool in_callback = false;
    if (in_callback)
      return;
    in_callback = true;

    if (value == "None") {
      for (auto *seq : CFXSequence::instances) {
        seq->stop();
      }
    } else {
      for (auto *seq : CFXSequence::instances) {
        if (seq->get_name() == value) {
          seq->start();
          break;
        }
      }
    }

    in_callback = false;
  });
}

void CFXSequenceSelect::control(const std::string &value) {
  this->publish_state(value);
}

void CFXSequence::setup() {
  ESP_LOGCONFIG(TAG, "Setting up CFX Sequence '%s'...", this->name_.c_str());
  CFXSequence::instances.push_back(this);
}

void CFXSequence::dump_config() {
  ESP_LOGCONFIG(TAG, "CFX Sequence: %s", this->name_.c_str());
  ESP_LOGCONFIG(TAG, "  Target Lights: %zu", this->lights_.size());
  ESP_LOGCONFIG(TAG, "  Effect: %s", this->effect_.c_str());
  if (this->speed_.has_value()) {
    ESP_LOGCONFIG(TAG, "  Speed Override: %d", this->speed_.value());
  }
}

void CFXSequence::start() {
  ESP_LOGD(TAG, "Starting Sequence: %s", this->name_.c_str());

  // Only update the dropdown if it doesn't already show this sequence.
  // Calling publish_state() when the value already matches would re-fire the
  // on_state_callback, creating an infinite loop.
  if (CFXSequenceSelect::instance != nullptr &&
      CFXSequenceSelect::instance->has_state() &&
      CFXSequenceSelect::instance->current_option() != this->name_) {
    CFXSequenceSelect::instance->publish_state(this->name_);
  }

  for (auto *l : this->lights_) {
    // 1. Issue standard light.turn_on with the target effect.
    // set_rgb(1,1,1) forces RGB color mode — without this ESPHome defaults to
    // "White" which drives 0 to all RGB channels on RGB-only strips and makes
    // the effect render to a black strip even though the runner is executing.
    auto call = l->turn_on();
    call.set_rgb(1.0f, 1.0f, 1.0f);
    call.set_effect(this->effect_);
    call.perform();

    // 2. Extract the underlying ChimeraFX effect to inject overrides
    light::LightEffect *effect =
        chimera_fx::LightStateProxy::get_active_effect(l);
    if (effect != nullptr) {
      chimera_fx::CFXAddressableLightEffect *active_fx = nullptr;
      for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
        if (inst == effect) {
          active_fx = inst;
          break;
        }
      }
      if (active_fx != nullptr) {
        active_fx->set_active_sequence(this, this->speed_, this->intensity_,
                                       this->palette_, this->iterations_);
      }
    }
  }
}

void CFXSequence::stop() {
  ESP_LOGD(TAG, "Stopping Sequence: %s", this->name_.c_str());

  if (CFXSequenceSelect::instance != nullptr &&
      CFXSequenceSelect::instance->has_state() &&
      CFXSequenceSelect::instance->current_option() == this->name_) {
    CFXSequenceSelect::instance->publish_state("None");
  }

  for (auto *l : this->lights_) {
    // First unbind the sequence so original defaults come back
    light::LightEffect *effect =
        chimera_fx::LightStateProxy::get_active_effect(l);
    if (effect != nullptr) {
      chimera_fx::CFXAddressableLightEffect *active_fx = nullptr;
      for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
        if (inst == effect) {
          active_fx = inst;
          break;
        }
      }
      if (active_fx != nullptr) {
        active_fx->set_active_sequence(nullptr, {}, {}, {}, 0);
      }
    }

    auto call = l->turn_on();
    call.set_effect("None");
    call.perform();
  }
}

void CFXSequence::report_event_start() {
  for (auto *t : this->on_start_triggers_) {
    t->trigger();
  }
}

void CFXSequence::report_event_complete() {
  for (auto *t : this->on_complete_triggers_) {
    t->trigger();
  }
}

void CFXSequence::check_positional_triggers(int32_t current_pixel,
                                            int32_t total_pixels) {
  if (total_pixels <= 0 || current_pixel < 0 || current_pixel >= total_pixels) {
    return;
  }

  // Prevent multiple identical triggers in sequence, debounce across frames
  if (current_pixel == this->last_triggered_pixel_) {
    return;
  }

  float current_percentage = (float)current_pixel / (float)total_pixels;

  // Evaluate on_reach (Percentage based)
  for (auto *t : this->on_reach_triggers_) {
    float target = t->get_target_position();
    if (std::abs(current_percentage - target) < (1.0f / total_pixels)) {
      if (std::abs(this->last_triggered_percentage_ - target) >=
          (1.0f / total_pixels)) {
        t->trigger(current_percentage);
      }
    }
  }

  // Evaluate on_pixel_num (Absolute based)
  for (auto *t : this->on_pixel_num_triggers_) {
    if (current_pixel == t->get_target_pixel()) {
      t->trigger(current_pixel);
    }
  }

  this->last_triggered_pixel_ = current_pixel;
  this->last_triggered_percentage_ = current_percentage;
}

} // namespace cfx_sequence
} // namespace esphome
