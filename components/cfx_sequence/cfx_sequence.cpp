#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>
#include <cstdlib>
#include <set>

#include "cfx_sequence.h"
#include "../cfx_effect/cfx_addressable_light_effect.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/event/event.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstring>

namespace esphome {
namespace cfx_sequence {

static const char *const TAG = "cfx_sequence";

std::vector<CFXSequence *> CFXSequence::instances;
CFXSequenceSelect *CFXSequenceSelect::instance = nullptr;
std::atomic<bool> CFXSequenceSelect::suppress_callback_{false};

CFXEventManager &CFXEventManager::get() {
  static CFXEventManager instance;
  return instance;
}

void CFXEventManager::fire_event(const char *type) {
  if (this->event_entity_ != nullptr) {
    this->event_entity_->trigger(type);
    bool suppress_idle = (strncmp(type, "cfx_complete", 12) == 0 ||
                          strncmp(type, "cfx_start",    9)  == 0 ||
                          strncmp(type, "cfx_reach",    9)  == 0);
    if (!suppress_idle) {
      this->pending_idle_ = true;
      this->idle_hold_until_ms_ = millis() + CFX_IDLE_HOLD_MS;
    }
  }
  // Mirror every event to the text_sensor so HA state trigger works
  // reliably — text_sensor state IS the string, no attribute indirection. (CFX-025)
  if (this->event_text_sensor_ != nullptr)
    this->event_text_sensor_->publish_state(type);
}

void CFXEventManager::queue_event(const char *type) {
  // Three-slot ring buffer (CFX-022). Only string literals are stored
  // (permanent lifetime). If all slots are occupied the new event is dropped.
  // Normal operation queues at most cfx_complete + one other event per tick.
  uint8_t next = (this->pending_write_ + 1) % PENDING_QUEUE_SIZE;
  if (next == this->pending_read_) {
    ESP_LOGW("cfx_sequence", "queue_event: queue full, dropping '%s'", type);
    return;
  }
  this->pending_events_[this->pending_write_] = type;
  this->pending_write_ = next;
}

void CFXEventManager::flush_pending() {
  if (this->event_entity_ == nullptr) return;

  // Fire one queued event per call. One event per loop() cycle guarantees
  // each event lands in a separate WebSocket frame to HA.
  if (this->pending_read_ != this->pending_write_) {
    const char *evt = this->pending_events_[this->pending_read_];
    this->pending_read_ = (this->pending_read_ + 1) % PENDING_QUEUE_SIZE;
    this->event_entity_->trigger(evt);
    // Mirror fire_event's idle suppression: cfx_start, cfx_complete, and any
    // cfx_reach variant don't need idle scheduling. (CFX-024)
    bool suppress_idle = (strncmp(evt, "cfx_complete", 12) == 0 ||
                          strncmp(evt, "cfx_start",    9)  == 0 ||
                          strncmp(evt, "cfx_reach",    9)  == 0);
    if (!suppress_idle) {
      this->pending_idle_ = true;
      this->idle_hold_until_ms_ = millis() + CFX_IDLE_HOLD_MS;
    }
    return; // one action per call
  }

  if (this->pending_idle_) {
    if (millis() >= this->idle_hold_until_ms_) {
      this->pending_idle_ = false;
      this->event_entity_->trigger("cfx_idle");
      if (this->event_text_sensor_ != nullptr)
        this->event_text_sensor_->publish_state("cfx_idle");
    }
  }
}

void CFXEventManager::report_progress(float pct) {
  if (this->progress_pct_sensor_ != nullptr) {
    // Phase J: Round to 0 decimals as requested by USER
    float rounded = std::round(pct);
    this->progress_pct_sensor_->publish_state(rounded);
  }
}

void CFXEventManager::report_last_pixel(int32_t pixel) {
  if (this->last_pixel_sensor_ != nullptr) {
    this->last_pixel_sensor_->publish_state(pixel);
  }
}

void CFXEventManager::check_milestones(float current_pct) {
  if (this->progress_step_ == 0) return;

  // Always reset the per-frame flag at the start of each check so callers
  // can reliably test it after this call. (CFX-022)
  this->milestone_fired_this_frame_ = false;

  uint8_t next_milestone = this->last_fired_milestone_ + this->progress_step_;
  if (current_pct >= next_milestone) {
    this->last_fired_milestone_ = next_milestone;

    this->milestone_fired_this_frame_ = true;

    // Fire using pre-computed string — no heap allocation on hot path. (CFX-024)
    // Progress value is encoded in the event string (cfx_reach:<tag>:<pct>)
    // so the separate progress sensor publish is redundant and adds API blocking.
    // The progress sensor is updated only at start/stop lifecycle points. (CFX-025)
    {
      uint8_t idx = (this->last_fired_milestone_ / this->progress_step_) - 1;
      if (idx < MAX_MILESTONES) {
        this->fire_event(this->milestone_events_[idx].c_str());
      } else {
        this->fire_event("cfx_reach"); // fallback, should not happen
      }
    }
  } else if (current_pct < this->last_fired_milestone_) {
    // Reset milestone counter when a new forward pass begins.
    // check_milestones is only called during the forward pass (the adapter
    // suppresses calls during the erase/return phase via runner->is_return_phase_).
    // So this branch is reached at the start of a genuine new forward pass
    // where current_pct drops back to ~0 after the previous cycle completed.
    // The >= 100 guard handles both exact and non-exact step divisors:
    // - last_fired_milestone_ >= 100: step divides evenly into 100
    // - current_pct >= 100.0f: step does NOT divide evenly (last milestone
    //   is e.g. 95 for step=5), but actual percentage reached 100.0
    if (this->last_fired_milestone_ >= 100 || current_pct >= 100.0f) {
      this->last_fired_milestone_ = 0;
      this->pass_count_++;
      // Schedule cfx_idle as a pass-boundary separator. At high speeds the
      // return/erase phase is very short (<375ms at speed 255). Without a gap
      // event, HA receives :100 and :5 of the next loop in rapid succession.
      // HA's state machine may not commit :100 before :5 arrives, so the
      // attribute appears unchanged (:75 -> :75) and the state trigger drops
      // the event. cfx_idle forces a guaranteed attribute change between loops
      // regardless of how fast the effect cycles. (CFX-024)
      this->pending_idle_ = true;
      this->idle_hold_until_ms_ = millis() + CFX_IDLE_HOLD_MS;
    }
  }
}

void CFXEventManager::pixel_advanced(uint16_t pixel) {
  // Both the sensor update and the event are opt-in. When ha_pixel_enabled_
  // is false there is nothing to do — skip all API work entirely. (CFX-024)
  if (!this->ha_pixel_enabled_) return;
  this->report_last_pixel((int32_t)pixel);
  // Use pre-computed tagged string — no heap allocation on hot path. (CFX-024)
  this->fire_event(this->cfx_pixel_tagged_.c_str());
}

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

// void CFXSequenceSelect::loop() implementation moved to header for inlining

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
  // Clear any listeners
  for (auto &m : this->monitored_lights_) {
    m.listener->nullify();
    // m.light->remove_remote_values_listener(m.listener); // Error: non-existent in ESPHome
    // delete m.listener; // Unsafe: cannot remove from LightState, so must leak to avoid crash
  }
  this->monitored_lights_.clear();

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

