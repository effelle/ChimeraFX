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
bool CFXSequenceSelect::suppress_callback_ = false;

void CFXSequenceSelect::setup() {
  CFXSequenceSelect::instance = this;
  this->add_on_state_callback([](const std::string &value, size_t index) {
    if (CFXSequenceSelect::suppress_callback_)
      return;

    if (value == "None") {
      ESP_LOGD(TAG, "Active Sequence Select: 'None'");
      for (auto *seq : CFXSequence::instances) {
        seq->stop();
        seq->force_reset();
      }
    } else {
      for (auto *seq : CFXSequence::instances) {
        if (seq->get_name() == value) {
          if (!seq->is_starting()) {
            ESP_LOGD(TAG, "Active Sequence Select: '%s' (ID: %s)",
                     value.c_str(), seq->get_id().c_str());
            seq->start();
          }
          break;
        }
      }
    }
  });
}

void CFXSequenceSelect::control(const std::string &value) {
  this->publish_state(value);
}

void CFXSequenceSelect::publish_state_silent(const std::string &value) {
  CFXSequenceSelect::suppress_callback_ = true;
  this->publish_state(value);
  CFXSequenceSelect::suppress_callback_ = false;
}

CFXSequence::CFXSequence(const std::string &id, const std::string &name,
                         const std::string &effect, bool restore)
    : id_(id), name_(name), effect_(effect), restore_state_(restore) {
  CFXSequence::instances.push_back(this);
}

void CFXSequence::start() {
  if (this->is_starting_ || this->is_running_)
    return;
  this->is_starting_ = true;

  // Handover: Stop all other sequences first to prevent state restoration race
  for (auto *seq : CFXSequence::instances) {
    if (seq != this && seq->is_running_) {
      seq->stop();
    }
  }

  ESP_LOGD(TAG, "Starting CFX Sequence '%s' (%s)...", this->name_.c_str(),
           this->id_.c_str());

  // Save current states before changing
  this->saved_states_.clear();
  for (auto *l : this->lights_) {
    SavedState s;
    s.values = l->remote_values;
    s.color_mode = l->remote_values.get_color_mode();
    s.effect = "None";

    light::LightEffect *effect =
        chimera_fx::LightStateProxy::get_active_effect(l);
    if (effect != nullptr) {
      s.effect = effect->get_name();
    }
    this->saved_states_.push_back(s);
  }

  // Activate effect on all target lights
  for (auto *l : this->lights_) {
    auto call = l->make_call();
    call.set_state(true);
    if (!this->effect_.empty()) {
      call.set_effect(this->effect_);
    }
    auto valid_mode = l->remote_values.get_color_mode();
    if (valid_mode == light::ColorMode::UNKNOWN) {
      if (l->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE)) {
        valid_mode = light::ColorMode::RGB_WHITE;
      } else if (l->get_traits().supports_color_mode(light::ColorMode::RGB)) {
        valid_mode = light::ColorMode::RGB;
      }
    }
    call.set_color_mode(valid_mode);
    call.set_transition_length(0);
    if (this->brightness_.has_value()) {
      call.set_brightness(this->brightness_.value());
    }
    call.perform();
  }

  // Bind sequence to the correct CFXAddressableLightEffect instance.
  // Strategy: get the active effect from EACH target light and match it
  // in all_effects to ensure all sequence-controlled segments are mapped.
  bool bound = false;
  for (auto *l : this->lights_) {
    light::LightEffect *active =
        chimera_fx::LightStateProxy::get_active_effect(l);
    if (active == nullptr)
      continue;

    for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
      if (inst == active) {
        ESP_LOGD(TAG, "  Binding Sequence to Effect %p (active match)", inst);
        inst->set_active_sequence(this, this->speed_, this->intensity_,
                                  this->palette_, this->iterations_);
        bound = true;
        // Do not break! Match other lights as well.
      }
    }
  }

  // Fallback: for segment lights, the active effect is a segment-local instance
  // that may not be in all_effects. Bind to the first master effect instead.
  if (!bound && !chimera_fx::CFXAddressableLightEffect::all_effects.empty()) {
    auto *master_fx = chimera_fx::CFXAddressableLightEffect::all_effects[0];
    ESP_LOGD(TAG, "  Binding Sequence to Effect %p (segment fallback)",
             master_fx);
    master_fx->set_active_sequence(this, this->speed_, this->intensity_,
                                   this->palette_, this->iterations_);
    bound = true;
  }

  if (!bound) {
    ESP_LOGW(TAG, "  FAILED to bind — no CFXAddressableLightEffect found");
  }

  this->last_triggered_percentage_ = -1.0f;
  this->last_triggered_pixel_ = -1;
  this->is_running_ = true;
  this->is_starting_ = false;

  // Update Select UI to reflect the active sequence
  if (CFXSequenceSelect::instance != nullptr) {
    CFXSequenceSelect::instance->publish_state_silent(this->name_);
  }

  this->report_event_start();
}

