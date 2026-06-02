/*
 * ChimeraFX - WLED Effects for ESPHome
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Based on WLED by Aircoookie (https://github.com/wled/WLED)
 *
 * Licensed under the EUPL-1.2
 */

#pragma once

#include "CFXRunner.h"
#include "cfx_triggers.h"
#include "esphome/components/light/addressable_light_effect.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"
#include <algorithm>
#include <optional>
#include <cstdint>
#include <vector>

namespace esphome {
namespace cfx_sequence {
class CFXSequence;
}
namespace cfx_light {
class CFXLightOutput;
class CFXVirtualSegmentLight;
}
namespace chimera_fx {
using cfx_sequence::CFXSequence;

class CFXRunner;
class CFXControl;

class CFXAddressableLightEffect : public light::AddressableLightEffect {
public:
  // ── Forward declarations needed by CFXActivation ─────────────────────────
  enum TransitionState {
    TRANSITION_NONE,
    TRANSITION_ENTRY,
    TRANSITION_EXIT,
    TRANSITION_RUNNING,
    OUTRO_RUNNING
  };

  struct MonochromaticPreset {
    bool is_active;
    uint8_t intro_mode;
    uint8_t outro_mode;
  };

  struct HydraulicsParticle {
    float pos;
    float vel;
    bool active;
  };

  // ── CFXActivation — heap-allocated per active light ───────────────────────
  // All members that are only meaningful while the effect is running live here.
  // Allocated in start(), deleted in stop(). At rest the object is ~100 bytes
  // instead of ~304 bytes, saving ~15 KB across 77 effect objects.
  // Fixed capacity for hydraulics splash/drip particles.
  // Must be declared before CFXActivation so the constant is available as an
  // array bound inside the nested struct.
  static constexpr uint8_t MAX_HYDRAULICS_PARTICLES = 8;

  struct CFXActivation {
    CFXRunner *runner{nullptr};
    std::vector<CFXRunner *> segment_runners{};
    bool segments_initialized{false};
    bool palette_synced{false};

    TransitionState state{TRANSITION_NONE};
    uint64_t transition_start_ms{0};
    uint32_t active_transition_duration_ms{0};
    std::vector<Color> intro_snapshot{};
    std::vector<Color> transition_target_snapshot{};
    bool is_sequence_outro{false};

    bool intro_active{false};
    uint8_t active_intro_mode{0};
    bool active_intro_uses_live_frame_fade{false};
    uint8_t active_intro_speed{128};
    uint8_t active_intro_intensity{128};
    uint64_t intro_start_time{0};
    uint32_t active_intro_duration_ms{2000};

    bool outro_active{false};
    uint8_t active_outro_mode{0};
    uint32_t active_outro_duration_ms{2000};
    uint8_t active_outro_speed{128};
    uint8_t active_outro_intensity{128};
    float active_outro_brightness{1.0f};
    uint64_t outro_start_time{0};
    std::vector<Color> outro_color_cache{};

    float hydraulics_fluid_level{0.0f};
    float hydraulics_fluid_velocity{0.0f};
    // Fixed-size array (audit 3.3): avoids heap allocation during intro/outro.
    // hydraulics_particle_count tracks how many slots [0..count) are active.
    HydraulicsParticle hydraulics_particles[MAX_HYDRAULICS_PARTICLES]{};
    uint8_t hydraulics_particle_count{0};
    uint64_t hydraulics_last_ms{0};

    CFXControl *controller{nullptr};
    bool runners_registered_with_controller{false};
    uint64_t last_controller_lookup_ms{0};
    bool active_outro_mirror{false};
    bool active_force_white{false};
    bool active_outro_force_white{false};
    bool initial_preset_applied{false};

    bool autotune_active{false};
    float autotune_expected_speed{-1.0f};
    float autotune_expected_intensity{-1.0f};
    std::string autotune_expected_palette{};
    std::string last_sent_palette{};
    uint64_t last_metadata_refresh{0};

    uint32_t saved_transition_length{0};
    std::string strip_tag{};
    uint8_t spi_diag_apply_logs{0};
    uint8_t spi_diag_bind_logs{0};
    uint8_t spi_diag_census_logs{0};
    uint8_t spi_diag_heartbeat_logs{0};
    uint32_t spi_diag_last_apply_ms{0};
    // Cached once in start() — avoids a heap-allocated std::string every frame
    // in apply() (audit 1.1).
    std::string cached_runner_name{};
    std::vector<std::string> cached_segment_names{};
    float last_triggered_percentage{-1.0f};
    int32_t last_triggered_pixel{-1};
    bool last_return_phase{false};
    int32_t last_leading_pixel{-1};
    bool lifecycle_start_fired{false};
    uint8_t last_fired_milestone{0};
    bool milestone_fired_this_frame{false};
    bool suppress_reach_event{false};
    bool suppress_positional_events{false};
    bool suppress_stop_event{false};
    bool suppress_complete_event{false};
    bool force_lifecycle_shutdown{false};
    // CFX-035: true only when the MAIN effect is progressive (pixel-marching,
    // e.g. Wipe/Sweep). Intro milestones are suppressed only in that case so
    // that monochromatic and non-progressive intros still fire cfx_reach. (CFX-035b)
    bool intro_suppresses_milestones{false};

#ifdef USE_CFX_SEQUENCE
    CFXSequence *active_sequence{nullptr};
    std::optional<uint8_t> sequence_speed{};
    std::optional<uint8_t> sequence_intensity{};
    std::optional<uint8_t> sequence_palette{};
    std::optional<bool> sequence_mirror{};
    std::optional<bool> sequence_autotune{};
    uint32_t sequence_iterations{0};
#endif
    bool completion_pending{false};