    // Never save ColorMode::UNKNOWN — ESPHome emits a warning if it is later
    // passed back to set_color_mode() on restore. Resolve it to the best
    // supported mode for this light so the restore call is always valid.
    light::ColorMode cm = l->remote_values.get_color_mode();
    if (cm == light::ColorMode::UNKNOWN) {
      if (l->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE))
        cm = light::ColorMode::RGB_WHITE;
      else if (l->get_traits().supports_color_mode(light::ColorMode::RGB))
        cm = light::ColorMode::RGB;
      else if (l->get_traits().supports_color_mode(light::ColorMode::WHITE))
        cm = light::ColorMode::WHITE;
      else if (l->get_traits().supports_color_mode(light::ColorMode::BRIGHTNESS))
        cm = light::ColorMode::BRIGHTNESS;
      else
        cm = light::ColorMode::ON_OFF;
    }
    s.color_mode = cm;
    s.effect = "None";

    light::LightEffect *effect =
        chimera_fx::LightStateProxy::get_active_effect(l);
    if (effect != nullptr) {
      s.effect = effect->get_name();
    }
    this->saved_states_.push_back(s);
  }

  // CFX-020: Listener reuse — CFXSequenceListener cannot be deleted because
  // LightState holds a permanent reference. Reuse existing listeners across
  // restart cycles via reinstate() to bound the leak to one allocation per
  // monitored light per CFXSequence object (not per start() call).
  for (auto *l : this->lights_) {
    auto it = std::find_if(
        this->monitored_lights_.begin(), this->monitored_lights_.end(),
        [l](const MonitoredLight &m) { return m.light == l; });

    if (it != this->monitored_lights_.end()) {
      it->listener->reinstate(this); // re-arm nullified listener for new run
    } else {
      auto *listener = new CFXSequenceListener(this, l);
      l->add_remote_values_listener(listener);
      this->monitored_lights_.push_back({l, listener});
    }
  }

  // CFX-024: Pre-load strip tag into CFXEventManager before perform() fires
  // the effect's start(). The effect's start() also derives the tag via
  // get_object_id() but doing it here ensures the singleton is primed even
  // if get_object_id() and the YAML id differ.
  if (!this->strip_tag_.empty()) {
    ESP_LOGD(TAG, "  Pre-loading strip tag '%s' into CFXEventManager", this->strip_tag_.c_str());
    cfx_sequence::CFXEventManager::get().set_strip_tag(this->strip_tag_);
    cfx_sequence::CFXEventManager::get().set_ha_pixel_enabled(this->ha_pixel_enabled_);
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
    if (this->effect_.empty()) {
      call.set_transition_length(0);
    }
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
        inst->set_strip_tag(this->strip_tag_);
        inst->set_ha_pixel_enabled(this->ha_pixel_enabled_);
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
    master_fx->set_strip_tag(this->strip_tag_);
    master_fx->set_ha_pixel_enabled(this->ha_pixel_enabled_);
    bound = true;
  }

  if (!bound) {
    ESP_LOGW(TAG, "  FAILED to bind — no CFXAddressableLightEffect found");
  }

  this->last_triggered_percentage_ = -1.0f;
  this->last_triggered_pixel_ = -1;
  this->completion_reported_ = false;
  this->is_running_ = true;
  this->is_starting_ = false;

  // Update Select UI to reflect the active sequence
  if (CFXSequenceSelect::instance != nullptr) {
    CFXSequenceSelect::instance->publish_state_silent(this->name_);
  }

  // Reset duration timer
  this->duration_start_ms_ = millis();
  this->duration_complete_fired_ = false;

  this->report_event_start();
}

