#include "cfx_sequencer.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace cfx_sequencer {

static const char *const TAG = "cfx_sequencer";

std::vector<CFXSequencer *> CFXSequencer::instances;

void CFXSequencer::setup() {
  ESP_LOGCONFIG(TAG, "Setting up CFX Sequencer...");
  CFXSequencer::instances.push_back(this);
}

void CFXSequencer::dump_config() {
  ESP_LOGCONFIG(TAG, "CFX Sequencer:");
  ESP_LOGCONFIG(TAG, "  Target Lights: %zu", this->lights_.size());
}

void CFXSequencer::report_event_start() {
  for (auto *t : this->on_start_triggers_) {
    t->trigger();
  }
}

void CFXSequencer::report_event_complete() {
  for (auto *t : this->on_complete_triggers_) {
    t->trigger();
  }
}

void CFXSequencer::check_positional_triggers(int32_t current_pixel,
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

} // namespace cfx_sequencer
} // namespace esphome