    // ── CFX-045: Monochromatic idle suppression ───────────────────────────────
    // After a monochromatic preset completes its intro and settles into solid
    // color, the runners have nothing left to compute. mono_idle is set true
    // at that point, causing the scheduler to skip service() entirely.
    //
    // mono_dirty is a one-frame wake signal: set to true whenever a parameter
    // that affects the rendered output changes (color, speed, force-white). The
    // scheduler services all runners for that single frame to commit the new
    // state to the DMA buffer, then clears mono_dirty and returns to idle.
    //
    // Invariant: mono_idle is only meaningful while is_mono_preset is true and
    // intro_active is false and state != OUTRO_RUNNING. Outside those conditions
    // the normal skip_service path already governs dispatch.
    bool mono_idle{false};
    bool mono_dirty{false};
    bool mono_output_dirty{false};
    bool mono_output_valid{false};
    bool mono_probe_requested{false};
    uint32_t mono_last_color{0xFFFFFFFF}; // sentinel: differs from any real color on first frame
    uint8_t  mono_last_speed{0xFF};       // sentinel: differs from any real speed on first frame
    uint8_t mono_last_palette{0xFF};      // sentinel: differs from any real palette on first frame
    bool mono_last_force_white{false};
    // CFX-047: apply()-level frame counter for idle FPS reporting.
    // Incremented every frame that passes the rate gate regardless of whether
    // service() runs — gives true DMA throughput even when runners are suppressed.
    uint32_t idle_frame_count{0};
    uint32_t idle_period_start_ms{0};
    // Per-frame interval tracking for Time and Jitter in idle log.
    uint32_t idle_last_frame_us{0};   // timestamp of last apply() frame in µs
    uint32_t idle_min_frame_us{UINT32_MAX};
    uint32_t idle_max_frame_us{0};
    uint64_t idle_total_frame_us{0};
    uint32_t idle_jitter_count{0};
    uint32_t idle_parallel_intervals[16]{0};
    uint8_t idle_parallel_interval_index{0};
    uint8_t idle_parallel_interval_count{0};
    uint32_t idle_target_frame_us{16666}; // updated from update_interval_ on first frame
    uint32_t idle_probe_total_us{0};
    bool idle_probe_valid{false};
    uint32_t perf_log_ms{0};
    uint32_t perf_apply_count{0};
    uint32_t perf_apply_max_total_us{0};
    uint32_t perf_apply_max_prep_us{0};
    uint32_t perf_apply_max_sync_us{0};
    uint32_t perf_apply_max_dispatch_us{0};
    uint32_t perf_apply_max_intro_us{0};
    uint32_t perf_apply_max_state_us{0};
    uint32_t perf_apply_max_post_us{0};
    uint64_t perf_apply_total_us{0};
    uint64_t perf_apply_prep_us{0};
    uint64_t perf_apply_sync_us{0};
    uint64_t perf_apply_dispatch_us{0};
    uint64_t perf_apply_intro_us{0};
    uint64_t perf_apply_state_us{0};
    uint64_t perf_apply_post_us{0};
  };

  // ── CFXEffectConfig — codegen-time config for non-virtual-segment effects ──
  // UI entity pointers, preset optionals, and trigger vectors live here.
  // Virtual segment effects leave cfg_ null, saving ~122 bytes per instance.
  // Allocated lazily on first setter call via ensure_cfg_().
  struct CFXEffectConfig {
    // 11 UI entity pointers (44 bytes) — null for virtual segments
    number::Number *speed{nullptr};
    number::Number *intensity{nullptr};
    select::Select *palette{nullptr};
    switch_::Switch *mirror{nullptr};
    switch_::Switch *autotune{nullptr};
    select::Select *transition_effect{nullptr};
    number::Number *transition_duration{nullptr};
    select::Select *intro_effect{nullptr};
    number::Number *inout_duration{nullptr};
    select::Select *outro_effect{nullptr};
    switch_::Switch *debug_switch{nullptr};

