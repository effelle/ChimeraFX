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
#ifndef USE_API_USER_DEFINED_ACTIONS
#define USE_API_USER_DEFINED_ACTIONS
#endif
#ifndef USE_API_CUSTOM_SERVICES
#define USE_API_CUSTOM_SERVICES
#endif
#include "esphome/components/api/custom_api_device.h"
#endif
#include <atomic>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <map>
#include <set>
#include <string>

#include "../cfx_effect/cfx_event_manager.h"

namespace esphome {
namespace chimera_fx {
class CFXAddressableLightEffect;  // Forward declaration for binding helpers
}
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

using CFXEventManager = chimera_fx::CFXEventManager;

class CFXSequence {
public:
  enum class StopMode : uint8_t {
    SELF = 0,
    TREE = 1,
  };

  CFXSequence(const std::string &id, const std::string &name,
              const std::string &effect, bool restore = true);
  // CFX-011: Destructor removes this from the static instances vector to
  // prevent dangling pointers when objects are destroyed.
  ~CFXSequence();

  // Sequence runtime controllers
  void start();
  void stop();
  void stop_tree();
  void force_reset();
  // CFX-044: Called by execute_completion() when a runner signals effect_complete_.
  // Performs the correctly-ordered teardown: clear binding → restore light →
  // report_event_complete(). This guarantees that on_complete YAML automations
  // (e.g. cfx_set starting step 2) are never overwritten by a subsequent stop().
  void complete_and_notify();

  void add_light(light::LightState *state) {
    if (state == nullptr)
      return;
    if (std::find(this->lights_.begin(), this->lights_.end(), state) ==
        this->lights_.end()) {
      this->lights_.push_back(state);
      if (!this->is_running_ && !this->is_starting_) {
        this->configured_light_count_ = this->lights_.size();
      }
    }
  }

  // adopt_light(): register a light that was activated mid-sequence via cfx_set.
  // Unlike add_light() (which is only safe to call before start()), this method
  // is called while the sequence is already running. It adds the light to lights_,
  // saves its current state for restore, and registers a listener so stop() will
  // include it in cleanup — ensuring its outro plays and HA state is correct.
  void adopt_light(light::LightState *state);

  // Custom payload bindings
  void set_speed(uint8_t speed) { this->speed_ = speed; }
  void set_intensity(uint8_t intensity) { this->intensity_ = intensity; }
  void set_palette(uint8_t palette) { this->palette_ = palette; }
  void set_iterations(uint32_t iterations) { this->iterations_ = iterations; }
  void set_brightness(float brightness) { this->brightness_ = brightness; }
  void set_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    this->has_color_ = true;
    this->color_r_ = r;
    this->color_g_ = g;
    this->color_b_ = b;
    this->color_w_ = 0;
    this->color_has_white_ = false;
  }
  void set_color_rgbw(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    this->has_color_ = true;
    this->color_r_ = r;
    this->color_g_ = g;
    this->color_b_ = b;
    this->color_w_ = w;
    this->color_has_white_ = true;
  }
  void set_mirror(bool mirror) { this->mirror_ = mirror; }
  void set_intro(uint8_t intro) { this->intro_ = intro; }
  void set_outro(uint8_t outro) { this->outro_ = outro; }
  void set_inout_duration(float dur) { this->inout_duration_ = dur; }
  void set_force_white(bool force_white) { this->force_white_ = force_white; }
  void set_autotune(bool autotune) { this->autotune_ = autotune; }
  void set_ha_events(bool enabled) { this->ha_events_ = enabled; }
  bool get_ha_events() const { return this->ha_events_; }

