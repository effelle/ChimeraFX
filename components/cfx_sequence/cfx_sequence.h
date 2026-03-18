#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/button/button.h"
#ifdef USE_API
#include "esphome/components/api/custom_api_device.h"
#endif
#include <atomic>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace esphome {
namespace cfx_sequence {

class CfxSeqOnStartTrigger : public ::esphome::Trigger<> {};
class CfxSeqOnCompleteTrigger : public ::esphome::Trigger<> {};
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
  void set_api_device(esphome::api::CustomAPIDevice *d) { this->api_device_ = d; }

  // HA event delivery opt-in. When false, fire_event() is a no-op for all
  // HA-facing paths. Internal on_cfx_reach triggers are unaffected. (CFX-026)
  void set_ha_events_enabled(bool enabled) { this->ha_events_enabled_ = enabled; }


  void add_known_tag(const std::string &tag) {
    for (const auto &t : this->known_tags_)
      if (t == tag) return;
    this->known_tags_.push_back(tag);
  }
  void fire_event(const char *type);
  void queue_event(const char *type);
  void flush_pending();
  void report_progress(float pct);
  void report_last_pixel(int32_t pixel);

  // Strip tag: set once at start() time from the effect adapter.
  // Also rebuilds the pre-computed milestone event strings. (CFX-024)
  void set_strip_tag(const std::string &tag) {
    this->strip_tag_ = tag;
    if (!tag.empty())
      this->add_known_tag(tag);
    this->rebuild_milestone_strings_();
  }
  const std::string &get_strip_tag() const { return this->strip_tag_; }

  // cfx_pixel opt-in: when false, pixel_advanced() updates the sensor but
  // does NOT fire cfx_pixel to HA. On-device on_cfx_pixel YAML triggers are
  // unaffected — they run before pixel_advanced() is called. (CFX-023)
  // cfx_pixel: on-device only. pixel_advanced() is called from the effect
  // adapter for on_cfx_pixel YAML triggers — no HA API write. (CFX-026)
  void pixel_advanced(uint16_t pixel);

  // High-level milestone logic (shared for Sequence and Manual)
  void check_milestones(float current_pct);
  void reset_milestones() {
    this->last_fired_milestone_ = 0;
    this->pass_count_++;
  }

  // Push one event string onto the deferred queue (called from render loop).
  // Zero blocking — just a ring buffer write. (CFX-025)
  void push_deferred(const std::string &evt) {
    uint8_t next = (this->deferred_write_ + 1) % DEFERRED_QUEUE_SIZE;
    if (next == this->deferred_read_) {
      ESP_LOGW("cfx_seq", "deferred queue full, dropping '%s'", evt.c_str());
      return;
    }
    this->deferred_queue_[this->deferred_write_] = evt;
    this->deferred_write_ = next;
  }

  // Returns true if check_milestones() fired cfx_reach during this frame.
  // Used by the effect adapter to suppress cfx_pixel on the same frame so
  // cfx_reach always arrives in its own WebSocket frame to HA. (CFX-022)
  bool was_milestone_fired() const { return this->milestone_fired_this_frame_; }
  void clear_milestone_fired() { this->milestone_fired_this_frame_ = false; }

protected:
  CFXEventManager() = default;
  esphome::event::Event *event_entity_{nullptr};
  esphome::sensor::Sensor *progress_pct_sensor_{nullptr};
  esphome::sensor::Sensor *last_pixel_sensor_{nullptr};
  esphome::api::CustomAPIDevice *api_device_{nullptr};
  bool ha_events_enabled_{true};   // CFX-026: gated by cfx_control ID 6
  bool discovery_done_{false};
  std::vector<std::string> known_tags_;  // all tags seen at startup for discovery

  // Strip identity tag — set by the effect adapter at start() time.
  // Used to build tagged event strings: "cfx_reach:strip_a". (CFX-023)
  std::string strip_tag_{};

  // Pre-computed milestone event strings: milestone_events_[i] holds the
  // string for milestone value (i+1)*progress_step_, e.g. "cfx_reach:ws_strip:5".
  // Built once in set_strip_tag() so check_milestones() does zero allocation
  // on the hot path. (CFX-024)
  static constexpr uint8_t MAX_MILESTONES = 20; // 5..100 in steps of 5
  std::string milestone_events_[MAX_MILESTONES];

