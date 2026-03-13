#include "cfx_sequence.h"
#include "../cfx_effect/cfx_addressable_light_effect.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/event/event.h"
#include "esphome/core/log.h"
#include <algorithm> // CFX-011: std::find in destructor
#include <atomic>    // CFX-012: std::atomic<bool>
#include <cmath>
#include <string>
#include <vector>

namespace esphome {
namespace cfx_sequence {

static const char *const TAG = "cfx_sequence";

std::vector<CFXSequence *> CFXSequence::instances;
CFXSequenceSelect *CFXSequenceSelect::instance = nullptr;
// CFX-012: atomic<bool> — safe across FreeRTOS tasks on dual-core ESP32
std::atomic<bool> CFXSequenceSelect::suppress_callback_{false};

void CFXSequenceSelect::setup() {
  CFXSequenceSelect::instance = this;
  // Phase E: Remind integrators to set api: batch_delay: 0ms for best event delivery.
  // This is a one-time boot advisory — it does not block operation.
  ESP_LOGW(TAG,
           "ChimeraFX: For sub-10ms cfx_start/cfx_complete event delivery to "
           "Home Assistant, set 'api: batch_delay: 0ms' in your ESPHome config.");
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

// CFX-011: Destructor removes this from the static instances vector.
// Without this, destroying a CFXSequence leaves a dangling pointer in the
// vector, causing undefined behaviour in stop(), force_reset(), and start().
CFXSequence::~CFXSequence() {
  auto it = std::find(CFXSequence::instances.begin(),
                      CFXSequence::instances.end(), this);
  if (it != CFXSequence::instances.end())
    CFXSequence::instances.erase(it);
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

  // CFX-015 FIX: Fallback path now uses LOGW and names both the targeted lights
  // and the effect it actually binds to, so misconfigured sequences are
  // immediately visible in the serial log.
  if (!bound && !chimera_fx::CFXAddressableLightEffect::all_effects.empty()) {
    auto *master_fx = chimera_fx::CFXAddressableLightEffect::all_effects[0];
    // Build a comma-separated list of target light names for the warning
    std::string target_names;
    for (auto *l : this->lights_) {
      if (!target_names.empty()) target_names += ", ";
      target_names += l->get_name();
    }
    ESP_LOGW(TAG,
             "Sequence '%s': no active CFX effect found for target light(s) [%s]. "
             "Falling back to first registered effect %p — animation may target "
             "the wrong strip. Check that the correct CFX effect is active.",
             this->name_.c_str(), target_names.c_str(), master_fx);
    master_fx->set_active_sequence(this, this->speed_, this->intensity_,
                                   this->palette_, this->iterations_);
    bound = true;
  }

  if (!bound) {
    ESP_LOGW(TAG, "  FAILED to bind — no CFXAddressableLightEffect found");
  }

  this->last_triggered_percentage_ = -1.0f;
  this->last_triggered_pixel_ = -1;
  this->last_fired_milestone_ = 0; // Phase C: reset milestone counter on each new run
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
  this->fire_event("cfx_start");
}

void CFXSequence::report_event_complete() {
  ESP_LOGD(TAG, "Sequence '%s': on_complete triggers firing",
           this->id_.c_str());
  for (auto *t : this->on_complete_triggers_) {
    t->trigger();
  }
  this->fire_event("cfx_complete");
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

  // Check runtime milestones (cfx_reach)
  uint8_t current_pct = (uint8_t)(current_percentage * 100.0f);
  this->check_milestones(current_pct);
}

void CFXSequence::check_milestones(uint8_t current_pct) {
  if (this->progress_step_ == 0) return; // No milestones configured

  uint8_t next_milestone = this->last_fired_milestone_ + this->progress_step_;
  if (current_pct >= next_milestone) {
    this->last_fired_milestone_ = next_milestone;

    // Update sensor before firing event (sensor-before-event pattern)
    if (this->progress_pct_sensor_) {
      this->progress_pct_sensor_->publish_state(current_pct);
    }

    this->fire_event("cfx_reach");
  }
}

void CFXSequence::pixel_advanced(uint16_t pixel) {
  if (this->pixel_whitelist_.empty()) return;

  // Check if pixel is in whitelist
  for (uint16_t p : this->pixel_whitelist_) {
    if (p == pixel) {
      // Update sensor before firing event (sensor-before-event pattern)
      if (this->last_pixel_sensor_) {
        this->last_pixel_sensor_->publish_state(pixel);
      }

      this->fire_event("cfx_pixel");
      break;
    }
  }
}

// ----------------------------------------------------
// Runtime Configurable Entities (Number & Text Inputs)
// ----------------------------------------------------

void CFXProgressStepNumber::setup() {
  uint8_t restored;
  this->pref_ = global_preferences->make_preference<uint8_t>(this->get_object_id_hash());
  if (this->pref_.load(&restored)) {
    this->publish_state(restored);
  } else {
    this->publish_state(10.0f); // Default 10%
  }
}

void CFXProgressStepNumber::control(float value) {
  uint8_t step = (uint8_t)value;
  this->publish_state(value);
  this->pref_.save(&step);
  
  for (auto *seq : CFXSequence::instances) {
    seq->set_progress_step(step);
  }
}

// Helper to parse CSV string into vector
static std::vector<uint16_t> parse_csv(const std::string &csv) {
  std::vector<uint16_t> result;
  size_t start = 0;
  size_t end = csv.find(',');
  while (end != std::string::npos) {
    std::string token = csv.substr(start, end - start);
    if (!token.empty()) {
      try {
        result.push_back(std::stoi(token));
      } catch (...) {} // Ignore invalid data
    }
    start = end + 1;
    end = csv.find(',', start);
  }
  if (start < csv.length()) {
    std::string token = csv.substr(start);
    if (!token.empty()) {
      try {
        result.push_back(std::stoi(token));
      } catch (...) {}
    }
  }
  return result;
}

void CFXPixelWatchText::setup() {
  std::string restored;
  // Use a fixed size hash array for string preference (max 64 chars for pixel list)
  this->pref_ = global_preferences->make_preference<char[64]>(this->get_object_id_hash());
  
  char buffer[64];
  if (this->pref_.load(&buffer)) {
    restored = std::string(buffer);
    this->publish_state(restored);
    
    // Parse and apply loaded state
    std::vector<uint16_t> parsed = parse_csv(restored);
    for (auto *seq : CFXSequence::instances) {
      seq->set_pixel_whitelist(parsed);
    }
  } else {
    this->publish_state(""); // Default empty
  }
}

void CFXPixelWatchText::control(const std::string &value) {
  this->publish_state(value);
  
  char buffer[64] = {0};
  strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
  this->pref_.save(&buffer);
  
  std::vector<uint16_t> parsed = parse_csv(value);
  for (auto *seq : CFXSequence::instances) {
    seq->set_pixel_whitelist(parsed);
  }
}

} // namespace cfx_sequence
} // namespace esphome