  esphome::optional<uint8_t> get_speed() const { return this->speed_; }
  esphome::optional<uint8_t> get_intensity() const { return this->intensity_; }
  esphome::optional<uint8_t> get_palette() const { return this->palette_; }
  esphome::optional<bool> get_mirror() const { return this->mirror_; }
  esphome::optional<bool> get_autotune() const { return this->autotune_; }
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
  void check_positional_triggers(int32_t current_pixel, int32_t total_pixels, bool is_return_phase = false);
  void flush_pending_triggers();
  void check_duration();
  void process_pending_teardown();
  bool get_duration_complete_fired() const { return this->duration_complete_fired_; }
  void clear_active_binding();
  bool try_bind_effects_();
  void apply_binding_to_effect_(chimera_fx::CFXAddressableLightEffect *inst);
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

  bool is_stagger_complete() const { return this->stagger_tasks_pending_ == 0; }
  static CFXSequence *get_current_trigger_sequence() {
    return current_trigger_sequence_;
  }
  static void set_current_trigger_sequence(CFXSequence *seq) {
    current_trigger_sequence_ = seq;
  }

protected:
  enum class TeardownMode : uint8_t {
    NONE = 0,
    STOP_RESTORE,
    STOP_FORCE_OFF,
    COMPLETE_RESTORE,
    FORCE_OFF,
  };

  void begin_teardown_(TeardownMode mode);
  void finalize_teardown_();
  void stop_tree_(std::set<CFXSequence *> &visited);
  void attach_child_sequence_(CFXSequence *child);
  void detach_child_sequence_(CFXSequence *child);
  void detach_runtime_parent_();

  std::atomic<uint32_t> stagger_tasks_pending_{0};
  std::string id_;
  std::string name_;
  std::string effect_;

  esphome::optional<uint8_t> speed_;
  esphome::optional<uint8_t> intensity_;
  esphome::optional<uint8_t> palette_;
  esphome::optional<float> brightness_;
  bool has_color_{false};
  uint8_t color_r_{0};
  uint8_t color_g_{0};
  uint8_t color_b_{0};
  uint8_t color_w_{0};
  bool color_has_white_{false};
  esphome::optional<bool>    mirror_;
  esphome::optional<uint8_t> intro_;
  esphome::optional<uint8_t> outro_;
  esphome::optional<float>   inout_duration_;
  esphome::optional<bool>    force_white_;
  esphome::optional<bool>    autotune_;
  uint32_t iterations_{0};
  bool restore_state_{true};
  bool ha_events_{true};
  uint32_t duration_ms_{0};
  std::string strip_tag_{};      // CFX-024: YAML id of first target light
  uint32_t duration_start_ms_{0};
  bool duration_complete_fired_{false};

  size_t configured_light_count_{0};
  std::vector<light::LightState *> lights_;
  uint8_t runtime_depth_{0};
  CFXSequence *runtime_parent_{nullptr};
  std::vector<CFXSequence *> runtime_children_{};
  // Runtime-configurable entities

  std::vector<CfxSeqOnStartTrigger *> on_start_triggers_;
  std::vector<CfxSeqOnBeginTrigger *>   on_begin_triggers_;
  std::vector<CfxSeqOnStopTrigger *>    on_stop_triggers_;
  std::vector<CfxSeqOnCompleteTrigger *> on_complete_triggers_;
  std::vector<CfxSeqOnReachTrigger *> on_reach_triggers_;

  // CFX-042: Deferred trigger queue. Triggers crossed during apply() are
  // collected here instead of fired synchronously. flush_pending_triggers()
  // is called by the bound effect at the END of apply(), with a flat stack,
  // preventing the nested start()->new CFXRunner() stack overflow.
  struct PendingTrigger {
    CfxSeqOnReachTrigger *trigger;
    float value;
  };
  // Small fixed-bound vector: max 8 triggers per frame is more than enough.
  std::vector<PendingTrigger> pending_reach_triggers_;
  // Sequence-level on_cfx_reach dedupe for the current forward pass.
  // This prevents multi-light sequences from firing the same YAML trigger once
  // per participating light when multiple strips cross the same threshold.
  std::vector<CfxSeqOnReachTrigger *> fired_reach_triggers_;