    // Effect preset defaults. Empty/inactive for most virtual segments.
    std::optional<uint8_t> speed_preset{};
    std::optional<uint8_t> intensity_preset{};
    std::optional<uint8_t> palette_preset{};
    std::optional<float> brightness_preset{};
    bool has_color_preset{false};
    bool color_preset_has_white{false};
    uint8_t color_preset_r{0};
    uint8_t color_preset_g{0};
    uint8_t color_preset_b{0};
    uint8_t color_preset_w{0};
    std::optional<bool> mirror_preset{};
    std::optional<bool> force_white_preset{};
    std::optional<uint8_t> intro_preset{};
    std::optional<float> inout_duration_preset{};
    std::optional<uint8_t> outro_preset{};

    // One-shot overrides injected by cfx_set before an effect has allocated
    // act_. start() consumes and clears them so they affect only the next run.
    std::optional<uint8_t> pending_sequence_speed{};
    std::optional<uint8_t> pending_sequence_intensity{};
    std::optional<uint8_t> pending_sequence_palette{};
    std::optional<bool> pending_sequence_mirror{};
    std::optional<bool> pending_sequence_autotune{};

    // 5 trigger vectors (60 bytes) — always empty for virtual segments
    std::vector<CfxOnStartTrigger *> on_start_triggers;
    std::vector<CfxOnBeginTrigger *> on_begin_triggers;
    std::vector<CfxOnStopTrigger *> on_stop_triggers;
    std::vector<CfxOnCompleteTrigger *> on_complete_triggers;
    std::vector<CfxOnReachTrigger *> on_reach_triggers;
  };

  CFXAddressableLightEffect(const char *name);
  virtual ~CFXAddressableLightEffect();

  static std::vector<CFXAddressableLightEffect *> all_effects;
  static std::vector<CFXAddressableLightEffect *> all_segment_effects;


  void start() override;
  void stop() override;
  void apply(light::AddressableLight &it, const Color &current_color) override;

  void set_effect_id(uint8_t effect_id) {
    this->effect_id_ = effect_id;
    this->configured_effect_id_ = effect_id;
  }
  // ── Setters — lazily allocate cfg_ on first call ──────────────────────────
  void set_speed(number::Number *v) { ensure_cfg_(); cfg_->speed = v; }
  void set_intensity(number::Number *v) { ensure_cfg_(); cfg_->intensity = v; }
  void set_palette(select::Select *v) { ensure_cfg_(); cfg_->palette = v; }
  void set_mirror(switch_::Switch *v) { ensure_cfg_(); cfg_->mirror = v; }
  void set_autotune(switch_::Switch *v) { ensure_cfg_(); cfg_->autotune = v; }
  // CFX-044: Stack bypass evaluation
  bool has_pending_completion() const { return this->act_ != nullptr && this->act_->completion_pending; }
  void sync_sequence_control_state();
  void execute_completion();
  bool uses_default_transition() const { return this->allow_default_transition_(); }

  void set_update_interval(uint32_t update_interval) {
    this->update_interval_ = update_interval;
    this->sync_diagnostic_target_interval_();
  }
  uint32_t get_update_interval() const { return this->update_interval_; }
  uint32_t get_effective_update_interval() const;
  void set_transition_effect(select::Select *v) { ensure_cfg_(); cfg_->transition_effect = v; }
  void set_transition_duration(number::Number *v) { ensure_cfg_(); cfg_->transition_duration = v; }
  void set_intro_effect(select::Select *v) { ensure_cfg_(); cfg_->intro_effect = v; }
  void set_inout_duration(number::Number *v) { ensure_cfg_(); cfg_->inout_duration = v; }
  void set_outro_effect(select::Select *v) { ensure_cfg_(); cfg_->outro_effect = v; }
  void set_outro_duration(number::Number *v) { ensure_cfg_(); cfg_->inout_duration = v; }
  void set_debug(switch_::Switch *v) { ensure_cfg_(); cfg_->debug_switch = v; }

  select::Select *get_intro_effect() { return cfg_ ? cfg_->intro_effect : nullptr; }

  enum IntroMode {
    INTRO_MODE_NONE = 0,
    INTRO_MODE_WIPE = 1,
    INTRO_MODE_FADE = 2,
    INTRO_MODE_CENTER = 3,
    INTRO_MODE_GLITTER = 4,
    INTRO_MODE_TWIN_PULSE = 5,
    INTRO_MODE_MORSE = 6,
    INTRO_MODE_QUADRANT = 7,
    INTRO_MODE_HYDRAULICS = 8,
    INTRO_MODE_DROPPING = 9,
    INTRO_MODE_ASSEMBLY = 10,
    INTRO_MODE_INERTIA_SWEEP = 11,
    INTRO_MODE_SONAR_REVEAL = 12,
    INTRO_MODE_VENETIAN = 13,
    INTRO_MODE_CRYSTALLIZE = 14,
    INTRO_MODE_DEEP_BREATHE = 15,
    INTRO_MODE_MOIRE_SHIFT  = 16,
    INTRO_MODE_RESONANCE_FILL = 17,
    INTRO_MODE_TELEMETRY    = 18,
    INTRO_MODE_STELLAR_DUST = 19,
    INTRO_MODE_INTERFERENCE = 20,
    INTRO_MODE_ECLIPSE = 21,
    INTRO_MODE_GAS_DISCHARGE = 22,
    INTRO_MODE_HARMONIC_SETTLE = 23,
    INTRO_MODE_LITHOGRAPH = 24,
    INTRO_MODE_TIDAL_SURGE = 25,
    INTRO_MODE_IMPACT_FLARE = 26,
    OUTRO_MODE_CENTER_SQUEEZE = 27
  };

