#include "cfx_sequence.h"
#include "../cfx_effect/cfx_addressable_light_effect.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/log.h"
#include <cmath>
#include <vector>


namespace esphome {
namespace cfx_sequence {

static const char *const TAG = "cfx_sequence";

std::vector<CFXSequence *> CFXSequence::instances;
CFXSequenceSelect *CFXSequenceSelect::instance = nullptr;

void CFXSequenceSelect::setup() {
  CFXSequenceSelect::instance = this;
  this->add_on_state_callback([](const std::string &value) {
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

CFXSequence::CFXSequence(const std::string &id, const std::string &name,
                         const std::string &effect)
    : id_(id), name_(name), effect_(effect) {
  CFXSequence::instances.push_back(this);
}

void CFXSequence::start() {
  if (this->is_starting_)
    return;
  this->is_starting_ = true;
  ESP_LOGD(TAG, "Starting CFX Sequence '%s' (%s)...", this->name_.c_str(),
           this->id_.c_str());

  // Save current states before changing
  this->saved_states_.clear();
  for (auto *l : this->lights_) {
    SavedState s;
    s.values = l->remote_values;
    s.effect = "None";

    light::LightEffect *effect =
        chimera_fx::LightStateProxy::get_active_effect(l);
    if (effect != nullptr) {
      s.effect = effect->get_name();
    }
    this->saved_states_.push_back(s);
  }

  // Update physical select if exists
  if (CFXSequenceSelect::instance != nullptr) {
    CFXSequenceSelect::instance->publish_state(this->name_);
  }

  // Activate effect on all target lights
  for (auto *l : this->lights_) {
    auto call = l->make_call();
    call.set_effect(this->effect_);
    call.perform();

    // Bind parameters
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
        ESP_LOGD(TAG, "  Binding Sequence '%s' (%p) to Effect instance %p",
                 this->id_.c_str(), this, active_fx);
        active_fx->set_active_sequence(this, this->speed_, this->intensity_,
                                       this->palette_, this->iterations_);
      } else {
        ESP_LOGW(
            TAG,
            "  FAILED to find matching ChimeraFX effect instance for light %s",
            l->get_name().c_str());
      }
    }
  }

  this->last_triggered_percentage_ = -1.0f;
  this->last_triggered_pixel_ = -1;
  this->is_starting_ = false;
}

void CFXSequence::stop() {
  if (this->is_stopping_)
    return;
  this->is_stopping_ = true;
  ESP_LOGD(TAG, "Stopping CFX Sequence '%s'...", this->name_.c_str());

  if (CFXSequenceSelect::instance != nullptr &&
      CFXSequenceSelect::instance->has_state() &&
      CFXSequenceSelect::instance->current_option() == this->name_) {
    CFXSequenceSelect::instance->publish_state("None");
  }

  this->clear_active_binding();

  // Restore States
  size_t light_idx = 0;
  for (auto *l : this->lights_) {
    if (light_idx < this->saved_states_.size()) {
      auto saved = this->saved_states_[light_idx];
      auto call = l->make_call();
      call.set_state(saved.values.is_on());
      if (saved.values.is_on()) {
        call.set_effect(saved.effect);
        call.set_brightness(saved.values.get_brightness());
        call.set_rgb(saved.values.get_red(), saved.values.get_green(),
                     saved.values.get_blue());
        call.set_white(saved.values.get_white());
      }
      call.set_transition_length(1000);
      call.perform();
    }
    light_idx++;
  }

  this->is_stopping_ = false;
}

void CFXSequence::clear_active_binding() {
  for (auto *l : this->lights_) {
    light::LightEffect *effect =
        chimera_fx::LightStateProxy::get_active_effect(l);
    if (effect != nullptr) {
      for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
        if (inst == effect && inst->get_active_sequence() == this) {
          inst->set_active_sequence(nullptr, {}, {}, {}, 0);
        }
      }
    }
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
  if (total_pixels <= 0)
    return;

  // Debounce per pixel
  if (current_pixel == this->last_triggered_pixel_) {
    return;
  }

  // Adjust percentage calculation to be boundary-inclusive (0.0 to 1.0)
  float current_percentage = (float)current_pixel / (float)(total_pixels - 1);

  // Evaluate on_reach (Percentage based)
  for (auto *t : this->on_reach_triggers_) {
    float target = t->get_target_position();
    // Allow for a small epsilon in matching
    if (std::abs(current_percentage - target) < (1.0f / total_pixels)) {
      if (std::abs(this->last_triggered_percentage_ - target) >=
          (1.0f / total_pixels)) {
        t->trigger(current_percentage);
      }
    }
  }

  // Evaluate on_pixel_num (Discrete based)
  for (auto *t : this->on_pixel_num_triggers_) {
    if (current_pixel == t->get_target_pixel()) {
      t->trigger(current_pixel);
    }
  }

  this->last_triggered_percentage_ = current_percentage;
  this->last_triggered_pixel_ = current_pixel;
}

} // namespace cfx_sequence
} // namespace esphome