  float last_triggered_percentage_{-1.0f};
  int32_t last_triggered_pixel_{-1};

  // Per-instance milestone tracking — decoupled from CFXEventManager singleton
  // so concurrent strips each maintain their own counter. (multi-strip fix)
  static constexpr uint8_t MILESTONE_STEP    = 10;
  static constexpr uint8_t MAX_MILESTONES    = 10;  // 10..100 in steps of 10
  uint8_t  last_fired_milestone_{0};
  bool     milestone_fired_this_frame_{false};
  static CFXSequence *current_trigger_sequence_;
  // No suppress field — intro guard is handled at the effect layer (act_->intro_active).
  // CFXSequence::check_milestones_() is only called from check_positional_triggers(),
  // which is only called from apply() after the intro_active guard below.

  void rebuild_milestone_strings_() {}  // no-op: strings built at fire time
  void reset_milestones_() {
    this->last_fired_milestone_ = 0;
    this->milestone_fired_this_frame_ = false;
    // Simple reset: main effect starts at pixel 0, so last_fired_milestone_ = 0
    // guarantees the first milestone (5%) fires correctly without any suppression.
  }

  // Sweep all milestones crossed since last call. While loop ensures no
  // milestone is skipped even when the frame step > 5%. (CFX sweep fix)
  // Note: intro guard is enforced at the effect layer (check_milestones_ in
  // cfx_addressable_light_effect.cpp returns early when intro_active is true),
  // so this sequence-side function is only reached during the main effect.
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
  TeardownMode teardown_mode_{TeardownMode::NONE};
  size_t teardown_light_index_{0};
  // bool is_stagger_complete_{true}; // Replaced by stagger_tasks_pending_ atomic
  bool duration_completion_pending_{false};  // CFX-044c: Defer duration timeout to worker
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

  // Snapshot captured at sequence start (or adopt_light() time) and used only
  // for light-state restore on teardown. Sequence/cfx_set runtime overrides
  // such as speed/intensity/palette/mirror/autotune are intentionally NOT
  // persisted here; they are transient run-scoped controls.
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
  bool has_pending_teardown() const { return this->teardown_mode_ != TeardownMode::NONE; }
  bool has_pending_triggers() const { return !this->pending_reach_triggers_.empty(); }
  bool has_pending_duration_completion() const { return this->duration_completion_pending_; }
  static std::vector<CFXSequence *> instances;
  bool owns_light(light::LightState *state) {
    for (auto *l : this->lights_) {
      if (l == state)
        return true;
    }
    return false;
  }

  // Pool ownership — set by CFXRunPool::claim(), cleared on release.
  // When true, complete_and_notify() and stop() call CFXRunPool::release()
  // so the slot returns to the pool automatically.
  bool is_pool_owned_{false};

  friend class CFXRunPool;      // allows release() to reset protected state
  friend class CfxRunActionBase; // allows do_play_() to configure protected fields

protected:
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
  void set_mode(CFXSequence::StopMode mode) { this->mode_ = mode; }

  void play(const Ts &...x) override {
    for (auto *seq : CFXSequence::instances) {
      if (seq->get_id() == this->target_id_) {
        if (this->mode_ == CFXSequence::StopMode::TREE)
          seq->stop_tree();
        else
          seq->stop();
        return;
      }
    }
  }

protected:
  std::string target_id_;
  CFXSequence::StopMode mode_{CFXSequence::StopMode::SELF};
};

