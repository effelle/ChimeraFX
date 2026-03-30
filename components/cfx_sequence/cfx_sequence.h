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

namespace esphome {
namespace cfx_sequence {

class CfxSeqOnStartTrigger : public ::esphome::Trigger<> {};
class CfxSeqOnBeginTrigger    : public ::esphome::Trigger<> {};
class CfxSeqOnStopTrigger     : public ::esphome::Trigger<> {};
class CfxSeqOnCompleteTrigger : public ::esphome::Trigger<> {};
class CfxSeqOnReachTrigger : public esphome::Trigger<float> {
public:
  explicit CfxSeqOnReachTrigger(float target_position)
      : target_position_(target_position) {}
  float get_target_position() const { return target_position_; }

protected:
  float target_position_;
};

class CFXEventManager {
public:
  static CFXEventManager &get();
  void set_event_entity(esphome::event::Event *e) { this->event_entity_ = e; }

  // HA event delivery opt-in. When false, fire_event() is a no-op for all
  // HA-facing paths. Internal on_cfx_reach triggers are unaffected. (CFX-026)
  void set_ha_events_enabled(bool enabled) { this->ha_events_enabled_ = enabled; }

  void add_known_tag(const std::string &tag) {
    for (const auto &t : this->known_tags_)
      if (t == tag) return;
    this->known_tags_.push_back(tag);
  }
  void fire_event(const char *type);
  void flush_pending();

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

protected:
  CFXEventManager() = default;
  esphome::event::Event *event_entity_{nullptr};
  bool ha_events_enabled_{true};
  bool discovery_done_{false};
  std::vector<std::string> known_tags_;

  static constexpr uint8_t DEFERRED_QUEUE_SIZE = 32;
  std::string deferred_queue_[DEFERRED_QUEUE_SIZE];
  uint8_t deferred_write_{0};
  uint8_t deferred_read_{0};
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
  void set_mirror(bool mirror) { this->mirror_ = mirror; }
  void set_intro(uint8_t intro) { this->intro_ = intro; }
  void set_outro(uint8_t outro) { this->outro_ = outro; }
  void set_inout_duration(float dur) { this->inout_duration_ = dur; }

  esphome::optional<uint8_t> get_speed() const { return this->speed_; }
  esphome::optional<uint8_t> get_intensity() const { return this->intensity_; }
  esphome::optional<uint8_t> get_palette() const { return this->palette_; }
  esphome::optional<float> get_brightness() const { return this->brightness_; }
  uint32_t get_iterations() const { return this->iterations_; }
  void set_duration_ms(uint32_t ms) { this->duration_ms_ = ms; }
  uint32_t get_duration_ms() const { return this->duration_ms_; }

  // Strip identity tag — YAML id of the first target light, injected by
  // codegen. Pre-loaded into CFXEventManager before perform() fires. (CFX-024)
  void set_strip_tag(const std::string &tag) { this->strip_tag_ = tag; }
  const std::string &get_strip_tag() const { return this->strip_tag_; }

  std::string get_id() const { return this->id_; }
  std::string get_name() const { return this->name_; }

  void add_on_start_trigger(CfxSeqOnStartTrigger *t) {
    this->on_start_triggers_.push_back(t);
  }
  void add_on_begin_trigger(CfxSeqOnBeginTrigger *t) {
    this->on_begin_triggers_.push_back(t);
  }
  void add_on_stop_trigger(CfxSeqOnStopTrigger *t) {
    this->on_stop_triggers_.push_back(t);
  }
  void add_on_complete_trigger(CfxSeqOnCompleteTrigger *t) {
    this->on_complete_triggers_.push_back(t);
  }
  void add_on_reach_trigger(CfxSeqOnReachTrigger *t) {
    this->on_reach_triggers_.push_back(t);
  }

  // Called by bound effects to report tracking
  void report_event_start();
  void report_event_begin();
  void report_event_stop();
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

protected:
  std::string id_;
  std::string name_;
  std::string effect_;

  esphome::optional<uint8_t> speed_;
  esphome::optional<uint8_t> intensity_;
  esphome::optional<uint8_t> palette_;
  esphome::optional<float> brightness_;
  esphome::optional<bool>    mirror_;
  esphome::optional<uint8_t> intro_;
  esphome::optional<uint8_t> outro_;
  esphome::optional<float>   inout_duration_;
  uint32_t iterations_{0};
  bool restore_state_{true};
  uint32_t duration_ms_{0};
  std::string strip_tag_{};      // CFX-024: YAML id of first target light
  uint32_t duration_start_ms_{0};
  bool duration_complete_fired_{false};

