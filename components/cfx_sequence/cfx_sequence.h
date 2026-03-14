#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/components/select/select.h"
#include "esphome/components/event/event.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/text/text.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include <atomic>    // CFX-012: for std::atomic<bool>
#include <algorithm> // CFX-011: for std::find in destructor
#include <cstdint>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace esphome {
namespace cfx_sequence {

class CfxSeqOnStartTrigger : public esphome::Trigger<> {};
class CfxSeqOnCompleteTrigger : public esphome::Trigger<> {};
class CfxSeqOnReachTrigger : public esphome::Trigger<float> {
public:
  explicit CfxSeqOnReachTrigger(float target_position)
      : target_position_(target_position) {}
  float get_target_position() const { return target_position_; }

protected:
  float target_position_;
};

class CfxSeqOnPixelNumTrigger : public esphome::Trigger<int32_t> {
public:
  explicit CfxSeqOnPixelNumTrigger(int32_t target_pixel)
      : target_pixel_(target_pixel) {}
  int32_t get_target_pixel() const { return target_pixel_; }

protected:
  int32_t target_pixel_;
};

class CFXEventManager {
public:
  static CFXEventManager &get();

  void set_event_entity(esphome::event::Event *e) { this->event_entity_ = e; }
  void set_progress_sensor(esphome::sensor::Sensor *s) { this->progress_pct_sensor_ = s; }
  void set_last_pixel_sensor(esphome::sensor::Sensor *s) { this->last_pixel_sensor_ = s; }
  void set_progress_step(uint8_t step) { this->progress_step_ = step; }

  void fire_event(const char *type);
  void flush_pending();
  void report_progress(float pct);
  void report_last_pixel(int32_t pixel);
  
  // High-level milestone logic (shared for Sequence and Manual)
  void check_milestones(float current_pct);
  void reset_milestones() { this->last_fired_milestone_ = 0; }
  void pixel_advanced(uint16_t pixel, const std::vector<uint16_t> &whitelist);

protected:
  CFXEventManager() = default;
  esphome::event::Event *event_entity_{nullptr};
  esphome::sensor::Sensor *progress_pct_sensor_{nullptr};
  esphome::sensor::Sensor *last_pixel_sensor_{nullptr};
  
  uint8_t progress_step_{10};
  uint8_t last_fired_milestone_{0};

  // Deferred event pipeline — ensures cfx_idle lands in a separate
  // WebSocket frame from the real event so HA State triggers fire reliably.
  static constexpr uint32_t CFX_IDLE_HOLD_MS = 200;
  const char *pending_event_{nullptr};
  bool pending_idle_{false};
  uint32_t idle_hold_until_ms_{0};
};

class CFXSequence {
public:
  CFXSequence(const std::string &id, const std::string &name,
              const std::string &effect, bool restore = true);
  // CFX-011: Destructor removes this from the static instances vector to
  // prevent dangling pointers when objects are destroyed.
  ~CFXSequence();

  // Sequence runtime controllers
  void start();
  void stop();
  void force_reset();

  void add_light(light::LightState *state) { this->lights_.push_back(state); }

  // Custom payload bindings
  void set_speed(uint8_t speed) { this->speed_ = speed; }
  void set_intensity(uint8_t intensity) { this->intensity_ = intensity; }
  void set_palette(uint8_t palette) { this->palette_ = palette; }
  void set_iterations(uint32_t iterations) { this->iterations_ = iterations; }
  void set_brightness(float brightness) { this->brightness_ = brightness; }

  esphome::optional<uint8_t> get_speed() const { return this->speed_; }
  esphome::optional<uint8_t> get_intensity() const { return this->intensity_; }
  esphome::optional<uint8_t> get_palette() const { return this->palette_; }
  esphome::optional<float> get_brightness() const { return this->brightness_; }
  uint32_t get_iterations() const { return this->iterations_; }

  std::string get_id() const { return this->id_; }
  std::string get_name() const { return this->name_; }

  void add_on_start_trigger(CfxSeqOnStartTrigger *t) {
    this->on_start_triggers_.push_back(t);
  }
  void add_on_complete_trigger(CfxSeqOnCompleteTrigger *t) {
    this->on_complete_triggers_.push_back(t);
  }
  void add_on_reach_trigger(CfxSeqOnReachTrigger *t) {
    this->on_reach_triggers_.push_back(t);
  }
  void add_on_pixel_num_trigger(CfxSeqOnPixelNumTrigger *t) {
    this->on_pixel_num_triggers_.push_back(t);
  }

  // Called by bound effects to report tracking
  void report_event_start();
  void report_event_complete();
  void check_positional_triggers(int32_t current_pixel, int32_t total_pixels);
  void clear_active_binding();