class CFXSequenceSelect : public esphome::select::Select,
                          public esphome::Component {
public:
  void setup() override;
  void loop() override;
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
  void set_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    this->has_color_ = true;
    this->color_r_ = r;
    this->color_g_ = g;
    this->color_b_ = b;
    this->color_w_ = 0;
    this->color_has_white_ = false;
  }
  void set_color_rgbw(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    this->has_color_ = true;
    this->color_r_ = r;
    this->color_g_ = g;
    this->color_b_ = b;
    this->color_w_ = w;
    this->color_has_white_ = true;
  }
  void set_mirror(bool v)        { this->mirror_     = v; }
  void set_intro(uint8_t v)      { this->intro_      = v; }
  void set_outro(uint8_t v)      { this->outro_      = v; }
  void set_inout_duration(float v) { this->inout_duration_ = v; }
  void set_force_white(bool v)   { this->force_white_ = v; }
  void set_autotune(bool v)      { this->autotune_ = v; }
  void set_ha_events(bool v)     { this->ha_events_ = v; }

protected:
  void do_play_();

  light::LightState *light_{nullptr};
  std::string effect_{};
  esphome::optional<uint8_t> speed_{};
  esphome::optional<uint8_t> intensity_{};
  esphome::optional<uint8_t> palette_{};
  esphome::optional<float>   brightness_{};
  bool has_color_{false};
  uint8_t color_r_{0};
  uint8_t color_g_{0};
  uint8_t color_b_{0};
  uint8_t color_w_{0};
  bool color_has_white_{false};
  esphome::optional<bool>    mirror_{};
  esphome::optional<uint8_t> intro_{};
  esphome::optional<uint8_t> outro_{};
  esphome::optional<float>   inout_duration_{};
  esphome::optional<bool>    force_white_{};
  esphome::optional<bool>    autotune_{};
  bool ha_events_{false};
};

template <typename... Ts>
class CfxSetAction : public CfxSetActionBase, public ::esphome::Action<Ts...> {
public:
  void play(const Ts &...x) override { this->do_play_(); }
};

// ── cfx_run ──────────────────────────────────────────────────────────────────
// Spawns a fully independent, pool-backed CFXSequence at runtime.
// Each cfx_run claim allocates one slot from a fixed pool of 8 sequences.
// The spawned sequence is autonomous — it has its own milestone tracking,
// its own lifecycle events, and its own on_cfx_reach triggers. It is not
// owned by the parent and continues running after the parent completes.
//
// Pool slots are returned automatically when the spawned sequence completes
// or is stopped. If all 8 slots are in use, cfx_run is a no-op with a LOGW.
//
// Maximum nesting depth: CFX_RUN_MAX_DEPTH (default 4). Each cfx_run level
// consumes one pool slot; the guard prevents stack exhaustion on deep chains.

static constexpr uint8_t CFX_RUN_POOL_SIZE  = 8;
static constexpr uint8_t CFX_RUN_MAX_DEPTH  = 4;

// Forward declaration — pool lives in cfx_sequence.cpp
class CFXRunPool;

class CfxRunActionBase {
public:
  void set_light(light::LightState *light)     { this->light_     = light; }
  void set_effect(const std::string &effect)   { this->effect_    = effect; }
  void set_speed(uint8_t v)                    { this->speed_     = v; }
  void set_intensity(uint8_t v)                { this->intensity_ = v; }
  void set_palette(uint8_t v)                  { this->palette_   = v; }
  void set_brightness(float v)                 { this->brightness_= v; }
  void set_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    this->has_color_ = true;
    this->color_r_ = r;
    this->color_g_ = g;
    this->color_b_ = b;
    this->color_w_ = 0;
    this->color_has_white_ = false;
  }
  void set_color_rgbw(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    this->has_color_ = true;
    this->color_r_ = r;
    this->color_g_ = g;
    this->color_b_ = b;
    this->color_w_ = w;
    this->color_has_white_ = true;
  }
  void set_mirror(bool v)                      { this->mirror_    = v; }
  void set_intro(uint8_t v)                    { this->intro_     = v; }
  void set_outro(uint8_t v)                    { this->outro_     = v; }
  void set_inout_duration(float v)             { this->inout_duration_ = v; }
  void set_force_white(bool v)                 { this->force_white_ = v; }
  void set_autotune(bool v)                    { this->autotune_ = v; }
  void set_iterations(uint32_t v)              { this->iterations_ = v; }
  void set_strip_tag(const std::string &tag)   { this->strip_tag_  = tag; }
  void set_nesting_depth(uint8_t depth)        { this->nesting_depth_ = depth; }
  void set_ha_events(bool v)                   { this->ha_events_ = v; }

  // Trigger registration — called by codegen for on_cfx_reach blocks
  // inside cfx_run. Stored and transferred to the spawned sequence at play time.
  void add_on_reach_trigger(CfxSeqOnReachTrigger *t) {
    this->on_reach_triggers_.push_back(t);
  }
  void add_on_complete_trigger(CfxSeqOnCompleteTrigger *t) {
    this->on_complete_triggers_.push_back(t);
  }
  void add_on_stop_trigger(CfxSeqOnStopTrigger *t) {
    this->on_stop_triggers_.push_back(t);
  }
  void add_on_start_trigger(CfxSeqOnStartTrigger *t) {
    this->on_start_triggers_.push_back(t);
  }