  void run_intro(light::AddressableLight &it, const Color &target_color);
  bool run_outro_frame(light::AddressableLight &it, CFXRunner *runner);


  // ── Preset setters — lazily allocate cfg_ ─────────────────────────────────
  void set_speed_preset(uint8_t v) { ensure_cfg_(); cfg_->speed_preset = v; }
  void set_intro_preset(uint8_t v) { ensure_cfg_(); cfg_->intro_preset = v; }
  void set_inout_duration_preset(float v) { ensure_cfg_(); cfg_->inout_duration_preset = v; }
  void set_outro_preset(uint8_t v) { ensure_cfg_(); cfg_->outro_preset = v; }
  void set_outro_duration_preset(float v) { ensure_cfg_(); cfg_->inout_duration_preset = v; }
  void set_intensity_preset(uint8_t v) { ensure_cfg_(); cfg_->intensity_preset = v; }
  void set_palette_preset(uint8_t v) { ensure_cfg_(); cfg_->palette_preset = v; }
  void set_brightness_preset(float v) { ensure_cfg_(); cfg_->brightness_preset = v; }
  void set_color_preset_rgb(uint8_t r, uint8_t g, uint8_t b) {
    ensure_cfg_();
    cfg_->has_color_preset = true;
    cfg_->color_preset_has_white = false;
    cfg_->color_preset_r = r;
    cfg_->color_preset_g = g;
    cfg_->color_preset_b = b;
    cfg_->color_preset_w = 0;
  }
  void set_color_preset_rgbw(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    ensure_cfg_();
    cfg_->has_color_preset = true;
    cfg_->color_preset_has_white = true;
    cfg_->color_preset_r = r;
    cfg_->color_preset_g = g;
    cfg_->color_preset_b = b;
    cfg_->color_preset_w = w;
  }
  void set_mirror_preset(bool v) { ensure_cfg_(); cfg_->mirror_preset = v; }
  void set_force_white_preset(bool v) { ensure_cfg_(); cfg_->force_white_preset = v; }

  void set_virtual_segment(bool virtual_segment) {
    this->is_virtual_segment_ = virtual_segment;
    if (virtual_segment) {
      auto it = std::find(all_effects.begin(), all_effects.end(), this);
      if (it != all_effects.end()) {
        all_segment_effects.push_back(*it);
        all_effects.erase(it);
      }
    }
  }

  void set_controller(CFXControl *controller) {
    this->controller_ = controller;  // stored flat; copied into act_ on start()
  }
  bool is_virtual_segment() const { return this->is_virtual_segment_; }
  cfx_light::CFXLightOutput *get_diag_output() const;

  // ── Trigger adders — lazily allocate cfg_ ─────────────────────────────────
  void add_on_start_trigger(CfxOnStartTrigger *t) {
    ensure_cfg_(); cfg_->on_start_triggers.push_back(t);
  }
  void add_on_begin_trigger(CfxOnBeginTrigger *t) {
    ensure_cfg_(); cfg_->on_begin_triggers.push_back(t);
  }
  void add_on_stop_trigger(CfxOnStopTrigger *t) {
    ensure_cfg_(); cfg_->on_stop_triggers.push_back(t);
  }
  void add_on_complete_trigger(CfxOnCompleteTrigger *t) {
    ensure_cfg_(); cfg_->on_complete_triggers.push_back(t);
  }
  void add_on_reach_trigger(CfxOnReachTrigger *t) {
    ensure_cfg_(); cfg_->on_reach_triggers.push_back(t);
  }

  void trigger_on_start();
  void trigger_on_begin();
  void trigger_on_stop();
  void trigger_on_complete();
  void check_positional_triggers(int32_t current_pixel, int32_t total_pixels);


  // Per-instance milestone tracking — replaces CFXEventManager singleton state.
  // Each effect instance tracks its own progress so concurrent strips are
  // fully independent. (multi-strip fix)
  static constexpr uint8_t MILESTONE_STEP = 10;
  static constexpr uint8_t MAX_MILESTONES = 10;  // 10..100 in steps of 10
  // No pre-computed string array — there are 100+ effect instances per light
  // so per-instance arrays would exhaust the heap at setup time.
  // Event strings are built on the stack at fire time (snprintf into 48-byte
  // local) — runs at most 4x per 24ms frame, negligible cost.