  std::vector<light::LightState *> lights_;
  // Runtime-configurable entities

  std::vector<CfxSeqOnStartTrigger *> on_start_triggers_;
  std::vector<CfxSeqOnBeginTrigger *>   on_begin_triggers_;
  std::vector<CfxSeqOnStopTrigger *>    on_stop_triggers_;
  std::vector<CfxSeqOnCompleteTrigger *> on_complete_triggers_;
  std::vector<CfxSeqOnReachTrigger *> on_reach_triggers_;

  float last_triggered_percentage_{-1.0f};
  int32_t last_triggered_pixel_{-1};

  // Per-instance milestone tracking — decoupled from CFXEventManager singleton
  // so concurrent strips each maintain their own counter. (multi-strip fix)
  static constexpr uint8_t MILESTONE_STEP    = 25;
  static constexpr uint8_t MAX_MILESTONES    = 4;  // 25..100 in steps of 25
  uint8_t  last_fired_milestone_{0};
  bool     milestone_fired_this_frame_{false};
  // Strings built on-demand — no pre-computed array, matching the effect side.
  // CFXSequence instances are few (one per declared sequence) so memory is not
  // the concern here, but consistency with the effect side is cleaner.

  void rebuild_milestone_strings_() {}  // no-op: strings built at fire time
  void reset_milestones_() {
    this->last_fired_milestone_ = 0;
    this->milestone_fired_this_frame_ = false;
  }

  // Sweep all milestones crossed since last call. While loop ensures no
  // milestone is skipped even when the frame step > 5%. (CFX sweep fix)
  void check_milestones_(float current_pct) {
    this->milestone_fired_this_frame_ = false;
    uint8_t next = this->last_fired_milestone_ + MILESTONE_STEP;
    while (current_pct >= next && next <= 100) {
      this->last_fired_milestone_ = next;
      this->milestone_fired_this_frame_ = true;
      char buf[48];
      snprintf(buf, sizeof(buf), "cfx_reach:%s:%u",
               this->strip_tag_.c_str(), (unsigned)this->last_fired_milestone_);
      CFXEventManager::get().fire_event(buf);
      next = this->last_fired_milestone_ + MILESTONE_STEP;
    }
    // Auto-reset when a new forward pass begins (pct wraps back to ~0)
    if (current_pct < this->last_fired_milestone_) {
      if (this->last_fired_milestone_ >= 100 || current_pct >= 100.0f)
        this->last_fired_milestone_ = 0;
    }
  }

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
        seq->reset_milestones_();
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


// cfx_set action — applies CFX parameters (speed/intensity/palette/brightness)
// to the active CFX effect on a target light, and optionally starts an effect.
// Parameters override sequence-injected values and persist until the next
// start() call clears them. Works with both bare light.turn_on and cfx_sequence.
// play() is implemented in cfx_sequence.cpp where cfx_addressable_light_effect.h
// is already included — the chimera_fx namespace is not available in this header.
class CfxSetActionBase {
public:
  void set_light(light::LightState *light) { this->light_ = light; }
  void set_effect(const std::string &effect) { this->effect_ = effect; }
  void set_speed(uint8_t v)      { this->speed_      = v; }
  void set_intensity(uint8_t v)  { this->intensity_  = v; }
  void set_palette(uint8_t v)    { this->palette_    = v; }
  void set_brightness(float v)   { this->brightness_ = v; }
  void set_mirror(bool v)        { this->mirror_     = v; }
  void set_intro(uint8_t v)      { this->intro_      = v; }
  void set_outro(uint8_t v)      { this->outro_      = v; }
  void set_inout_duration(float v) { this->inout_duration_ = v; }

protected:
  void do_play_();

  light::LightState *light_{nullptr};
  std::string effect_{};
  esphome::optional<uint8_t> speed_{};
  esphome::optional<uint8_t> intensity_{};
  esphome::optional<uint8_t> palette_{};
  esphome::optional<float>   brightness_{};
  esphome::optional<bool>    mirror_{};
  esphome::optional<uint8_t> intro_{};
  esphome::optional<uint8_t> outro_{};
  esphome::optional<float>   inout_duration_{};
};

template <typename... Ts>
class CfxSetAction : public CfxSetActionBase, public ::esphome::Action<Ts...> {
public:
  void play(const Ts &...x) override { this->do_play_(); }
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
