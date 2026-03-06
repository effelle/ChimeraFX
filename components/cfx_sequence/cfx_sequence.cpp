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
  });
}

void CFXSequenceSelect::control(const std::string &value) {
  this->publish_state(value);
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

  if (CFXSequenceSelect::instance != nullptr) {
    CFXSequenceSelect::instance->publish_state(this->name_);
  }

  for (auto *l : this->lights_) {
    // 1. Issue standard light.turn_on with the target effect
    auto call = l->turn_on();
    call.set_effect(this->effect_);
    call.perform();

    // 2. Extract the underlying ChimeraFX effect to inject overrides
    if (l->get_effect() != nullptr) {
      auto *active_fx = dynamic_cast<chimera_fx::CFXAddressableLightEffect *>(
          l->get_effect());
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
    if (l->get_effect() != nullptr) {
      auto *active_fx = dynamic_cast<chimera_fx::CFXAddressableLightEffect *>(
          l->get_effect());
      if (active_fx != nullptr) {
        active_fx->set_active_sequence(nullptr, {}, {}, {}, 0);
      }
    }

    auto call = l->turn_on();
    call.set_effect("Solid");
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