  // No-op: strings are built on-demand in check_milestones_(), not pre-computed.
  void rebuild_milestone_strings_() {}

  // Implemented in .cpp where cfx_sequence.h (CFXEventManager) is in scope.
  void check_milestones_(float current_pct);

  void reset_milestones_() {
    if (!act_) return;
    act_->last_fired_milestone = 0;
    act_->milestone_fired_this_frame = false;
    // No suppress flag needed: caller (end of intro) is followed immediately
    // by the main effect starting at pixel 0, which is < MILESTONE_STEP anyway.
  }

protected:
  uint8_t effect_id_{0};
  uint8_t configured_effect_id_{0};
  // ── CFXEffectConfig pointer — null for virtual segments ────────────────────
  // Holds UI entity pointers, preset optionals, and trigger vectors.
  // Allocated on first setter call via ensure_cfg_().
  CFXEffectConfig *cfg_{nullptr};

  void ensure_cfg_() { if (!cfg_) cfg_ = new CFXEffectConfig(); }

  // ── Inline accessors — return null/empty when cfg_ absent ─────────────────
  number::Number *local_speed_() const { return cfg_ ? cfg_->speed : nullptr; }
  number::Number *local_intensity_() const { return cfg_ ? cfg_->intensity : nullptr; }
  select::Select *local_palette_() const { return cfg_ ? cfg_->palette : nullptr; }
  switch_::Switch *local_mirror_() const { return cfg_ ? cfg_->mirror : nullptr; }
  switch_::Switch *local_autotune_() const { return cfg_ ? cfg_->autotune : nullptr; }
  select::Select *local_transition_effect_() const { return cfg_ ? cfg_->transition_effect : nullptr; }
  number::Number *local_transition_duration_() const { return cfg_ ? cfg_->transition_duration : nullptr; }
  select::Select *local_intro_effect_() const { return cfg_ ? cfg_->intro_effect : nullptr; }
  number::Number *local_inout_duration_() const { return cfg_ ? cfg_->inout_duration : nullptr; }
  select::Select *local_outro_effect_() const { return cfg_ ? cfg_->outro_effect : nullptr; }
  switch_::Switch *local_debug_switch_() const { return cfg_ ? cfg_->debug_switch : nullptr; }

  // ── Preset accessors ──────────────────────────────────────────────────────
  bool has_speed_preset_() const { return cfg_ && cfg_->speed_preset.has_value(); }
  bool has_intensity_preset_() const { return cfg_ && cfg_->intensity_preset.has_value(); }
  bool has_palette_preset_() const { return cfg_ && cfg_->palette_preset.has_value(); }
  bool has_brightness_preset_() const { return cfg_ && cfg_->brightness_preset.has_value(); }
  bool has_color_preset_() const { return cfg_ && cfg_->has_color_preset; }
  bool has_mirror_preset_() const { return cfg_ && cfg_->mirror_preset.has_value(); }
  bool has_force_white_preset_() const { return cfg_ && cfg_->force_white_preset.has_value(); }
  bool has_intro_preset_() const { return cfg_ && cfg_->intro_preset.has_value(); }
  bool has_inout_duration_preset_() const { return cfg_ && cfg_->inout_duration_preset.has_value(); }
  bool has_outro_preset_() const { return cfg_ && cfg_->outro_preset.has_value(); }

  uint8_t speed_preset_val_() const { return cfg_->speed_preset.value(); }
  uint8_t intensity_preset_val_() const { return cfg_->intensity_preset.value(); }
  uint8_t palette_preset_val_() const { return cfg_->palette_preset.value(); }
  float brightness_preset_val_() const { return cfg_->brightness_preset.value(); }
  bool color_preset_has_white_() const { return cfg_->color_preset_has_white; }
  uint8_t color_preset_r_() const { return cfg_->color_preset_r; }
  uint8_t color_preset_g_() const { return cfg_->color_preset_g; }
  uint8_t color_preset_b_() const { return cfg_->color_preset_b; }
  uint8_t color_preset_w_() const { return cfg_->color_preset_w; }
  bool mirror_preset_val_() const { return cfg_->mirror_preset.value(); }
  bool force_white_preset_val_() const { return cfg_->force_white_preset.value(); }
  uint8_t intro_preset_val_() const { return cfg_->intro_preset.value(); }
  float inout_duration_preset_val_() const { return cfg_->inout_duration_preset.value(); }
  uint8_t outro_preset_val_() const { return cfg_->outro_preset.value(); }
  std::optional<uint32_t> resolve_inout_duration_override_ms_(
      number::Number *dur_num) const {
    if (dur_num != nullptr && dur_num->has_state()) {
      return static_cast<uint32_t>(dur_num->state * 1000.0f);
    }
    if (this->has_inout_duration_preset_()) {
      return static_cast<uint32_t>(this->inout_duration_preset_val_() * 1000.0f);
    }
    return std::nullopt;
  }


public:
#ifndef USE_CFX_SEQUENCE
  // Diagnostics and light-output helpers need activation visibility in
  // non-sequence builds too.
  CFXActivation *get_act() const { return act_; }
  size_t get_runner_count() const {
    if (!act_) return 0;
    if (!act_->segment_runners.empty()) return act_->segment_runners.size();
    return act_->runner ? 1U : 0U;
  }
  void apply_live_autotune_state(bool enabled) {
    if (!act_) return;
    act_->autotune_active = enabled;
    if (enabled)
      this->apply_autotune_defaults_();
  }
#endif

#ifdef USE_CFX_SEQUENCE
  void set_active_sequence(CFXSequence *seq, std::optional<uint8_t> spd,
                           std::optional<uint8_t> iten,
                           std::optional<uint8_t> pal,
                           std::optional<bool> mir,
                           std::optional<bool> autotune, uint32_t itr);
  CFXSequence *get_active_sequence() const { return act_ ? act_->active_sequence : nullptr; }
  // CFX-030: allows callers to check whether the effect is currently running
  // before calling set_active_sequence() (act_==nullptr means effect is stopped).
  CFXActivation *get_act() const { return act_; }
  size_t get_runner_count() const {
    if (!act_) return 0;
    if (!act_->segment_runners.empty()) return act_->segment_runners.size();
    return act_->runner ? 1U : 0U;
  }