  void rebuild_milestone_strings_() {
    if (this->strip_tag_.empty()) {
      for (uint8_t i = 0; i < MAX_MILESTONES; i++)
        milestone_events_[i] = "cfx_reach";
      return;
    }
    char buf[64];
    for (uint8_t i = 0; i < MAX_MILESTONES; i++) {
      uint8_t m = (i + 1) * this->progress_step_;
      snprintf(buf, sizeof(buf), "cfx_reach:%s:%u", this->strip_tag_.c_str(), (unsigned)m);
      milestone_events_[i] = buf;
    }
  }

  uint8_t progress_step_{5};
  uint8_t last_fired_milestone_{0};
  uint16_t pass_count_{0};

  // Set to true for the remainder of the frame in which cfx_reach fires.
  bool milestone_fired_this_frame_{false};

  // Deferred event queue — ALL events are pushed here from the hot path
  // (render loop) and drained ONE PER loop() CALL from flush_pending().
  // This decouples milestone firing from API writes entirely: the render loop
  // does zero blocking WebSocket calls; the API writes happen in loop()
  // between frames, each within ESPHome's 30ms component budget. (CFX-025)
  //
  // Capacity 32: forward pass fires 20 milestones + cfx_idle + cfx_start +
  // cfx_complete + spare = 24 max in flight. 32 gives comfortable headroom.
  // At 50Hz loop rate the queue drains in 640ms — well within the return
  // phase at any speed. String storage avoids const char* lifetime issues.
  static constexpr uint8_t DEFERRED_QUEUE_SIZE = 32;
  std::string deferred_queue_[DEFERRED_QUEUE_SIZE];
  uint8_t deferred_write_{0};
  uint8_t deferred_read_{0};

  // Legacy small queue kept for cfx_complete (queued from a different task).
  // CFX-021: atomic to guard queue head/tail across FreeRTOS tasks.
  static constexpr uint8_t PENDING_QUEUE_SIZE = 3;
  const char *pending_events_[PENDING_QUEUE_SIZE]{nullptr, nullptr, nullptr};
  std::atomic<uint8_t> pending_write_{0};
  std::atomic<uint8_t> pending_read_{0};

  // Deferred event pipeline — ensures cfx_idle lands in a separate
  // WebSocket frame from the real event so HA State triggers fire reliably.
  static constexpr uint32_t CFX_IDLE_HOLD_MS = 200;
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
  void set_pixel_step(uint16_t step) { this->pixel_step_ = step; }

  esphome::optional<uint8_t> get_speed() const { return this->speed_; }
  esphome::optional<uint8_t> get_intensity() const { return this->intensity_; }
  esphome::optional<uint8_t> get_palette() const { return this->palette_; }
  esphome::optional<float> get_brightness() const { return this->brightness_; }
  uint16_t get_pixel_step() const { return this->pixel_step_; }
  uint32_t get_iterations() const { return this->iterations_; }
  void set_duration_ms(uint32_t ms) { this->duration_ms_ = ms; }
  uint32_t get_duration_ms() const { return this->duration_ms_; }

  // Strip identity tag — YAML id of the first target light, injected by
  // codegen. Pre-loaded into CFXEventManager before perform() fires. (CFX-024)
  void set_strip_tag(const std::string &tag) { this->strip_tag_ = tag; }
  const std::string &get_strip_tag() const { return this->strip_tag_; }

  // Opt-in flag for cfx_pixel events to HA. Passed to bound effects at
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
  void check_duration();
  bool get_duration_complete_fired() const { return this->duration_complete_fired_; }
  void clear_active_binding();
  void force_stop_all();
  static void stop_all();

  // HA event integration
  void fire_event(const char *type) {
    CFXEventManager::get().fire_event(type);
  }
  void set_event_entity(esphome::event::Event *e) {
    CFXEventManager::get().set_event_entity(e);
  }
  void set_ha_events_enabled(bool enabled) {
    CFXEventManager::get().set_ha_events_enabled(enabled);
  }
  void add_known_tag(const std::string &tag) {
    CFXEventManager::get().add_known_tag(tag);
  }