void CFXSequence::stop() {
  if (this->is_stopping_ || !this->is_running_)
    return;
  this->is_stopping_ = true;
  this->is_running_ = false;
  this->duration_complete_fired_ = false;

  ESP_LOGD(TAG, "Stopping CFX Sequence '%s'...", this->name_.c_str());

  this->clear_active_binding();

  // Restore States: Only if explicitly requested
  if (this->restore_state_) {
    size_t light_idx = 0;
    for (auto *l : this->lights_) {
      if (light_idx < this->saved_states_.size()) {
        auto saved = this->saved_states_[light_idx];
        auto call = l->make_call();

        // Restore transition first
        call.set_transition_length(0);

        // Restore state
        bool turning_on = saved.values.is_on();
        call.set_state(turning_on);

        // Restore mode and brightness.
        // Guard: never pass UNKNOWN to set_color_mode — ESPHome logs a warning.
        // The save-time resolution above should prevent this, but be defensive.
        if (saved.color_mode != light::ColorMode::UNKNOWN)
          call.set_color_mode(saved.color_mode);
        call.set_brightness(saved.values.get_brightness());
        call.set_rgb(saved.values.get_red(), saved.values.get_green(),
                     saved.values.get_blue());

        // Only set white if the strip supports it in this mode
        if (l->get_traits().supports_color_mode(saved.color_mode) &&
            (saved.color_mode == light::ColorMode::RGB_WHITE ||
             saved.color_mode == light::ColorMode::RGB_COLD_WARM_WHITE ||
             saved.color_mode == light::ColorMode::COLD_WARM_WHITE ||
             saved.color_mode == light::ColorMode::WHITE)) {
          call.set_white(saved.values.get_white());
        }

        // Only restore effect if the light is on
        // Setting an effect (even "None") while turning off triggers warnings.
        if (turning_on) {
          call.set_effect(saved.effect);
        }

        call.perform();
      }
      light_idx++;
    }
  } else {
    // restore: false — clear the effect first, then turn lights off.
    // Two separate calls are required: ESPHome rejects set_effect("None")
    // when combined with set_state(false) in the same call, producing:
    //   [W] 'RGB Light': cannot start effect when turning off
    // If the effect is not cleared before turning off, ESPHome remembers it
    // and re-applies it the next time the light turns on.
    for (auto *l : this->lights_) {
      auto clear_call = l->make_call();
      clear_call.set_effect("None");
      clear_call.set_transition_length(0);
      clear_call.perform();

      auto off_call = l->make_call();
      off_call.set_state(false);
      off_call.set_transition_length(0);
      off_call.perform();
    }
  }

  this->is_stopping_ = false;

  // Update Select UI to reflect the stopped sequence
  if (CFXSequenceSelect::instance != nullptr &&
      CFXSequenceSelect::instance->has_state()) {
    const std::string &current = CFXSequenceSelect::instance->current_option();
    if (!current.empty() && this->name_ == current) {
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

void CFXSequence::force_stop_all() {
  if (this->is_stopping_)
    return;
  this->is_stopping_ = true;
  this->is_running_ = false;

  ESP_LOGD(TAG, "Force stop all: '%s'", this->name_.c_str());

  this->clear_active_binding();

  // Always turn lights off — ignore restore_state_.
  // Two calls required: see stop() else branch for explanation.
  for (auto *l : this->lights_) {
    auto clear_call = l->make_call();
    clear_call.set_effect("None");
    clear_call.set_transition_length(0);
    clear_call.perform();

    auto off_call = l->make_call();
    off_call.set_state(false);
    off_call.set_transition_length(0);
    off_call.perform();
  }

  this->is_stopping_ = false;
}

void CFXSequence::stop_all() {
  std::set<light::LightState *> unique_lights;
  for (auto *seq : CFXSequence::instances) {
    for (auto *l : seq->lights_) {
      unique_lights.insert(l);
    }
    if (seq->is_running_) {
      seq->is_running_ = false;
      seq->clear_active_binding();
      seq->duration_complete_fired_ = false;
    }
  }

  // Two calls required: see stop() else branch for explanation.
  for (auto *l : unique_lights) {
    auto clear_call = l->make_call();
    clear_call.set_effect("None");
    clear_call.set_transition_length(0);
    clear_call.perform();

    auto off_call = l->make_call();
    off_call.set_state(false);
    off_call.set_transition_length(0);
    off_call.perform();
  }
}

void CFXSequence::clear_active_binding() {
  // Clear binding on ALL effect instances that point to this sequence.
  // is_sequence_outro is only set when the sequence has already reported
  // completion (report_event_complete() was called). This prevents double-
  // firing of cfx_complete when the outro runs after an iteration/duration
  // completion, while still allowing cfx_complete to fire when the sequence
  // is interrupted externally (e.g. light turned off by user) without having
  // previously completed.
  for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
    if (inst->get_active_sequence() == this) {
      inst->set_is_sequence_outro(this->completion_reported_);
      inst->set_active_sequence(nullptr, {}, {}, {}, 0);
    }
  }
}

void CFXSequence::check_duration() {
  if (!this->is_running_)
    return;
  if (this->duration_ms_ == 0)
    return;
  if (this->duration_complete_fired_)
    return;
  if ((millis() - this->duration_start_ms_) >= this->duration_ms_) {
    ESP_LOGD(TAG, "Sequence '%s': duration %u ms elapsed — completing",
             this->name_.c_str(), this->duration_ms_);
    this->duration_complete_fired_ = true;
    this->report_event_complete();
    this->stop();
  }
}

void CFXSequence::report_event_start() {
  ESP_LOGV(TAG, "Sequence '%s': on_start triggers firing", this->id_.c_str());
  for (auto *t : this->on_start_triggers_) {
    t->trigger();
  }
  // NOTE: cfx_start HA event is fired by CFXAddressableLightEffect::start()
  // unconditionally for all effects and all paths. Do NOT fire it here again.
}

void CFXSequence::report_event_complete() {
  ESP_LOGD(TAG, "Sequence '%s': on_complete triggers firing",
           this->id_.c_str());
  this->completion_reported_ = true;
  for (auto *t : this->on_complete_triggers_) {
    t->trigger();
  }
  CFXEventManager::get().queue_event("cfx_complete");
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
  // total_pixels - 1 ensures that the last pixel maps to 100%
  float current_percentage = (total_pixels > 1) ? (float)current_pixel / (float)(total_pixels - 1) : 1.0f;

  // Reset stale tracking state at the start of a new forward pass.
  // The adapter (cfx_addressable_light_effect.cpp) suppresses calls to this
  // function during the erase/return phase via runner->is_return_phase_.
  // This means last_triggered_percentage_ retains its end-of-forward-pass
  // value (~0.98) when the new forward pass begins at pixel=0.
  // Without this reset, the backward-crossing condition fires all mid-cycle
  // targets simultaneously at pixel=0 because:
  //   current(0.0) <= target(0.25/0.50/0.75) AND last(0.98) > target → crossed
  // Resetting to -1.0f here causes the initial first-run path to handle
  // pixel=0 correctly: 0.0 >= 0.25 is false, so nothing fires at the start.
  if (this->last_triggered_percentage_ > 0.8f && current_percentage < 0.2f) {
    this->last_triggered_percentage_ = -1.0f;
  }

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

  // Check runtime milestones (cfx_reach) via global manager
  CFXEventManager::get().check_milestones(current_percentage * 100.0f);
}

void CFXSequence::CFXSequenceListener::on_light_remote_values_update() {
  if (this->parent_ != nullptr && this->parent_->is_running() && !this->light_->remote_values.is_on()) {
    ESP_LOGD("cfx_sequence",
             "Sequence '%s' stopping because light '%s' turned off",
             this->parent_->get_name().c_str(), this->light_->get_name().c_str());
    this->parent_->stop();
  }
}

// ----------------------------------------------------
// Runtime Configurable Entities (Number Inputs)
// ----------------------------------------------------

void CFXProgressStepNumber::setup() {
  uint8_t restored;
  // CFX-022: XOR with version salt 0x01 to invalidate the old preference slot
  // that stored the step-10 default. Devices upgrading from pre-CFX-022 will
  // not find a saved value here and will correctly fall back to step-5.
  // Users who explicitly saved a custom value before this version will need to
  // set it once more from the HA UI — a one-time migration cost.
  this->pref_ = global_preferences->make_preference<uint8_t>(this->get_object_id_hash() ^ 0x01u);
  if (this->pref_.load(&restored)) {
    this->publish_state(restored);
    CFXEventManager::get().set_progress_step(restored);
  } else {
    this->publish_state(5.0f); // Default 5% (CFX-022)
    CFXEventManager::get().set_progress_step(5);
  }
}

void CFXProgressStepNumber::control(float value) {
  uint8_t step = (uint8_t)value;
  this->publish_state(value);
  this->pref_.save(&step); // saves to the CFX-022 slot (hash ^ 0x01)
  CFXEventManager::get().set_progress_step(step);
}

void CFXStopAllButton::press_action() {
  ESP_LOGD(TAG, "Stop All: stopping all sequences and forcing lights off");
  CFXSequence::stop_all();
  // Update Select UI
  if (CFXSequenceSelect::instance != nullptr) {
    CFXSequenceSelect::instance->publish_state_silent("None");
  }
}

#ifdef USE_API
void CFXSequenceServiceHandler::setup() {
  this->register_service(
      &CFXSequenceServiceHandler::on_sequence_start,
      "cfx_sequence_start",
      {"sequence"}
  );
  this->register_service(
      &CFXSequenceServiceHandler::on_sequence_stop,
      "cfx_sequence_stop",
      {"sequence"}
  );
}

void CFXSequenceServiceHandler::on_sequence_start(std::string sequence_name) {
  ESP_LOGD(TAG, "Service: cfx_sequence_start('%s')", sequence_name.c_str());
  uint8_t match_count = 0;
  for (auto *seq : CFXSequence::instances) {
    if (seq->get_name() == sequence_name) {
      match_count++;
      if (match_count == 1)
        seq->start(); // act on first match only
    }
  }
  if (match_count == 0)
    ESP_LOGW(TAG, "cfx_sequence_start: '%s' not found",
             sequence_name.c_str());
  if (match_count > 1)
    ESP_LOGW(TAG,
             "cfx_sequence_start: '%s' matched %u sequences — using first. "
             "Sequence names must be unique.",
             sequence_name.c_str(), match_count);
}

void CFXSequenceServiceHandler::on_sequence_stop(std::string sequence_name) {
  ESP_LOGD(TAG, "Service: cfx_sequence_stop('%s')", sequence_name.c_str());
  uint8_t match_count = 0;
  for (auto *seq : CFXSequence::instances) {
    if (seq->get_name() == sequence_name) {
      match_count++;
      if (match_count == 1)
        seq->stop(); // act on first match only
    }
  }
  if (match_count == 0)
    ESP_LOGW(TAG, "cfx_sequence_stop: '%s' not found",
             sequence_name.c_str());
  if (match_count > 1)
    ESP_LOGW(TAG,
             "cfx_sequence_stop: '%s' matched %u sequences — using first. "
             "Sequence names must be unique.",
             sequence_name.c_str(), match_count);
}
#endif

} // namespace cfx_sequence
} // namespace esphome

#ifdef CHIMERAFX_NEED_API_SYMBOLS
namespace esphome {
namespace api {
template<> std::string get_execute_arg_value<std::string>(const ExecuteServiceArgument &arg) { return arg.string_; }
template<> enums::ServiceArgType to_service_arg_type<std::string>() { return enums::SERVICE_ARG_TYPE_STRING; }
} // namespace api
} // namespace esphome
#endif