  // cfx_set action setters — override sequence params on the active effect.
  // Persist until the next start() call resets them via set_active_sequence().
  void apply_live_autotune_state(bool enabled) {
    if (!act_) return;
    act_->autotune_active = enabled;
    if (enabled)
      this->apply_autotune_defaults_();
  }

  void set_sequence_speed(uint8_t v) {
    ensure_cfg_();
    cfg_->pending_sequence_speed = v;
    if (act_) {
      act_->sequence_speed = v;
      if (act_->runner)
        act_->runner->setSpeed(v);
      for (auto *r : act_->segment_runners)
        r->setSpeed(v);
    }
  }
  void set_sequence_intensity(uint8_t v) {
    ensure_cfg_();
    cfg_->pending_sequence_intensity = v;
    if (act_) {
      act_->sequence_intensity = v;
      if (act_->runner)
        act_->runner->setIntensity(v);
      for (auto *r : act_->segment_runners)
        r->setIntensity(v);
    }
  }
  void set_sequence_palette(uint8_t v) {
    ensure_cfg_();
    cfg_->pending_sequence_palette = v;
    if (act_) {
      act_->sequence_palette = v;
      if (act_->runner)
        act_->runner->setPalette(v);
      for (auto *r : act_->segment_runners)
        r->setPalette(v);
    }
  }
  void set_sequence_mirror(bool v) {
    ensure_cfg_();
    cfg_->pending_sequence_mirror = v;
    if (act_) {
      act_->sequence_mirror = v;
      if (act_->runner)
        act_->runner->setMirror(v);
      for (auto *r : act_->segment_runners)
        r->setMirror(v);
    }
  }
  void set_sequence_autotune(bool v) {
    ensure_cfg_();
    cfg_->pending_sequence_autotune = v;
    if (act_) act_->sequence_autotune = v;
  }

  // Propagate ownership flags to all runners so CFXControl push callbacks
  // don't overwrite cfx_set values via UI slider on_state_callback.
  void set_runner_owns_speed(bool v) {
    if (!act_) return;
    if (act_->runner) act_->runner->sequence_owns_speed_ = v;
    for (auto *r : act_->segment_runners) r->sequence_owns_speed_ = v;
  }
  void set_runner_owns_intensity(bool v) {
    if (!act_) return;
    if (act_->runner) act_->runner->sequence_owns_intensity_ = v;
    for (auto *r : act_->segment_runners) r->sequence_owns_intensity_ = v;
  }
  void set_runner_owns_palette(bool v) {
    if (!act_) return;
    if (act_->runner) act_->runner->sequence_owns_palette_ = v;
    for (auto *r : act_->segment_runners) r->sequence_owns_palette_ = v;
  }
  void set_runner_owns_mirror(bool v) {
    if (!act_) return;
    if (act_->runner) act_->runner->sequence_owns_mirror_ = v;
    for (auto *r : act_->segment_runners) r->sequence_owns_mirror_ = v;
  }
#endif

  bool can_parent_coordinate_segment() const;
  bool parent_coordinated_segment_due(uint64_t now) const;
  void prepare_parent_coordinated_runner(light::AddressableLight &it);
  void sync_parent_owned_inputs(uint32_t color, float gamma,
                                float global_brightness);
  void mark_parent_coordinated_run(uint64_t now);
  void process_parent_coordinated_runner_events();
  bool runner_mode_can_idle_(uint8_t mode);
  bool evaluate_mono_idle_();