  // HA event integration
  void fire_event(const char *type) {
    CFXEventManager::get().fire_event(type);
  }
  void set_event_entity(esphome::event::Event *e) {
    CFXEventManager::get().set_event_entity(e);
  }

  // Runtime configurable entities
  void set_progress_step(uint8_t step) { CFXEventManager::get().set_progress_step(step); }
  void set_pixel_whitelist(const std::vector<uint16_t>& pixels) { this->pixel_whitelist_ = pixels; }
  void set_progress_sensor(esphome::sensor::Sensor *sensor) { CFXEventManager::get().set_progress_sensor(sensor); }
  void set_last_pixel_sensor(esphome::sensor::Sensor *sensor) { CFXEventManager::get().set_last_pixel_sensor(sensor); }

  // Milestone tracking
  void check_milestones(uint8_t current_pct) {
    CFXEventManager::get().check_milestones(current_pct);
  }
  void pixel_advanced(uint16_t pixel);

protected:
  std::string id_;
  std::string name_;
  std::string effect_;

  esphome::optional<uint8_t> speed_;
  esphome::optional<uint8_t> intensity_;
  esphome::optional<uint8_t> palette_;
  esphome::optional<float> brightness_;
  uint32_t iterations_{0};
  bool restore_state_{true};

  std::vector<light::LightState *> lights_;
  // Runtime-configurable entities
  std::vector<uint16_t> pixel_whitelist_;


  std::vector<CfxSeqOnStartTrigger *> on_start_triggers_;
  std::vector<CfxSeqOnCompleteTrigger *> on_complete_triggers_;
  std::vector<CfxSeqOnReachTrigger *> on_reach_triggers_;
  std::vector<CfxSeqOnPixelNumTrigger *> on_pixel_num_triggers_;

  float last_triggered_percentage_{-1.0f};
  int32_t last_triggered_pixel_{-1};

  bool is_starting_{false};
  bool is_stopping_{false};
  bool is_running_{false};

  class CFXSequenceListener : public light::LightRemoteValuesListener {
  public:
    CFXSequenceListener(CFXSequence *parent, light::LightState *light)
        : parent_(parent), light_(light) {}
    void on_light_remote_values_update() override;
    void nullify() { this->parent_ = nullptr; }

  private:
    CFXSequence *parent_;
    light::LightState *light_;
  };

  struct SavedState {
    light::LightColorValues values;
    light::ColorMode color_mode;
    std::string effect;
  };
  std::vector<SavedState> saved_states_;

  struct MonitoredLight {
    light::LightState *light;
    CFXSequenceListener *listener;
  };
  std::vector<MonitoredLight> monitored_lights_;

public:
  bool is_starting() const { return this->is_starting_; }
  bool is_running() const { return this->is_running_; }
  static std::vector<CFXSequence *> instances;
  bool owns_light(light::LightState *state) {
    for (auto *l : this->lights_) {
      if (l == state)
        return true;
    }
    return false;
  }
};

template <typename... Ts> class StartAction : public Action<Ts...> {
public:
  StartAction(const std::string &target_id) : target_id_(target_id) {}

  void play(const Ts &...x) override {
    for (auto *seq : CFXSequence::instances) {
      if (seq->get_id() == this->target_id_) {
        // Reset global milestone tracking for the new sequence
        CFXEventManager::get().reset_milestones();
        seq->start();
        return;
      }
    }
  }

protected:
  std::string target_id_;
};

template <typename... Ts> class StopAction : public Action<Ts...> {
public:
  StopAction(const std::string &target_id) : target_id_(target_id) {}

  void play(const Ts &...x) override {
    for (auto *seq : CFXSequence::instances) {
      if (seq->get_id() == this->target_id_) {
        seq->stop();
        return;
      }
    }
  }

protected:
  std::string target_id_;
};

class CFXSequenceSelect : public esphome::select::Select,
                          public esphome::Component {
public:
  void setup() override;
  void loop() override;
  void control(const std::string &value) override;

  /// Safely update the UI without triggering the callback.
  void publish_state_silent(const std::string &value);

  static CFXSequenceSelect *instance;
  // CFX-012: Changed from plain bool to std::atomic<bool> to prevent a race
  // condition on multi-core ESP32 where a FreeRTOS task could read false before
  // publish_state_silent() sets it back, causing an unintended recursive start.
  static std::atomic<bool> suppress_callback_;
};

class CFXProgressStepNumber : public esphome::number::Number, public esphome::Component {
public:
  void setup() override;
  void control(float value) override;
  
protected:
  esphome::ESPPreferenceObject pref_;
};

class CFXPixelWatchText : public esphome::text::Text, public esphome::Component {
public:
  void setup() override;
  void control(const std::string &value) override;
  
protected:
  esphome::ESPPreferenceObject pref_;
};

} // namespace cfx_sequence
} // namespace esphome