  // Runtime configurable entities
  void set_progress_sensor(esphome::sensor::Sensor *sensor) { CFXEventManager::get().set_progress_sensor(sensor); }
  void set_last_pixel_sensor(esphome::sensor::Sensor *sensor) { CFXEventManager::get().set_last_pixel_sensor(sensor); }

  // Milestone tracking
  void check_milestones(uint8_t current_pct) {
    CFXEventManager::get().check_milestones(current_pct);
  }

protected:
  std::string id_;
  std::string name_;
  std::string effect_;

  esphome::optional<uint8_t> speed_;
  esphome::optional<uint8_t> intensity_;
  esphome::optional<uint8_t> palette_;
  esphome::optional<float> brightness_;
  uint32_t iterations_{0};
  uint16_t pixel_step_{0};  // 0 = auto-computed
  bool restore_state_{true};
  uint32_t duration_ms_{0};
  std::string strip_tag_{};      // CFX-024: YAML id of first target light
  uint32_t duration_start_ms_{0};
  bool duration_complete_fired_{false};

  std::vector<light::LightState *> lights_;
  // Runtime-configurable entities

  std::vector<CfxSeqOnStartTrigger *> on_start_triggers_;
  std::vector<CfxSeqOnCompleteTrigger *> on_complete_triggers_;
  std::vector<CfxSeqOnReachTrigger *> on_reach_triggers_;
  std::vector<CfxSeqOnPixelNumTrigger *> on_pixel_num_triggers_;

  float last_triggered_percentage_{-1.0f};
  int32_t last_triggered_pixel_{-1};

  bool is_starting_{false};
  bool is_stopping_{false};
  bool is_running_{false};
  // Set to true when report_event_complete() has been called for this run.
  // Used by clear_active_binding() to decide whether the outro should suppress
  // cfx_complete (prevent double-fire) or allow it (first completion signal).
  bool completion_reported_{false};

  class CFXSequenceListener : public light::LightRemoteValuesListener {
  public:
    CFXSequenceListener(CFXSequence *parent, light::LightState *light)
        : parent_(parent), light_(light) {}
    void on_light_remote_values_update() override;
    void nullify()                  { this->parent_ = nullptr; }
    void reinstate(CFXSequence *s)  { this->parent_ = s; } // CFX-020: re-arm for new run

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

template <typename... Ts> class StartAction : public ::esphome::Action<Ts...> {
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

template <typename... Ts> class StopAction : public ::esphome::Action<Ts...> {
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
  void loop() override {
    CFXEventManager::get().flush_pending();
    for (auto *seq : CFXSequence::instances) {
      seq->check_duration();
    }
  }
  void control(const std::string &value) override;
  void set_event_entity(esphome::event::Event *e) {
    CFXEventManager::get().set_event_entity(e);
  }
  void set_ha_events_enabled(bool enabled) {
    CFXEventManager::get().set_ha_events_enabled(enabled);
  }
  void add_known_tag(const std::string &tag) {
    CFXEventManager::get().add_known_tag(tag);
  }

  /// Safely update the UI without triggering the callback.
  void publish_state_silent(const std::string &value);

  static CFXSequenceSelect *instance;
  // CFX-012: Changed from plain bool to std::atomic<bool> to prevent a race
  // condition on multi-core ESP32 where a FreeRTOS task could read false before
  // publish_state_silent() sets it back, causing an unintended recursive start.
  static std::atomic<bool> suppress_callback_;
};


class CFXStopAllButton : public ::esphome::button::Button,
                         public ::esphome::Component {
public:
  void press_action() override;
};

#ifdef USE_API
class CFXSequenceServiceHandler : public ::esphome::api::CustomAPIDevice,
                                   public ::esphome::Component {
public:
  void setup() override;

private:
  void on_sequence_start(std::string sequence_name);
  void on_sequence_stop(std::string sequence_name);
};
#endif

} // namespace cfx_sequence
} // namespace esphome