  bool is_clean_mono_idle_output() const {
    return act_ != nullptr && act_->mono_idle && act_->mono_output_valid &&
           !act_->mono_output_dirty && !act_->intro_active &&
           !act_->outro_active && act_->state == TRANSITION_NONE;
  }
  bool has_dirty_mono_idle_output() const {
    return act_ != nullptr && act_->mono_idle && act_->mono_output_dirty;
  }
  bool is_mono_idle() const {
    return act_ != nullptr && act_->mono_idle;
  }
  bool mono_idle_logging_enabled() const;
  bool has_pending_mono_idle_probe() const {
    return act_ != nullptr && act_->mono_probe_requested;
  }
  bool mono_idle_probe_due(uint32_t now_ms) const {
    if (act_ == nullptr || !act_->mono_idle || act_->mono_probe_requested ||
        !this->mono_idle_logging_enabled()) {
      return false;
    }
    static constexpr uint32_t idle_probe_interval_ms = 2000;
    const auto *diag = act_->runner != nullptr ? &act_->runner->diagnostics
                                               : nullptr;
    if (diag == nullptr) {
      for (auto *runner : act_->segment_runners) {
        if (runner != nullptr) {
          diag = &runner->diagnostics;
          break;
        }
      }
    }
    if (diag == nullptr) {
      return false;
    }
    return (now_ms - diag->last_log_time) >= idle_probe_interval_ms;
  }
  void request_mono_idle_probe() {
    if (act_ != nullptr && act_->mono_idle) {
      act_->mono_probe_requested = true;
      act_->mono_dirty = true;
      act_->mono_output_dirty = true;
    }
  }
  void wake_mono_idle_output() {
    if (act_ != nullptr && act_->mono_idle) {
      act_->mono_probe_requested = false;
      act_->mono_dirty = true;
      act_->mono_output_dirty = true;
      act_->idle_probe_total_us = 0;
      act_->idle_probe_valid = false;
      if (act_->runner != nullptr) {
        act_->runner->diagnostics.reset_log_window();
      }
      for (auto *runner : act_->segment_runners) {
        if (runner != nullptr) {
          runner->diagnostics.reset_log_window();
        }
      }
    }
  }
  void log_mono_idle_sleep(bool force = false);
  void mark_mono_output_dirty() {
    if (act_ != nullptr && act_->mono_idle) {
      act_->mono_output_dirty = true;
    }
  }
  void mark_mono_output_committed() {
    if (act_ != nullptr && act_->mono_idle) {
      act_->mono_output_dirty = false;
      act_->mono_output_valid = true;
    }
  }

  // Trigger vectors moved into CFXEffectConfig (accessed via cfg_->on_start_triggers etc.)
  // Empty static vectors returned when cfg_ is null (virtual segments).
  static const std::vector<CfxOnStartTrigger *> empty_start_triggers_;
  static const std::vector<CfxOnBeginTrigger *> empty_begin_triggers_;
  static const std::vector<CfxOnStopTrigger *> empty_stop_triggers_;
  static const std::vector<CfxOnCompleteTrigger *> empty_complete_triggers_;
  static const std::vector<CfxOnReachTrigger *> empty_reach_triggers_;


  void set_strip_tag(const std::string &tag) { if (act_) act_->strip_tag = tag; }

  void set_is_sequence_outro(bool v) { if (act_) act_->is_sequence_outro = v; }
  void set_suppress_positional_events(bool v) {
    if (act_) act_->suppress_positional_events = v;
  }
  void set_suppress_reach_event(bool v) {
    if (act_) act_->suppress_reach_event = v;
  }
  void set_suppress_stop_event(bool v) {
    if (act_) act_->suppress_stop_event = v;
  }
  void set_suppress_complete_event(bool v) {
    if (act_) act_->suppress_complete_event = v;
  }
  void request_lifecycle_shutdown() {
    if (act_) act_->force_lifecycle_shutdown = true;
  }

  // ── Activation pointer — null when effect is not running ─────────────────
  // All per-run state lives in CFXActivation, allocated in start(), freed in
  // stop(). At rest this object carries ~100 bytes instead of ~304 bytes.
  CFXActivation *act_{nullptr};

  // is_virtual_segment_ is set at codegen time, not per-activation.
  bool is_virtual_segment_{false};
  uint32_t update_interval_{16};

  void sync_diagnostic_target_interval_();
  uint64_t next_run_{0};         // Absolute due-time gate; avoids snapping to caller ticks.
  uint64_t last_run_{0};         // Per-instance rate gate — must NOT be in CFXActivation (shared across virtual segments)

  // controller_ is set at codegen time via set_controller(), before start()
  // is ever called. Copied into act_->controller on each start().
  CFXControl *controller_{nullptr};




  MonochromaticPreset get_monochromatic_preset_(uint8_t effect_id);
  bool rate_gate_due_(uint64_t now);
  uint32_t effective_update_interval_ms_() const;
  bool is_monochromatic_(uint8_t effect_id) const;
  bool is_animated_monochromatic_hold_(uint8_t effect_id) const;
  std::vector<uint8_t> get_monochromatic_pool_();
  static bool is_architectural_effect_id_(uint8_t effect_id);
  bool allow_default_transition_() const;