void CFXSequence::stop() {
  if (this->is_stopping_ || !this->is_running_)
    return;
  this->is_stopping_ = true;
  this->is_running_ = false;

  ESP_LOGD(TAG, "Stopping CFX Sequence '%s'...", this->name_.c_str());

  this->clear_active_binding();

  // Restore States: Only if explicitly requested
  if (this->restore_state_) {
    size_t light_idx = 0;
    for (auto *l : this->lights_) {
      if (light_idx < this->saved_states_.size()) {
        auto saved = this->saved_states_[light_idx];
        auto call = l->make_call();

        // Restore everything: state, mode, brightness, color
        // This ensures that even if we turn OFF, the "last known" values are
        // correct.
        call.set_state(saved.values.is_on());
        call.set_color_mode(saved.color_mode);
        call.set_brightness(saved.values.get_brightness());
        call.set_rgb(saved.values.get_red(), saved.values.get_green(),
                     saved.values.get_blue());
        call.set_white(saved.values.get_white());

        if (saved.values.is_on()) {
          call.set_effect(saved.effect);
        } else {
          call.set_effect("None");
        }

        call.set_transition_length(0); // Instant restore for sequences
        call.perform();
      }
      light_idx++;
    }
  }

  this->is_stopping_ = false;

  // Update Select UI to reflect the stopped sequence
  if (CFXSequenceSelect::instance != nullptr &&
      CFXSequenceSelect::instance->has_state()) {
    const char *current = CFXSequenceSelect::instance->current_option();
    if (current != nullptr && this->name_ == current) {
      CFXSequenceSelect::instance->publish_state_silent("None");
    }
  }
}

void CFXSequence::force_reset() {
  // Hard reset: turn off all lights managed by this sequence.
  // Called by "None" handler to ensure lights are off even when
  // the sequence has already completed and on_complete overrode state.
  for (auto *l : this->lights_) {
    auto call = l->make_call();
    call.set_state(false);
    call.set_effect("None");
    call.set_transition_length(0);
    call.perform();
  }
}

void CFXSequence::clear_active_binding() {
  // Clear binding on ALL effect instances that point to this sequence
  for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
    if (inst->get_active_sequence() == this) {
      inst->set_active_sequence(nullptr, {}, {}, {}, 0);
    }
  }
}

void CFXSequence::report_event_start() {
  ESP_LOGV(TAG, "Sequence '%s': on_start triggers firing", this->id_.c_str());
  for (auto *t : this->on_start_triggers_) {
    t->trigger();
  }
}

void CFXSequence::report_event_complete() {
  ESP_LOGD(TAG, "Sequence '%s': on_complete triggers firing",
           this->id_.c_str());
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
  float current_percentage = (float)current_pixel / (float)total_pixels;

  // Evaluate on_reach (Percentage based)
  for (auto *t : this->on_reach_triggers_) {
    float target = t->get_target_position();
    bool crossed = false;

    if (this->last_triggered_percentage_ == -1.0f) {
      if (current_percentage >= target)
        crossed = true;
    } else {
      // Forward crossing
      if (current_percentage >= target &&
          this->last_triggered_percentage_ < target) {
        crossed = true;
      }
      // Backward crossing
      else if (current_percentage <= target &&
               this->last_triggered_percentage_ > target) {
        crossed = true;
      }
      // Wrap-around forward (e.g. 0.95 -> 0.05)
      else if (this->last_triggered_percentage_ > 0.8f &&
               current_percentage < 0.2f) {
        if (target > this->last_triggered_percentage_ ||
            target <= current_percentage) {
          crossed = true;
        }
      }
      // Wrap-around backward (e.g. 0.05 -> 0.95)
      else if (this->last_triggered_percentage_ < 0.2f &&
               current_percentage > 0.8f) {
        if (target < this->last_triggered_percentage_ ||
            target >= current_percentage) {
          crossed = true;
        }
      }
    }

    if (crossed) {
      ESP_LOGD(TAG, "Sequence '%s': on_reach %.0f%% triggered",
               this->id_.c_str(), target * 100.0f);
      t->trigger(current_percentage);
    }
  }

  // Evaluate on_pixel_num (Discrete based)
  for (auto *t : this->on_pixel_num_triggers_) {
    if (current_pixel == t->get_target_pixel()) {
      ESP_LOGD(TAG, "Sequence '%s': on_pixel_num %d triggered",
               this->id_.c_str(), current_pixel);
      t->trigger(current_pixel);
    }
  }

  this->last_triggered_percentage_ = current_percentage;
  this->last_triggered_pixel_ = current_pixel;
}

} // namespace cfx_sequence
} // namespace esphome
