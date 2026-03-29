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
#include <optional>
#include <cstdint>
#include <vector>

namespace esphome {
namespace cfx_sequence {
class CFXSequence;
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
  struct CFXActivation {
    CFXRunner *runner{nullptr};
    std::vector<CFXRunner *> segment_runners{};
    bool segments_initialized{false};
    bool palette_synced{false};
    uint64_t last_run{0};

    TransitionState state{TRANSITION_NONE};
    uint64_t transition_start_ms{0};
    std::vector<Color> intro_snapshot{};
    bool is_sequence_outro{false};

    bool intro_active{false};
    uint8_t active_intro_mode{0};
    uint8_t active_intro_speed{128};
    uint64_t intro_start_time{0};
    uint32_t active_intro_duration_ms{1000};

    bool outro_active{false};
    uint8_t active_outro_mode{0};
    uint32_t active_outro_duration_ms{1000};
    uint8_t active_outro_speed{128};
    uint8_t active_outro_intensity{128};
    float active_outro_brightness{1.0f};
    uint64_t outro_start_time{0};
    std::vector<Color> outro_color_cache{};

    float hydraulics_fluid_level{0.0f};
    float hydraulics_fluid_velocity{0.0f};
    std::vector<HydraulicsParticle> hydraulics_particles{};
    uint64_t hydraulics_last_ms{0};

    CFXControl *controller{nullptr};
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
    float last_triggered_percentage{-1.0f};
    int32_t last_triggered_pixel{-1};
    bool last_return_phase{false};
    int32_t last_leading_pixel{-1};
    uint8_t last_fired_milestone{0};
    bool milestone_fired_this_frame{false};

#ifdef USE_CFX_SEQUENCE
    CFXSequence *active_sequence{nullptr};
    std::optional<uint8_t> sequence_speed{};
    std::optional<uint8_t> sequence_intensity{};
    std::optional<uint8_t> sequence_palette{};
    uint32_t sequence_iterations{0};
#endif
  };

  CFXAddressableLightEffect(const char *name);
  virtual ~CFXAddressableLightEffect();

  static std::vector<CFXAddressableLightEffect *> all_effects;

  void start() override;
  void stop() override;
  void apply(light::AddressableLight &it, const Color &current_color) override;

  void set_effect_id(uint8_t effect_id) {
    this->effect_id_ = effect_id;
    this->configured_effect_id_ = effect_id;
  }
  void set_speed(number::Number *speed) { this->speed_ = speed; }
  void set_intensity(number::Number *intensity) {
    this->intensity_ = intensity;
  }
  void set_palette(select::Select *palette) { this->palette_ = palette; }
  void set_mirror(switch_::Switch *mirror) { this->mirror_ = mirror; }
  void set_autotune(switch_::Switch *autotune) { this->autotune_ = autotune; }
  void set_update_interval(uint32_t update_interval) {
    this->update_interval_ = update_interval;
  }
  void set_transition_effect(select::Select *s) {
    this->transition_effect_ = s;
  }
  void set_transition_duration(number::Number *n) {
    this->transition_duration_ = n;
  }
  void set_intro_effect(select::Select *s) { this->intro_effect_ = s; }
  void set_inout_duration(number::Number *n) { this->inout_duration_ = n; }
  void set_outro_effect(select::Select *s) { this->outro_effect_ = s; }
  void set_outro_duration(number::Number *n) { this->inout_duration_ = n; }
  void set_debug(switch_::Switch *s) { this->debug_switch_ = s; }

  select::Select *get_intro_effect() { return this->intro_effect_; }

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
    INTRO_MODE_LITHOGRAPH = 24
  };

  void run_intro(light::AddressableLight &it, const Color &target_color);
  bool run_outro_frame(light::AddressableLight &it, CFXRunner *runner);


  void set_speed_preset(uint8_t v) { this->speed_preset_ = v; }
  void set_intro_preset(uint8_t v) { this->intro_preset_ = v; }
  void set_inout_duration_preset(float v) { this->inout_duration_preset_ = v; }
  void set_outro_preset(uint8_t v) { this->outro_preset_ = v; }
  void set_outro_duration_preset(float v) { this->inout_duration_preset_ = v; }
  void set_intensity_preset(uint8_t v) { this->intensity_preset_ = v; }
  void set_palette_preset(uint8_t v) { this->palette_preset_ = v; }
  void set_mirror_preset(bool v) { this->mirror_preset_ = v; }
  void set_autotune_preset(bool v) { this->autotune_preset_ = v; }
  void set_force_white_preset(bool v) { this->force_white_preset_ = v; }

  void set_virtual_segment(bool virtual_segment) {
    this->is_virtual_segment_ = virtual_segment;
  }

  void set_controller(CFXControl *controller) {
    this->controller_ = controller;  // stored flat; copied into act_ on start()
  }