  static uint8_t last_roulette_id_;

  uint8_t get_palette_index_();
  uint8_t get_pal_idx(select::Select *s);
  uint8_t get_default_palette_id_(uint8_t effect_id);
  Color get_intro_palette_color_(uint8_t palette_id, const Color &fallback) const;
  bool resolve_force_white_active_(bool requested, uint8_t palette_id) const;
  std::string get_palette_name_(uint8_t pal_id);
  std::string get_intro_name_(uint8_t intro_id);
  std::string get_outro_name_(uint8_t outro_id);
  uint32_t get_intro_mode_min_duration_ms_(uint8_t intro_mode) const;
  uint32_t get_outro_mode_min_duration_ms_(uint8_t outro_mode) const;
  std::optional<float> get_default_inout_duration_s_(uint8_t effect_id) const;
  uint8_t get_default_speed_(uint8_t effect_id);
  uint8_t get_default_intensity_(uint8_t effect_id);

  // Preset optionals moved into CFXEffectConfig (accessed via has_*_preset_() / *_preset_val_()).


  void run_controls_();
  bool can_batch_steady_virtual_segment_() const;
  bool try_batch_steady_virtual_segments_(uint64_t now);
  void prepare_steady_virtual_segment_runner_(light::AddressableLight &it);
  void fire_start_lifecycle_if_needed_();




  // Applies per-effect defaults to UI sliders/palette and records expected
  // values. Only touches controls that don't have a hard YAML preset.
  void apply_autotune_defaults_();
  void apply_startup_light_presets_();
  void apply_startup_control_presets_();
  void restore_preset_runtime_defaults_(uint32_t delay_ms = 250);

  // Transition length saved/restored around effect runs for virtual segments
  // to prevent the white flash from ESPHome's transition engine.
};


} // namespace chimera_fx
} // namespace esphome

#include "esphome/components/light/light_call.h"

namespace esphome {
namespace chimera_fx {

class LightStateProxy : public light::LightState {
public:
  static light::LightEffect *get_active_effect(light::LightState *state) {
    return static_cast<LightStateProxy *>(state)->get_active_effect_();
  }
  static void stop_state_transformer(light::LightState *state) {
    static_cast<LightStateProxy *>(state)->transformer_.reset();
  }
  // Fix 2: detect whether the Master Light's transition engine is active.
  // When true, ESPHome's transformer is writing a solid colour into the shared
  // DMA buffer every frame, silently overwriting any idling segment data.
  // Checking transformer_ != nullptr is the lightest possible probe — no pixel
  // reads, no heap allocation, no side effects.
  static bool has_active_transformer(light::LightState *state) {
    return static_cast<LightStateProxy *>(state)->transformer_ != nullptr;
  }
  static void clear_pending_write(light::LightState *state) {
    static_cast<LightStateProxy *>(state)->next_write_ = false;
  }
};

template <typename... Ts> class PlayEffectAction : public Action<Ts...> {
public:
  PlayEffectAction(light::LightState *light) : light_(light) {}

  TEMPLATABLE_VALUE(std::string, effect);
  TEMPLATABLE_VALUE(uint8_t, speed);
  TEMPLATABLE_VALUE(uint8_t, intensity);
  TEMPLATABLE_VALUE(uint8_t, palette);
  TEMPLATABLE_VALUE(bool, mirror);

  void play(Ts... x) override {
    auto call = this->light_->turn_on();

    if (this->effect_.has_value()) {
      call.set_effect(this->effect_.value(x...));
    }

    call.perform();
    // After calling perform(), ESPHome activates the target effect object
    // natively. If the active effect is CFXAddressableLightEffect, we can
    // dynamically access it and inject our parameter presets immediately
    // before the engine's first update cycle.

    // 2. Extract the underlying ChimeraFX effect to inject overrides
    light::LightEffect *effect =
        LightStateProxy::get_active_effect(this->light_);
    if (effect != nullptr) {
      CFXAddressableLightEffect *active_fx = nullptr;
      for (auto *inst : CFXAddressableLightEffect::all_effects) {
        if (inst == effect) {
          active_fx = inst;
          break;
        }
      }
      if (active_fx != nullptr) {
        if (this->speed_.has_value())
          active_fx->set_speed_preset(this->speed_.value(x...));
        if (this->intensity_.has_value())
          active_fx->set_intensity_preset(this->intensity_.value(x...));
        if (this->palette_.has_value())
          active_fx->set_palette_preset(this->palette_.value(x...));
        if (this->mirror_.has_value())
          active_fx->set_mirror_preset(this->mirror_.value(x...));
      }
    }
  }

protected:
  light::LightState *light_;
};

} // namespace chimera_fx
} // namespace esphome