protected:
  void do_play_();

  light::LightState *light_{nullptr};
  std::string effect_{};
  std::string strip_tag_{};
  esphome::optional<uint8_t>  speed_{};
  esphome::optional<uint8_t>  intensity_{};
  esphome::optional<uint8_t>  palette_{};
  esphome::optional<float>    brightness_{};
  bool has_color_{false};
  uint8_t color_r_{0};
  uint8_t color_g_{0};
  uint8_t color_b_{0};
  uint8_t color_w_{0};
  bool color_has_white_{false};
  esphome::optional<bool>     mirror_{};
  esphome::optional<uint8_t>  intro_{};
  esphome::optional<uint8_t>  outro_{};
  esphome::optional<float>    inout_duration_{};
  esphome::optional<bool>     force_white_{};
  esphome::optional<bool>     autotune_{};
  bool ha_events_{false};
  uint32_t iterations_{1};
  uint8_t  nesting_depth_{0};

  std::vector<CfxSeqOnReachTrigger *>    on_reach_triggers_;
  std::vector<CfxSeqOnCompleteTrigger *> on_complete_triggers_;
  std::vector<CfxSeqOnStopTrigger *>     on_stop_triggers_;
  std::vector<CfxSeqOnStartTrigger *>    on_start_triggers_;
};

template <typename... Ts>
class CfxRunAction : public CfxRunActionBase, public ::esphome::Action<Ts...> {
public:
  void play(const Ts &...x) override { this->do_play_(); }
};

// Pool slot descriptor — wraps a CFXSequence stored in static storage.
// Allocated once at startup; never heap-fragmented during operation.
struct CFXRunSlot {
  bool         in_use{false};
  uint8_t      depth{0};           // nesting depth of this spawned sequence
  CFXSequence *sequence{nullptr};  // points into the static pool array
};

class CFXRunPool {
public:
  static CFXRunPool &get();

  // Claim a free slot and return the sequence pointer, or nullptr if full.
  // depth: nesting level of the caller (0 = top-level cfx_run).
  CFXSequence *claim(uint8_t depth);

  // Release a slot by sequence pointer. Called from the sequence's
  // complete_and_notify() and stop() when the sequence is pool-owned.
  void release(CFXSequence *seq);

  bool is_pool_owned(CFXSequence *seq) const;

private:
  CFXRunPool() = default;

  // Fixed storage — 8 slots, allocated once, never freed.
  // Each slot holds a CFXSequence constructed in-place via placement new.
  static constexpr uint8_t POOL_SIZE = CFX_RUN_POOL_SIZE;

  // Heap-allocated on first use (avoids static init order issues).
  // Each pointer is valid for the lifetime of the firmware.
  CFXSequence *sequences_[POOL_SIZE]{};
  CFXRunSlot   slots_[POOL_SIZE]{};
  bool         initialized_{false};

  void ensure_initialized_();
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