  void add_on_start_trigger(CfxOnStartTrigger *t) {
    this->on_start_triggers_.push_back(t);
  }
  void add_on_begin_trigger(CfxOnBeginTrigger *t) {
    this->on_begin_triggers_.push_back(t);
  }
  void add_on_stop_trigger(CfxOnStopTrigger *t) {
    this->on_stop_triggers_.push_back(t);
  }
  void add_on_complete_trigger(CfxOnCompleteTrigger *t) {
    this->on_complete_triggers_.push_back(t);
  }
  void add_on_reach_trigger(CfxOnReachTrigger *t) {
    this->on_reach_triggers_.push_back(t);
  }

  void trigger_on_start();
  void trigger_on_begin();
  void trigger_on_stop();
  void trigger_on_complete();
  void check_positional_triggers(int32_t current_pixel, int32_t total_pixels);


  // Per-instance milestone tracking — replaces CFXEventManager singleton state.
  // Each effect instance tracks its own progress so concurrent strips are
  // fully independent. (multi-strip fix)
  static constexpr uint8_t MILESTONE_STEP = 5;
  static constexpr uint8_t MAX_MILESTONES = 20;  // 5..100 in steps of 5
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
  }

protected:
  uint8_t effect_id_{0};
  uint8_t configured_effect_id_{0};
  number::Number *speed_{nullptr};
  number::Number *intensity_{nullptr};
  select::Select *palette_{nullptr};
  switch_::Switch *mirror_{nullptr};
  switch_::Switch *autotune_{nullptr};
  select::Select *transition_effect_{nullptr};
  number::Number *transition_duration_{nullptr};
  select::Select *intro_effect_{nullptr};
  number::Number *inout_duration_{nullptr};
  select::Select *outro_effect_{nullptr};
  switch_::Switch *debug_switch_{nullptr};


public:
#ifdef USE_CFX_SEQUENCE
  void set_active_sequence(CFXSequence *seq, std::optional<uint8_t> spd,
                           std::optional<uint8_t> iten, std::optional<uint8_t> pal,
                           uint32_t itr);
  CFXSequence *get_active_sequence() const { return act_ ? act_->active_sequence : nullptr; }

  // cfx_set action setters — override sequence params on the active effect.
  // Persist until the next start() call resets them via set_active_sequence().
  void set_sequence_speed(uint8_t v)     { if (act_) act_->sequence_speed     = v; }
  void set_sequence_intensity(uint8_t v) { if (act_) act_->sequence_intensity = v; }
  void set_sequence_palette(uint8_t v)   { if (act_) act_->sequence_palette   = v; }

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
#endif

  std::vector<CfxOnStartTrigger *> on_start_triggers_;
  std::vector<CfxOnBeginTrigger *>   on_begin_triggers_;
  std::vector<CfxOnStopTrigger *>    on_stop_triggers_;
  std::vector<CfxOnCompleteTrigger *> on_complete_triggers_;
  std::vector<CfxOnReachTrigger *> on_reach_triggers_;


  void set_strip_tag(const std::string &tag) { if (act_) act_->strip_tag = tag; }

  void set_is_sequence_outro(bool v) { if (act_) act_->is_sequence_outro = v; }

  // ── Activation pointer — null when effect is not running ─────────────────
  // All per-run state lives in CFXActivation, allocated in start(), freed in
  // stop(). At rest this object carries ~100 bytes instead of ~304 bytes.
  CFXActivation *act_{nullptr};

  // is_virtual_segment_ is set at codegen time, not per-activation.
  bool is_virtual_segment_{false};
  uint32_t update_interval_{16};

  // controller_ is set at codegen time via set_controller(), before start()
  // is ever called. Copied into act_->controller on each start().
  CFXControl *controller_{nullptr};

  // MAX_HYDRAULICS_PARTICLES stays as a class constant (used in run_intro/outro).
  static const uint8_t MAX_HYDRAULICS_PARTICLES = 8;




  MonochromaticPreset get_monochromatic_preset_(uint8_t effect_id);
  bool is_monochromatic_(uint8_t effect_id);
  std::vector<uint8_t> get_monochromatic_pool_();

  static uint8_t last_roulette_id_;

  uint8_t get_palette_index_();
  uint8_t get_pal_idx(select::Select *s);
  uint8_t get_default_palette_id_(uint8_t effect_id);
  std::string get_palette_name_(uint8_t pal_id);
  uint8_t get_default_speed_(uint8_t effect_id);
  uint8_t get_default_intensity_(uint8_t effect_id);

  std::optional<uint8_t> speed_preset_{};
  std::optional<uint8_t> intensity_preset_{};
  std::optional<uint8_t> palette_preset_{};
  std::optional<bool> mirror_preset_{};
  std::optional<bool> autotune_preset_{};
  std::optional<bool> force_white_preset_{};
  std::optional<uint8_t> intro_preset_{};
  std::optional<float> inout_duration_preset_{};
  std::optional<uint8_t> outro_preset_{};


  void run_controls_();




  // Applies per-effect defaults to UI sliders/palette and records expected
  // values. Only touches controls that don't have a hard YAML preset.
  void apply_autotune_defaults_();

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
