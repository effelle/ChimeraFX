/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * This file is part of the ChimeraFX for ESPHome.
 */

#include "cfx_addressable_light_effect.h"
#include "../cfx_light/cfx_light.h"
#include "../cfx_light/cfx_virtual_segment_light.h"
#include "cfx_compat.h"
#include "cfx_control.h"
#include "cfx_utils.h"
#include "esphome/core/application.h"
#include "esphome/core/color.h"
#include "esphome/core/hal.h" // For millis_64()
#include "esphome/core/log.h"
#include <algorithm>
#include <span>

#include "cfx_event_manager.h"
#include "cfx_scheduler.h"
#ifdef USE_CFX_SEQUENCE
#include "../cfx_sequence/cfx_sequence.h"
#endif

// File-local macro: maps bare `instance` and `chimera_fx::instance` →
// per-core slot. Fixes all intro/outro engine references without requiring
// individual edits. Same rule as CFXRunner.cpp: no outer parens.
#define instance instance_per_core[xPortGetCoreID()]

namespace esphome {
namespace chimera_fx {

bool CFXControl::global_debug_enabled_ = false;

std::vector<CFXAddressableLightEffect *> CFXAddressableLightEffect::all_effects;
std::vector<CFXAddressableLightEffect *> CFXAddressableLightEffect::all_segment_effects;
uint8_t CFXAddressableLightEffect::last_roulette_id_ = 0;

// Static empty vectors for trigger accessors when cfg_ is null (virtual
// segments).
const std::vector<CfxOnStartTrigger *>
    CFXAddressableLightEffect::empty_start_triggers_;
const std::vector<CfxOnBeginTrigger *>
    CFXAddressableLightEffect::empty_begin_triggers_;
const std::vector<CfxOnStopTrigger *>
    CFXAddressableLightEffect::empty_stop_triggers_;
const std::vector<CfxOnCompleteTrigger *>
    CFXAddressableLightEffect::empty_complete_triggers_;
const std::vector<CfxOnReachTrigger *>
    CFXAddressableLightEffect::empty_reach_triggers_;

static const char *const TAG = "chimera_fx";

namespace {
struct SPIDiagCensus {
  size_t total_effects{0};
  size_t total_segment_effects{0};
  size_t active_effects{0};
  size_t active_segment_effects{0};
  size_t active_spi_effects{0};
  size_t bound_sequences{0};
  size_t runner_count{0};
};

static cfx_light::CFXLightOutput *resolve_diag_output(
    CFXAddressableLightEffect *effect) {
  if (effect == nullptr)
    return nullptr;
  return effect->get_diag_output();
}

// CFX-057: Cached census — avoid scanning 656+ effect instances every frame.
// The census is recomputed at most once every 250ms. Callers that run on the
// hot render path (apply(), check_milestones_) now get O(1) reads instead of
// O(N) scans where N can reach 656 in a fully-loaded system.
static SPIDiagCensus cached_census_;
static uint32_t cached_census_ms_{0};
static constexpr uint32_t CENSUS_TTL_MS = 250;

static SPIDiagCensus collect_spi_diag_census() {
  const uint32_t now = esphome::millis();
  if (cached_census_ms_ != 0 && (now - cached_census_ms_) < CENSUS_TTL_MS) {
    return cached_census_;
  }

  SPIDiagCensus census;
  census.total_effects = CFXAddressableLightEffect::all_effects.size();
  census.total_segment_effects =
      CFXAddressableLightEffect::all_segment_effects.size();

  uint16_t wdt_counter = 0;
  auto collect_group = [&census, &wdt_counter](const std::vector<CFXAddressableLightEffect *> &group,
                                 bool is_segment_group) {
    for (auto *inst : group) {
      if (inst == nullptr)
        continue;
      auto *act = inst->get_act();
      if (act == nullptr)
        continue;

      if (is_segment_group)
        census.active_segment_effects++;
      else
        census.active_effects++;

      auto *out = resolve_diag_output(inst);
      if (out != nullptr && out->is_spi_transport())
        census.active_spi_effects++;
      if (inst->get_active_sequence() != nullptr)
        census.bound_sequences++;
      census.runner_count += inst->get_runner_count();

      // CFX-057: Feed WDT every 64 iterations to prevent stall
      if (++wdt_counter % 64 == 0)
        esphome::App.feed_wdt();
    }
  };

  collect_group(CFXAddressableLightEffect::all_effects, false);
  collect_group(CFXAddressableLightEffect::all_segment_effects, true);

  cached_census_ = census;
  cached_census_ms_ = now;
  return census;
}

static bool should_force_spi_sequential_dispatch() {
  return collect_spi_diag_census().active_spi_effects > 0;
}
}  // namespace

CFXAddressableLightEffect::CFXAddressableLightEffect(const char *name)
    : light::AddressableLightEffect(name) {
  CFXAddressableLightEffect::all_effects.push_back(this);
}

CFXAddressableLightEffect::~CFXAddressableLightEffect() {
  auto it = std::find(CFXAddressableLightEffect::all_effects.begin(),
                      CFXAddressableLightEffect::all_effects.end(), this);
  if (it != CFXAddressableLightEffect::all_effects.end()) {
    CFXAddressableLightEffect::all_effects.erase(it);
  }
  delete this->act_;
  this->act_ = nullptr;
  delete this->cfg_;
  this->cfg_ = nullptr;
}

cfx_light::CFXLightOutput *CFXAddressableLightEffect::get_diag_output() const {
  if (this->is_virtual_segment_) {
#ifdef USE_ESP32
    auto *vseg = static_cast<cfx_light::CFXVirtualSegmentLight *>(
        this->get_addressable_());
    if (vseg != nullptr)
      return vseg->get_parent();
#endif
    return nullptr;
  }

  return static_cast<cfx_light::CFXLightOutput *>(this->get_addressable_());
}

CFXAddressableLightEffect::MonochromaticPreset
CFXAddressableLightEffect::get_monochromatic_preset_(uint8_t effect_id) {
  // Master dictionary for static/monochromatic effects requiring forced
  // intros/outros
  switch (effect_id) {
  case 161: // Horizon Sweep
    return {true, INTRO_WIPE, INTRO_WIPE};
  case 162: // Curtain Sweep
    return {true, INTRO_CENTER, INTRO_CENTER};
  case 163: // Stardust Sweep
    return {true, INTRO_GLITTER, INTRO_GLITTER};
  case 165: // Twin Pulse Sweep
    return {true, INTRO_TWIN_PULSE, INTRO_TWIN_PULSE};
  case 166: // Transmission
    return {true, INTRO_MORSE, INTRO_MORSE};
  case 167: // Four Times the Charm
    return {true, INTRO_MODE_QUADRANT, INTRO_MODE_QUADRANT};
  case 168: // Hydro-Pulse
    return {true, INTRO_MODE_HYDRAULICS, INTRO_MODE_HYDRAULICS};
  case 169: // Dropping Fill
    return {true, INTRO_MODE_DROPPING, INTRO_MODE_DROPPING};
  case 170: // Assembly
    return {true, INTRO_MODE_ASSEMBLY, INTRO_MODE_ASSEMBLY};
  case 171: // Inertia Sweep
    return {true, INTRO_MODE_INERTIA_SWEEP, INTRO_MODE_INERTIA_SWEEP};
  case 172: // Sonar Reveal
    return {true, INTRO_MODE_SONAR_REVEAL, INTRO_MODE_SONAR_REVEAL};
  case 173: // Venetian
    return {true, INTRO_MODE_VENETIAN, INTRO_MODE_VENETIAN};
  case 174: // Crystallize
    return {true, INTRO_MODE_CRYSTALLIZE, INTRO_MODE_CRYSTALLIZE};
  case 175: // Deep Breathe
    return {true, INTRO_MODE_DEEP_BREATHE, INTRO_MODE_DEEP_BREATHE};
  case 176: // Moiré Shift
    return {true, INTRO_MODE_MOIRE_SHIFT, INTRO_MODE_MOIRE_SHIFT};
  case 177: // Resonance Fill
    return {true, INTRO_MODE_RESONANCE_FILL, INTRO_MODE_RESONANCE_FILL};
  case 178: // Telemetry
    return {true, INTRO_MODE_TELEMETRY, INTRO_MODE_TELEMETRY};
  case 179: // Stellar Dust
    return {true, INTRO_MODE_STELLAR_DUST, INTRO_MODE_STELLAR_DUST};
  case 181: // Eclipse
    return {true, INTRO_MODE_ECLIPSE, INTRO_MODE_ECLIPSE};
  case 182: // Gas Discharge
    return {true, INTRO_MODE_GAS_DISCHARGE, INTRO_MODE_GAS_DISCHARGE};
  case 183: // Harmonic Settle
    return {true, INTRO_MODE_HARMONIC_SETTLE, INTRO_MODE_HARMONIC_SETTLE};
  case 184: // Lithograph
    return {true, INTRO_MODE_LITHOGRAPH, INTRO_MODE_LITHOGRAPH};
  case 186: // Tidal Surge
    return {true, INTRO_MODE_TIDAL_SURGE, INTRO_MODE_TIDAL_SURGE};
  case 187: // Impact Flare
    return {true, INTRO_MODE_IMPACT_FLARE, OUTRO_MODE_CENTER_SQUEEZE};
  case 188: // Monolith
    return {true, INTRO_MODE_FADE, INTRO_MODE_FADE};
  case 189: // Standing Wave
    return {true, INTRO_MODE_INTERFERENCE, INTRO_MODE_INTERFERENCE};
  default:
    return {false, INTRO_NONE, INTRO_NONE};
  }
}

bool CFXAddressableLightEffect::is_monochromatic_(uint8_t effect_id) const {
  switch (effect_id) {
  case 161: // Horizon Sweep
  case 162: // Curtain Sweep
  case 163: // Stardust Sweep
  case 165: // Twin Pulse Sweep
  case 166: // Transmission
  case 167: // Four Times the Charm
  case 168: // Aqueous Flow
  case 169: // Dropping Fill
  case 170: // Assembly
  case 171: // Inertia Sweep
  case 172: // Sonar Reveal
  case 173: // Venetian
  case 174: // Crystallize
  case 175: // Deep Breathe
  case 176: // Moiré Shift
  case 177: // Resonance Fill
  case 178: // Telemetry
  case 179: // Stellar Dust
  case 181: // Eclipse
  case 182: // Gas Discharge
  case 183: // Harmonic Settle
  case 184: // Lithograph
  case 186: // Tidal Surge
  case 187: // Impact Flare
  case 188: // Monolith
  case 189: // Standing Wave
    return true;
  default:
    return false;
  }
}

std::vector<uint8_t> CFXAddressableLightEffect::get_monochromatic_pool_() {
  // Automatically builds the pool from all effects registered as monochromatic
  std::vector<uint8_t> pool;
  // Common premium monochromatic IDs
  for (uint8_t id = 100; id < 200; id++) {
    if (this->is_monochromatic_(id)) {
      pool.push_back(id);
    }
  }
  return pool;
}

void CFXAddressableLightEffect::start() {
  light::AddressableLightEffect::start();

  // Initialise Core 0 dispatch task on first effect start (idempotent).
  CFXScheduler::get().setup();

  // ── CFX-044: Heap floor guard ─────────────────────────────────────────────
  // Refuse to start a new effect if free heap is below the safety floor.
  // The ESP32 radio stack (WiFi / BT / LwIP) needs ~60 kB of contiguous heap
  // to operate. We keep a 15 kB margin above that floor (75 kB total) to
  // absorb small runtime allocations and segment data without risking a crash.
  //
  // When the guard fires the strip holds its last valid pixel state — the
  // effect simply does not start. A LOGW is emitted so the condition is
  // visible in the ESPHome log without stopping the device.
  //
  // The guard is skipped when act_ is already allocated (rapid start/stop
  // cycle reusing the existing object) because no new heap is consumed.
  constexpr uint32_t CFX_HEAP_FLOOR = 75000U; // 75 kB — 15 kB above radio stack minimum
  if (this->act_ == nullptr) {
    uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (free_heap < CFX_HEAP_FLOOR) {
      ESP_LOGW("cfx_heap",
               "[%s] Heap too low to start effect (%u B free, %u B floor) — "
               "strip holds last state. Free RAM before adding more lights/segments.",
               this->get_name().c_str(), free_heap, CFX_HEAP_FLOOR);
      return;
    }
  }

  // Allocate per-activation state. If already allocated (rapid start/stop)
  // reuse the existing object after resetting it cleanly.
  if (this->act_ == nullptr) {
    this->act_ = new CFXActivation();
    if (this->act_ == nullptr) {
      ESP_LOGE("chimera_fx", "FATAL: Failed to allocate CFXActivation! System near OOM.");
      return;
    }
  }

  // CFX-043: Monitor heap health during start()
  ESP_LOGV("cfx_heap", "CFX Start [%s]: Free Heap: %u B, Minimum Ever: %u B",
           this->get_name().c_str(),
           (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  // Initialize/Reset tracking flags cleanly on every start
  this->reset_milestones_();
  this->act_->intro_suppresses_milestones = false;

  // Copy codegen-time fields into the activation context.
  this->act_->controller = this->controller_;

#ifdef USE_CFX_SEQUENCE
  // start() may reuse an existing activation during rapid restart/no-op paths.
  // Clear last-run transient sequence values first; cfx_set cold-start
  // overrides are re-applied below after runner setup.
  act_->sequence_speed.reset();
  act_->sequence_intensity.reset();
  act_->sequence_palette.reset();
  act_->sequence_mirror.reset();
  act_->sequence_autotune.reset();
  act_->sequence_iterations = 0;
#endif

  // --- Ambient Roulette (Randomizer) ---
  if (this->configured_effect_id_ == 255) {
    this->effect_id_ = 255; // Reset to roulette base
    std::vector<uint8_t> pool = this->get_monochromatic_pool_();
    if (!pool.empty()) {
      uint8_t chosen_id = 0;
      // Filter out only the last one to avoid back-to-back repetitions
      std::vector<uint8_t> filtered_pool;
      for (uint8_t id : pool) {
        if (id != CFXAddressableLightEffect::last_roulette_id_) {
          filtered_pool.push_back(id);
        }
      }

      if (filtered_pool.empty()) {
        // Fallback if pool only has 1 element
        chosen_id = pool[0];
      } else {
        uint32_t r_idx =
            (uint32_t)cfx::hw_random16(0, (uint16_t)filtered_pool.size());
        chosen_id = filtered_pool[r_idx];
      }

      ESP_LOGI("chimera_fx", "Ambient Roulette (255) selected effect %u",
               chosen_id);
      this->effect_id_ = chosen_id;
      CFXAddressableLightEffect::last_roulette_id_ = chosen_id;
    }
  }

#ifdef USE_CFX_EVENTS
  // Derive act_->strip_tag early — needed by both cfx_begin and cfx_start
  // events.
  {
    auto *ls = this->get_light_state();
    if (ls != nullptr) {
      char id_buf[128] = {};
      ls->get_object_id_to(std::span(id_buf));
      act_->strip_tag = std::string(id_buf, strnlen(id_buf, sizeof(id_buf)));
    }
    chimera_fx::CFXEventManager::get().add_known_tag(act_->strip_tag);
    this->rebuild_milestone_strings_();
    this->reset_milestones_();
  }
#endif

  // on_cfx_begin trigger (on-device YAML) fires unconditionally.
  // The HA cfx_begin *event* fires only when an actual intro is configured;
  // see end of start() below. (CFX-029)
  // Separator (ID 185) is a UI-only divider — suppress all lifecycle events.
  if (this->effect_id_ != 185) {
    this->trigger_on_begin();

    this->trigger_on_start();

#ifdef USE_CFX_EVENTS
    // Suppress cfx_start in cascade - sequence fires it centrally.
    // This prevents event burst when multiple lights start in sequence.
#endif
  }

  // Zero the light's default transition length while a CFX effect is running.
  // ESPHome's transition engine can delay effect-visible turn-on or paint the
  // ON-state color into the shared buffer before the effect owns rendering.
  // Keeping the configured value only for solid-color mode preserves normal
  // fades there while making effect startup immediate. Restored in stop().

  // State Machine Init: Check if we are turning ON from OFF BEFORE modifying
  // state
  bool is_fresh_turn_on = false;
  if (auto *state = this->get_light_state()) {
    is_fresh_turn_on = !state->current_values.is_on();
    uint32_t default_transition_ms = state->get_default_transition_length();
    if (default_transition_ms > 0) {
      if (act_->saved_transition_length == 0) {
        act_->saved_transition_length = default_transition_ms;
      }
      state->set_default_transition_length(0);
    }
  }

  // Monochromatic presets functionally ARE intros. They must unconditionally
  // execute their start logic regardless of transition snaps bypassing the
  // state check.
  if (this->get_monochromatic_preset_(this->effect_id_).is_active) {
    is_fresh_turn_on = true;
  }

  // Force bypass transition to avoid the 1s darkness bug on initial render
  if (auto *ls = this->get_light_state()) {
    ls->current_values = ls->remote_values;
    if (chimera_fx::LightStateProxy::has_active_transformer(ls)) {
      chimera_fx::LightStateProxy::stop_state_transformer(ls);
    }
  }

  // Defensive reset: ensure outro_start_time_ is clean for the next outro.
  act_->outro_start_time = 0;
  act_->active_transition_duration_ms = 0;
  act_->is_sequence_outro = false;
  act_->suppress_reach_event = false;
  act_->suppress_positional_events = false;
  act_->suppress_stop_event = false;
  act_->suppress_complete_event = false;
  act_->outro_color_cache.clear();
  act_->hydraulics_fluid_level = 0.0f;
  act_->hydraulics_fluid_velocity = 0.0f;
  act_->hydraulics_particle_count = 0; // audit 3.3: fixed array, reset count
  act_->hydraulics_last_ms = 0;

  act_->last_triggered_pixel = -1;
  act_->last_triggered_percentage = -1.0f;

  // CFX-045: Reset mono idle state on every start() so a restarted effect
  // always runs its intro and commits its first solid frame before going idle.
  act_->mono_idle       = false;
  act_->mono_dirty      = false;
  act_->mono_last_color = 0xFFFFFFFF;
  act_->mono_last_speed = 0xFF;
  act_->mono_last_force_white = false;
  act_->idle_frame_count     = 0;
  act_->idle_period_start_ms = 0;
  act_->idle_last_frame_us   = 0;
  act_->idle_min_frame_us    = UINT32_MAX;
  act_->idle_max_frame_us    = 0;
  act_->idle_total_frame_us  = 0;
  act_->idle_jitter_count    = 0;

  // Reset palette sync flag so we enforce the effect's default on the first
  // frame
  act_->palette_synced = false;

  // Find controller early
  if (act_->controller == nullptr) {
    act_->controller = CFXControl::find(this->get_light_state());
  }

  // Prevent leaking if start() is called while runner_ is already initialized
  // (e.g. rapid toggles)
  if (act_->runner != nullptr) {
    if (act_->controller) {
      act_->controller->unregister_runner(act_->runner);
    }
    if (!act_->segment_runners.empty()) {
      for (auto *r : act_->segment_runners) {
        if (r != act_->runner && act_->controller) {
          act_->controller->unregister_runner(r);
        }
        if (r != act_->runner)
          delete r;
      }
      act_->segment_runners.clear();
      act_->segments_initialized = false;
    }
    delete act_->runner;
    act_->runner = nullptr;
  }

  // Allocate Runner(s) early so we can use them for metadata fallback
  if (act_->runner == nullptr) {
    auto *it = (light::AddressableLight *)this->get_light_state()->get_output();
    if (it != nullptr) {
#ifdef USE_ESP32
      // Virtual segment lights are single-runner by design.
      // We check the flag injected by Python codegen to avoid illegal
      // dynamic_cast (-fno-rtti)
      std::vector<cfx_light::CFXSegmentDef> seg_defs;
      if (!this->is_virtual_segment_) {
        auto *cfx_out = static_cast<cfx_light::CFXLightOutput *>(it);
        seg_defs = cfx_out->get_segment_defs();
      }

        if (!seg_defs.empty() && !act_->segments_initialized) {
          for (const auto &def : seg_defs) {
            auto *r = new CFXRunner(it);
            if (r == nullptr) {
               ESP_LOGE("chimera_fx", "CFX-043 FATAL: Segment runner allocation failed!");
               break; 
            }
            r->setBakeBrightness(true); // Multi-segment mode: bake in engine
            r->_segment.start = def.start;
            r->_segment.stop = def.stop;
            r->_segment.mirror = def.mirror;
            r->set_segment_id(def.id);
            r->setMode(this->effect_id_);
            r->diagnostics.set_target_interval_ms(this->update_interval_);
            act_->segment_runners.push_back(r);

            // Feed WDT during potentially heavy allocation loop
            esphome::App.feed_wdt();
          }
          if (!act_->segment_runners.empty()) {
            act_->runner = act_->segment_runners[0];
            act_->segments_initialized = true;
          }
          ESP_LOGV("chimera_fx", "Multi-segment mode: %u runners created for %s",
                   act_->segment_runners.size(), this->get_name());
        } else {
#endif
          act_->runner = new CFXRunner(it);
          if (act_->runner == nullptr) {
             ESP_LOGE("chimera_fx", "CFX-043 FATAL: Single runner allocation failed!");
             return; 
          }
        // If this is a virtual segment entity, it must bake brightness.
        // If it's a standard non-segmented master strip, let the hardware gate
        // handle it.
        act_->runner->setBakeBrightness(this->is_virtual_segment_);
        act_->runner->setMode(this->effect_id_);
        act_->runner->diagnostics.set_target_interval_ms(
            this->update_interval_);
        ESP_LOGV("chimera_fx",
                 "Single-segment mode runner created for %s (Bake: %d)",
                 this->get_name(), this->is_virtual_segment_);

#ifdef USE_ESP32
      }
#endif
    }
  }

  // Cache the light name once per start() so apply() never allocates a
  // std::string every frame (audit 1.1).
  if (auto *ls = this->get_light_state())
    act_->cached_runner_name = ls->get_name().str();
  else
    act_->cached_runner_name.clear();

  auto *diag_out = resolve_diag_output(this);
  SPIDiagCensus diag_census = collect_spi_diag_census();
  if (diag_out != nullptr && diag_out->is_spi_transport()) {
    ESP_LOGD("cfx_seq",
             "SPI diag census[start]: effect=%s tag=%s act=%p totals(e=%u,se=%u) "
             "active(e=%u,se=%u,spi=%u) bound=%u runners=%u",
             act_->cached_runner_name.c_str(), act_->strip_tag.c_str(), act_,
             static_cast<unsigned>(diag_census.total_effects),
             static_cast<unsigned>(diag_census.total_segment_effects),
             static_cast<unsigned>(diag_census.active_effects),
             static_cast<unsigned>(diag_census.active_segment_effects),
             static_cast<unsigned>(diag_census.active_spi_effects),
             static_cast<unsigned>(diag_census.bound_sequences),
             static_cast<unsigned>(diag_census.runner_count));
  } else if (diag_census.active_spi_effects > 0 && act_->spi_diag_census_logs < 2) {
    ESP_LOGD("cfx_seq",
             "SPI diag census[start-neighbor][%u]: effect=%s act=%p totals(e=%u,se=%u) "
             "active(e=%u,se=%u,spi=%u) bound=%u runners=%u",
             static_cast<unsigned>(act_->spi_diag_census_logs),
             act_->cached_runner_name.c_str(), act_,
             static_cast<unsigned>(diag_census.total_effects),
             static_cast<unsigned>(diag_census.total_segment_effects),
             static_cast<unsigned>(diag_census.active_effects),
             static_cast<unsigned>(diag_census.active_segment_effects),
             static_cast<unsigned>(diag_census.active_spi_effects),
             static_cast<unsigned>(diag_census.bound_sequences),
             static_cast<unsigned>(diag_census.runner_count));
    act_->spi_diag_census_logs++;
  }

  // Force reset runner memory whenever an effect is selected/started
  // to ensure multi-segment sequences synchronize and start fresh.
  if (!act_->segment_runners.empty()) {
    for (auto *r : act_->segment_runners) {
      r->reset();
    }
  } else if (act_->runner != nullptr) {
    act_->runner->reset();
  }

#ifdef USE_CFX_SEQUENCE
  // CFX-036: Sequence Auto-Bind must happen after activation and runner reset.
  // Earlier binding gets clobbered by the defensive start() initialization
  // above, which clears intro state and HA-event suppression flags. Binding
  // here makes the sequence-owned policy the final startup state while still
  // landing before control resolution and intro-mode selection.
  for (auto *seq : chimera_fx::CFXSequence::instances) {
    if (seq->is_running() && seq->owns_light(this->get_light_state())) {
      seq->apply_binding_to_effect_(this);
      break;
    }
  }

  if (cfg_ != nullptr) {
    if (cfg_->pending_sequence_speed.has_value()) {
      act_->sequence_speed = cfg_->pending_sequence_speed;
      cfg_->pending_sequence_speed.reset();
    }
    if (cfg_->pending_sequence_intensity.has_value()) {
      act_->sequence_intensity = cfg_->pending_sequence_intensity;
      cfg_->pending_sequence_intensity.reset();
    }
    if (cfg_->pending_sequence_palette.has_value()) {
      act_->sequence_palette = cfg_->pending_sequence_palette;
      cfg_->pending_sequence_palette.reset();
    }
    if (cfg_->pending_sequence_mirror.has_value()) {
      act_->sequence_mirror = cfg_->pending_sequence_mirror;
      cfg_->pending_sequence_mirror.reset();
    }
    if (cfg_->pending_sequence_autotune.has_value()) {
      act_->sequence_autotune = cfg_->pending_sequence_autotune;
      cfg_->pending_sequence_autotune.reset();
    }
  }

  const bool owns_speed = act_->sequence_speed.has_value();
  const bool owns_intensity = act_->sequence_intensity.has_value();
  const bool owns_palette = act_->sequence_palette.has_value();
  const bool owns_mirror = act_->sequence_mirror.has_value();
  if (!act_->segment_runners.empty()) {
    for (auto *r : act_->segment_runners) {
      r->sequence_owns_speed_ = owns_speed;
      r->sequence_owns_intensity_ = owns_intensity;
      r->sequence_owns_palette_ = owns_palette;
      r->sequence_owns_mirror_ = owns_mirror;
    }
  } else if (act_->runner != nullptr) {
    act_->runner->sequence_owns_speed_ = owns_speed;
    act_->runner->sequence_owns_intensity_ = owns_intensity;
    act_->runner->sequence_owns_palette_ = owns_palette;
    act_->runner->sequence_owns_mirror_ = owns_mirror;
  }
#endif

  // Pre-seed the UI with this effect's default palette — only when autotune is
  // active. When autotune is OFF, the user's last palette selection is
  // preserved.
  bool autotune_will_run = true;
  {
    switch_::Switch *at_sw =
        (act_->controller && act_->controller->get_autotune())
            ? act_->controller->get_autotune()
            : this->local_autotune_();
    if (act_->sequence_autotune.has_value())
      autotune_will_run = act_->sequence_autotune.value();
    else if (this->has_autotune_preset_())
      autotune_will_run = this->autotune_preset_val_();
    else if (at_sw != nullptr)
      autotune_will_run = at_sw->state;
  }
  const bool transient_autotune_context =
#ifdef USE_CFX_SEQUENCE
      (act_->active_sequence != nullptr || act_->sequence_autotune.has_value());
#else
      false;
#endif

  if (autotune_will_run && act_->controller && !transient_autotune_context) {
    select::Select *palette_sel_init = act_->controller->get_palette()
                                           ? act_->controller->get_palette()
                                           : this->local_palette_();
    if (palette_sel_init != nullptr) {
      std::string pal_name = "Default";
      if (this->is_monochromatic_(this->effect_id_)) {
        pal_name = "Solid";
      }

      if (palette_sel_init->current_option() != pal_name) {
        auto call = palette_sel_init->make_call();
        call.set_option(pal_name);
        call.perform();
      }
    }
  }

  this->run_controls_();

  // CFX-048: initialization overhead cleanup.
  // We sync the activation state flags once, then rely on run_controls_ for the rest.
  CFXControl *c = act_->controller;
  {
    bool autotune_enabled = true;
    switch_::Switch *autotune_sw = (c && c->get_autotune()) ? c->get_autotune() : this->local_autotune_();
    if (act_->sequence_autotune.has_value()) autotune_enabled = act_->sequence_autotune.value();
    else if (this->has_autotune_preset_()) autotune_enabled = this->autotune_preset_val_();
    else if (autotune_sw != nullptr) autotune_enabled = autotune_sw->state;
    act_->autotune_active = autotune_enabled;
    if (autotune_enabled) this->apply_autotune_defaults_();
  }

  // Pass force_white flag down to the underlying Native CFXRunners
  {
    bool force_white_requested =
        this->has_force_white_preset_()
            ? this->force_white_preset_val_()
            : (c != nullptr && c->get_force_white() != nullptr &&
               c->get_force_white()->state);
    uint8_t force_white_palette =
        act_->runner != nullptr ? act_->runner->getPalette()
                                : this->get_palette_index_();
    bool force_white_active =
        this->resolve_force_white_active_(force_white_requested,
                                          force_white_palette);
    if (!act_->segment_runners.empty()) {
      for (auto *r : act_->segment_runners) {
        r->force_white_active_ = force_white_active;
        r->setBakeBrightness(true);
      }
    } else if (act_->runner) {
      act_->runner->force_white_active_ = force_white_active;
      act_->runner->setBakeBrightness(this->is_virtual_segment_);
    }
    act_->active_force_white = force_white_active;
  }

  act_->initial_preset_applied = true;
  esphome::App.feed_wdt();

  // Visualizer: Notify metadata (only for non-virtual segments —
  // get_output() returns CFXVirtualSegmentLight for virtual segments,
  // which is NOT a CFXLightOutput and cannot be static_cast'd safely).
  if (!this->is_virtual_segment_) {
    auto *out = static_cast<cfx_light::CFXLightOutput *>(
        this->get_light_state()->get_output());
    if (out != nullptr) {
      std::string pal_name = "";
      select::Select *palette_sel = (c && c->get_palette()) ? c->get_palette() : this->local_palette_();
      if (palette_sel && palette_sel->has_state()) {
        // audit 2.2: c_str() directly on the reference — no std::string copy
        const char *opt = palette_sel->current_option().c_str();
        if (opt != nullptr)
          pal_name = opt;
      }

      // Resolve "Default" to actual palette name if possible
      if ((pal_name.empty() || pal_name == "Default") && act_->runner) {
        pal_name = this->get_palette_name_(act_->runner->getPalette());
      }

      out->send_visualizer_metadata(this->get_name(), pal_name);
      act_->last_sent_palette = pal_name;
    }
  }

  // State Machine Init: Check if we are turning ON from OFF
  auto *state = this->get_light_state();
  if (state != nullptr) {
    // Bypass intro for effects that have their own embedded startup animations
    if (this->effect_id_ == 158 || this->effect_id_ == 159) {
      is_fresh_turn_on = false;
    }

    // Preserve active intro state against redundant turn_on() calls triggered
    // by segment->master->segment sync recursion
    if (!act_->intro_active) {
      act_->intro_active = is_fresh_turn_on;
      if (act_->intro_active) {
        act_->intro_start_time = millis_64();
      }
    } else {
      // Intro is already running, do NOT kill it just because the light is now
      // ON
      is_fresh_turn_on = true;
    }

    if (act_->intro_active && act_->controller == nullptr) {
      // Try linking again if missed
      act_->controller = CFXControl::find(this->get_light_state());
      this->run_controls_(); // Re-run to pull pointers
    }
  } else {
    act_->intro_active = false;
  }

  // Resolve Intro Mode (Now reflecting Presets!)
  act_->active_intro_mode = INTRO_NONE;
  select::Select *intro_sel = (c && c->get_intro_effect()) ? c->get_intro_effect() : this->local_intro_effect_();

  if (act_->intro_active) {
    // Re-resolve in case preset changed it
    if (intro_sel == nullptr && c != nullptr) {
      intro_sel = c->get_intro_effect();
    }
    // Also check local member if controller failed
    if (intro_sel == nullptr)
      intro_sel = this->local_intro_effect_();

    // 1. Highest Priority: Embedded Monochromatic Presets
    MonochromaticPreset preset =
        this->get_monochromatic_preset_(this->effect_id_);
    if (preset.is_active) {
      act_->intro_active = true;
      act_->active_intro_mode = preset.intro_mode;
    } else {
      // 2. YAML/runtime presets override the live UI selectors.
      if (this->has_intro_preset_()) {
        act_->active_intro_mode = this->intro_preset_val_();
      } else if (intro_sel != nullptr && intro_sel->has_state()) {
        // audit 2.2: c_str() directly on the reference — no std::string copy
        const char *opt = intro_sel->current_option().c_str();
        std::string s = opt ? opt : "";
        if (s == "Wipe")
          act_->active_intro_mode = INTRO_MODE_WIPE;
        else if (s == "Fade")
          act_->active_intro_mode = INTRO_MODE_FADE;
        else if (s == "Center")
          act_->active_intro_mode = INTRO_MODE_CENTER;
        else if (s == "Glitter")
          act_->active_intro_mode = INTRO_MODE_GLITTER;
        else if (s == "Twin Pulse")
          act_->active_intro_mode = INTRO_MODE_TWIN_PULSE;
        else if (s == "Morse Code")
          act_->active_intro_mode = INTRO_MODE_MORSE;
        else if (s == "Quadrant")
          act_->active_intro_mode = INTRO_MODE_QUADRANT;
        else if (s == "Eclipse")
          act_->active_intro_mode = INTRO_MODE_ECLIPSE;
        else if (s == "Gas Discharge")
          act_->active_intro_mode = INTRO_MODE_GAS_DISCHARGE;
        else if (s == "Harmonic Settle")
          act_->active_intro_mode = INTRO_MODE_HARMONIC_SETTLE;
        else if (s == "Lithograph")
          act_->active_intro_mode = INTRO_MODE_LITHOGRAPH;
        else if (s == "Pressurize")
          act_->active_intro_mode = INTRO_MODE_HYDRAULICS;
        else if (s == "Dropping")
          act_->active_intro_mode = INTRO_MODE_DROPPING;
        else if (s == "Construct")
          act_->active_intro_mode = INTRO_MODE_ASSEMBLY;
        else if (s == "Inertia Sweep")
          act_->active_intro_mode = INTRO_MODE_INERTIA_SWEEP;
        else if (s == "Sonar Reveal")
          act_->active_intro_mode = INTRO_MODE_SONAR_REVEAL;
        else if (s == "Venetian")
          act_->active_intro_mode = INTRO_MODE_VENETIAN;
        else if (s == "Crystallize")
          act_->active_intro_mode = INTRO_MODE_CRYSTALLIZE;
        else if (s == "Deep Breathe")
          act_->active_intro_mode = INTRO_MODE_DEEP_BREATHE;
        else if (s == "Moiré Shift")
          act_->active_intro_mode = INTRO_MODE_MOIRE_SHIFT;
        else if (s == "Resonance")
          act_->active_intro_mode = INTRO_MODE_RESONANCE_FILL;
        else if (s == "Telemetry")
          act_->active_intro_mode = INTRO_MODE_TELEMETRY;
        else if (s == "Stellar Dust")
          act_->active_intro_mode = INTRO_MODE_STELLAR_DUST;
        else if (s == "Interference")
          act_->active_intro_mode = INTRO_MODE_INTERFERENCE;
        else if (s == "Tidal Surge")
          act_->active_intro_mode = INTRO_MODE_TIDAL_SURGE;
        else if (s == "Impact Flare")
          act_->active_intro_mode = INTRO_MODE_IMPACT_FLARE;
      }
    }

    // 10. Intro Duration Priority Chain (Calculated ONCE during start)
    uint32_t duration_ms = 1000; // Final Default: 1.0s
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();

    if (act_->active_intro_mode == INTRO_MODE_MORSE) {
      // A. Highest: Morse Code Timing (Message length based)
      uint8_t current_speed = 128;
      number::Number *intensity_num =
          (act_->controller && act_->controller->get_intensity())
              ? act_->controller->get_intensity()
              : this->local_intensity_();
      if (intensity_num != nullptr && intensity_num->has_state()) {
        current_speed = (uint8_t)intensity_num->state;
      }
      uint32_t unit_ms = 80 + ((255 - current_speed) * 100 / 255);
      duration_ms = 35 * unit_ms; // ~ "HELLO"
    } else if (auto duration_override =
                   this->resolve_inout_duration_override_ms_(dur_num);
               duration_override.has_value()) {
      // B. Runtime override: sequence/YAML preset beats the live UI slider.
      duration_ms = duration_override.value();
    } else {
      // D. Monochromatic Preset Fallback: Speed Slider
      MonochromaticPreset preset =
          this->get_monochromatic_preset_(this->effect_id_);
      if (preset.is_active) {
        if (this->effect_id_ == 182) { // Gas Discharge
          duration_ms = 2200;
        } else if (this->effect_id_ == 183) { // Fluorescent Start
          duration_ms = 800;
        } else {
          number::Number *speed_num = this->local_speed_();
          if (speed_num == nullptr && act_->controller != nullptr)
            speed_num = act_->controller->get_speed();

          if (speed_num != nullptr && speed_num->has_state()) {
            // Map Speed (0-255) to Duration (500ms up to 10000ms)
            float speed_val = speed_num->state;
            duration_ms = (uint32_t)(500.0f + (speed_val / 255.0f * 9500.0f));
          } else {
            duration_ms = 1000; // Standard 1s default
          }
        }
      }
    }
    // Clamp upward to each mode's minimum stable runtime so the state machine
    // never terminates a long-form architectural intro before its own shader
    // logic reaches a natural full frame.
    uint32_t intro_mode_min =
        this->get_intro_mode_min_duration_ms_(act_->active_intro_mode);
    if (intro_mode_min > 0 && duration_ms < intro_mode_min) {
      duration_ms = intro_mode_min;
    }

    act_->active_intro_duration_ms = duration_ms;

    // Cache Speed and Intensity for Intro
    act_->active_intro_speed = 128; // fallback
    act_->active_intro_intensity = 128; // fallback
    
    number::Number *speed_num =
        (c && c->get_speed()) ? c->get_speed() : this->local_speed_();
    if (speed_num != nullptr && speed_num->has_state()) {
      act_->active_intro_speed = (uint8_t)speed_num->state;
    } else if (this->has_speed_preset_()) {
      act_->active_intro_speed = this->speed_preset_val_();
    } else {
      act_->active_intro_speed = this->get_default_speed_(this->effect_id_);
    }

    number::Number *inten_num =
        (c && c->get_intensity()) ? c->get_intensity() : this->local_intensity_();
    if (inten_num != nullptr && inten_num->has_state()) {
      act_->active_intro_intensity = (uint8_t)inten_num->state;
    } else if (this->has_intensity_preset_()) {
      act_->active_intro_intensity = this->intensity_preset_val_();
    } else {
      act_->active_intro_intensity = this->get_default_intensity_(this->effect_id_);
    }

    // Cache for this run
    ensure_cfg_();
    cfg_->intro_effect = intro_sel;

    if (act_->active_intro_mode == INTRO_MODE_NONE && !preset.is_active) {
      act_->intro_active = false;
    }
  }
}

void CFXAddressableLightEffect::stop() {
  light::AddressableLightEffect::stop();
  this->last_run_ = 0; // Reset per-instance rate gate for clean restart

  // Diagnostic hardening: stop() can be re-entered by ESPHome teardown paths
  // after a prior stop() already released the activation struct. In that case
  // the correct behavior is a no-op, not a crash while touching act_->strip_tag.
  if (this->act_ == nullptr) {
    ESP_LOGW(TAG, "stop() called with null activation for '%s' — skipping",
             this->get_name().c_str());
    return;
  }

  if (this->effect_id_ != 185) {
    this->trigger_on_stop();
#ifdef USE_CFX_EVENTS
#ifdef USE_CFX_SEQUENCE
    if (!act_->strip_tag.empty() && act_->active_sequence == nullptr &&
        !act_->suppress_stop_event) {
#else
    if (!act_->strip_tag.empty()) {
#endif
      std::string evt = std::string("cfx_stop:") + act_->strip_tag;
      chimera_fx::CFXEventManager::get().fire_event(evt.c_str());
    }
#endif
  }
  this->trigger_on_complete();

  // Clear intro snapshot — keep capacity for next start() to avoid realloc
  // during the transition (audit 3.1).
  act_->intro_snapshot.clear();

  // Restore the light default transition length that was suppressed in start()
  // so solid-color ON/OFF behavior works again after the effect stops.
  if (act_->saved_transition_length > 0) {
    auto *ls = this->get_light_state();
    if (ls != nullptr)
      ls->set_default_transition_length(act_->saved_transition_length);
    act_->saved_transition_length = 0;
  }

  CFXControl *c = act_->controller;
  auto *state = this->get_light_state();

  if (state != nullptr && act_->runner != nullptr) {
    cfx_light::CFXLightOutput *out = nullptr;
    if (this->is_virtual_segment_) {
#ifdef USE_ESP32
      auto *vseg = static_cast<cfx_light::CFXVirtualSegmentLight *>(
          this->get_addressable_());
      if (vseg)
        out = vseg->get_parent();
#endif
    } else {
      out = static_cast<cfx_light::CFXLightOutput *>(this->get_addressable_());
    }

    if (out != nullptr) {

      // Resolve Outro Mode synchronously before dropping controller mapping
      act_->active_outro_mode = INTRO_MODE_NONE;
      select::Select *out_eff = this->local_outro_effect_();
      if (out_eff == nullptr && c != nullptr)
        out_eff = c->get_outro_effect();

      // 1. Highest Priority: Embedded Monochromatic Presets
      MonochromaticPreset preset =
          this->get_monochromatic_preset_(this->effect_id_);

      if (preset.is_active) {
        act_->active_outro_mode = preset.outro_mode;
      } else {
        // 2. YAML/runtime presets override the live UI selectors.
        if (this->has_outro_preset_()) {
          act_->active_outro_mode = this->outro_preset_val_();
        } else if (out_eff != nullptr && out_eff->has_state()) {
          std::string raw_opt_s(out_eff->current_option());
          const char *raw_opt = raw_opt_s.c_str();
          std::string s = raw_opt ? raw_opt : "";
          if (s == "Wipe")
            act_->active_outro_mode = INTRO_MODE_WIPE;
          else if (s == "Fade")
            act_->active_outro_mode = INTRO_MODE_FADE;
          else if (s == "Center")
            act_->active_outro_mode = INTRO_MODE_CENTER;
          else if (s == "Glitter")
            act_->active_outro_mode = INTRO_MODE_GLITTER;
          else if (s == "Twin Pulse")
            act_->active_outro_mode = INTRO_MODE_TWIN_PULSE;
          else if (s == "Morse Code")
            act_->active_outro_mode = INTRO_MODE_MORSE;
          else if (s == "Quadrant")
            act_->active_outro_mode = INTRO_MODE_QUADRANT;
          else if (s == "Eclipse")
            act_->active_outro_mode = INTRO_MODE_ECLIPSE;
          else if (s == "Gas Discharge")
            act_->active_outro_mode = INTRO_MODE_GAS_DISCHARGE;
          else if (s == "Harmonic Settle")
            act_->active_outro_mode = INTRO_MODE_HARMONIC_SETTLE;
          else if (s == "Lithograph")
            act_->active_outro_mode = INTRO_MODE_LITHOGRAPH;
          else if (s == "Drain")
            act_->active_outro_mode = INTRO_MODE_HYDRAULICS;
          else if (s == "Emptying")
            act_->active_outro_mode = INTRO_MODE_DROPPING;
          else if (s == "Dismantle")
            act_->active_outro_mode = INTRO_MODE_ASSEMBLY;
          else if (s == "Decelerate")
            act_->active_outro_mode = INTRO_MODE_INERTIA_SWEEP;
          else if (s == "Sonar Fade")
            act_->active_outro_mode = INTRO_MODE_SONAR_REVEAL;
          else if (s == "Close Blinds")
            act_->active_outro_mode = INTRO_MODE_VENETIAN;
          else if (s == "Erode")
            act_->active_outro_mode = INTRO_MODE_CRYSTALLIZE;
          else if (s == "Exhale")
            act_->active_outro_mode = INTRO_MODE_DEEP_BREATHE;
          else if (s == "Moiré Fade")
            act_->active_outro_mode = INTRO_MODE_MOIRE_SHIFT;
          else if (s == "Resonance Fade")
            act_->active_outro_mode = INTRO_MODE_RESONANCE_FILL;
          else if (s == "Telemetry Fade")
            act_->active_outro_mode = INTRO_MODE_TELEMETRY;
          else if (s == "Stellar Fade")
            act_->active_outro_mode = INTRO_MODE_STELLAR_DUST;
          else if (s == "Interference Fade")
            act_->active_outro_mode = INTRO_MODE_INTERFERENCE;
          else if (s == "Tidal Recede")
            act_->active_outro_mode = INTRO_MODE_TIDAL_SURGE;
          else if (s == "Center Squeeze")
            act_->active_outro_mode = OUTRO_MODE_CENTER_SQUEEZE;
          else
            act_->active_outro_mode = INTRO_MODE_NONE;
        } else {
          // No explicit outro selected means no outro.
          act_->active_outro_mode = INTRO_MODE_NONE;
        }
      }

      // 10. Outro Duration Priority Chain
      uint32_t duration_ms = 1000; // Hard Default

      number::Number *dur_num = this->local_inout_duration_();
      if (dur_num == nullptr && c != nullptr)
        dur_num = c->get_outro_duration();

      if (act_->active_outro_mode == INTRO_MODE_MORSE) {
        // A. Highest: Morse Code Timing (Message length based)
        uint8_t current_speed = 128;
        number::Number *intensity_num = (c && c->get_intensity())
                                            ? c->get_intensity()
                                            : this->local_intensity_();
        if (intensity_num != nullptr && intensity_num->has_state()) {
          current_speed = (uint8_t)intensity_num->state;
        }
        uint32_t unit_ms = 80 + ((255 - current_speed) * 100 / 255);
        duration_ms = 35 * unit_ms;
      } else if (auto duration_override =
                     this->resolve_inout_duration_override_ms_(dur_num);
                 duration_override.has_value()) {
        // B. Runtime override: sequence/YAML preset beats the live UI slider.
        duration_ms = duration_override.value();
      } else if (preset.is_active) {
        // D. Fallback: Monochromatic Mode Speed Slider
        number::Number *speed_num = this->local_speed_();
        if (speed_num == nullptr && c != nullptr)
          speed_num = c->get_speed();

        if (speed_num != nullptr && speed_num->has_state()) {
          float speed_val = speed_num->state;
          duration_ms = (uint32_t)(500.0f + (speed_val / 255.0f * 9500.0f));
        }
      } else {
        // E. Lowest: Light Default Transition
        auto *current_state = this->get_light_state();
        if (current_state != nullptr &&
            current_state->get_default_transition_length() > 0) {
          duration_ms = current_state->get_default_transition_length();
        }
      }
      uint32_t outro_mode_min =
          this->get_outro_mode_min_duration_ms_(act_->active_outro_mode);
      if (outro_mode_min > 0 && duration_ms < outro_mode_min) {
        duration_ms = outro_mode_min;
      }
      act_->active_outro_duration_ms = duration_ms;

      // Cache Intensity for Outro (since controller is detached during Outro)
      act_->active_outro_intensity = 128; // fallback
      number::Number *intensity_num = this->local_intensity_();
      if (intensity_num == nullptr && c != nullptr)
        intensity_num = c->get_intensity();

      if (intensity_num != nullptr && intensity_num->has_state()) {
        act_->active_outro_intensity = (uint8_t)intensity_num->state;
      } else {
        act_->active_outro_intensity =
            this->get_default_intensity_(this->effect_id_);
      }

      // Cache Speed for Outro
      act_->active_outro_speed = 128; // fallback
      number::Number *speed_num = this->local_speed_();
      if (speed_num == nullptr && c != nullptr)
        speed_num = c->get_speed();

      if (speed_num != nullptr && speed_num->has_state()) {
        act_->active_outro_speed = (uint8_t)speed_num->state;
      } else {
        act_->active_outro_speed = this->get_default_speed_(this->effect_id_);
      }

      act_->active_outro_brightness = state->current_values.get_brightness();

      // Cache Mirror for Outro
      act_->active_outro_mirror = false;
#ifdef USE_CFX_SEQUENCE
      if (act_->sequence_mirror.has_value()) {
        act_->active_outro_mirror = act_->sequence_mirror.value();
      } else {
#endif
        switch_::Switch *mirror_sw = this->local_mirror_();
        if (mirror_sw == nullptr && c != nullptr)
          mirror_sw = c->get_mirror();
        if (mirror_sw != nullptr)
          act_->active_outro_mirror = mirror_sw->state;
#ifdef USE_CFX_SEQUENCE
      }
#endif

      bool force_white_requested =
          this->has_force_white_preset_()
              ? this->force_white_preset_val_()
              : (c != nullptr && c->get_force_white() != nullptr &&
                 c->get_force_white()->state);
      uint8_t force_white_palette =
          act_->runner != nullptr ? act_->runner->getPalette()
                                  : this->get_palette_index_();
      act_->active_outro_force_white =
          this->resolve_force_white_active_(force_white_requested,
                                            force_white_palette);

      // Capture runners for the outro.
      // CFX-046: Virtual segment fix — when a virtual segment stops, only
      // remove ITS OWN runner from act_->segment_runners. Sibling runners
      // (other segments on the same physical strip) must remain intact so
      // their apply() loops continue to render. Previously, stop() cleared
      // ALL segment_runners unconditionally, darkening every other segment.
      //
      // For non-virtual (master) stops and single-runner lights the original
      // behaviour is preserved: capture everything and clear.
      auto captured_runners = std::make_shared<std::vector<CFXRunner *>>();
      // Capture the sequence pointer now — clear_active_binding() in stop()
      // nulls active_sequence_ before the outro callback runs, so we must
      // grab it here while it is still valid.
#ifdef USE_CFX_SEQUENCE
      CFXSequence *captured_sequence = act_->active_sequence;
#else
      CFXSequence *captured_sequence = nullptr;
#endif

      if (!act_->segment_runners.empty()) {
        if (this->is_virtual_segment_) {
          // CFX-046: Drain Core 0 before mutating segment_runners to prevent
          // a race where Core 0 is iterating core0_slice_ (which holds runner
          // pointers from segment_runners) while Core 1 erases and deletes.
          CFXScheduler::get().drain_core0();

          // Identify this segment's own runner by segment_id and remove only it.
          std::string my_id;
#ifdef USE_ESP32
          auto *vseg = static_cast<cfx_light::CFXVirtualSegmentLight *>(
              this->get_addressable_());
          if (vseg)
            my_id = vseg->get_segment_id();
#endif
          auto &runners = act_->segment_runners;
          for (auto it = runners.begin(); it != runners.end(); ++it) {
            if ((*it)->get_segment_id() == my_id) {
              if (c)
                c->unregister_runner(*it);
              captured_runners->push_back(*it);
              runners.erase(it);
              break; // Only one runner per segment id
            }
          }
          // Keep act_->runner pointing at segment_runners[0] if any remain,
          // so sibling apply() calls pass the null-runner guard at line 1124.
          if (!runners.empty()) {
            act_->runner = runners[0];
          } else {
            // Last segment stopped — full teardown below.
            act_->runner = nullptr;
            act_->segments_initialized = false;
          }
          // Wake mono idle so surviving siblings commit one fresh frame,
          // ensuring DMA reflects the correct post-stop pixel state.
          act_->mono_dirty = true;
        } else {
          // Non-virtual master stop: capture and clear all runners as before.
          for (auto *r : act_->segment_runners) {
            if (c)
              c->unregister_runner(r);
            captured_runners->push_back(r);
          }
          act_->segment_runners.clear();
          act_->segments_initialized = false;
          act_->runner = nullptr;
        }
      } else {
        if (c)
          c->unregister_runner(act_->runner);
        captured_runners->push_back(act_->runner);
        act_->runner = nullptr; // Null here so start() creates a fresh one later
      }

      // Safely detach from effect runner system only when fully torn down.
      // For virtual segment partial stops, controller and intro state must
      // remain valid for the surviving sibling runners.
      if (act_->runner == nullptr) {
        act_->controller = nullptr;
        act_->intro_active = false;
        act_->outro_active = false;
      }

      auto *output = this->get_light_state()->get_output();
      auto *it_light = static_cast<light::AddressableLight *>(output);
#ifdef USE_ESP32
      CFXActivation *captured_act = act_;
      // `out` is already correctly set above (via vseg->get_parent() for
      // virtual segments, or direct cast for non-virtual). Register outro.
      if (out != nullptr) {
        if (act_->active_outro_mode == INTRO_MODE_NONE) {
          this->act_ = nullptr;
          for (auto *r : *captured_runners)
            delete r;
          captured_runners->clear();
          captured_act->outro_start_time = 0;
          delete captured_act;
          return;
        } else {
          this->act_ = nullptr;
          out->add_outro_callback([this, it_light, captured_runners,
                                   captured_sequence, captured_act]() -> bool {
            auto *current_state = this->get_light_state();

            if (current_state != nullptr &&
                current_state->remote_values.is_on()) {
              // Effect was completely changed or light remained ON.
              // Abort the outro and delete all captured runners cleanly.

              for (auto *r : *captured_runners)
                delete r;
              captured_runners->clear();
              captured_act->outro_start_time = 0; // Reset for the NEXT outro
              delete captured_act;
              return true;
            }

            // Initialize outro start time on the very first allowed frame
            if (captured_act->outro_start_time == 0) {
              captured_act->outro_start_time = millis_64();
              captured_act->hydraulics_last_ms = captured_act->outro_start_time;
              if (captured_act->active_outro_mode == INTRO_MODE_HYDRAULICS) {
                captured_act->hydraulics_fluid_level = (float)it_light->size();
                captured_act->hydraulics_particle_count = 0; // audit 3.3
              }
            }

            // Run outro frame on ALL captured segment runners
            bool done = false;
            CFXActivation *live_act = this->act_;
            this->act_ = captured_act;
            for (auto *r : *captured_runners) {
              chimera_fx::instance = r;
              done = this->run_outro_frame(*it_light, r);
            }
            this->act_ = live_act;
            chimera_fx::instance = nullptr;

            // CFX-046b: While a segment outro is animating into the shared DMA
            // buffer, its pixels may bleed into sibling segment ranges. Keep
            // mono_dirty set on every outro frame so surviving idle siblings
            // repaint their pixel ranges each frame and correct any contamination.
            if (!done)
              captured_act->mono_dirty = true;

            if (done) {
              ESP_LOGV("chimera_fx", "[T05] outro DONE: effect_id=%u is_mono=%d",
                       (unsigned)this->effect_id_,
                       (int)this->get_monochromatic_preset_(this->effect_id_)
                           .is_active);
#ifdef USE_CFX_EVENTS
              // Fire cfx_complete when the outro animation finishes.
              // If is_sequence_outro_ is true, the sequence already fired
              // completion (report_event_complete was called before stop()),
              // so we skip here to avoid double-firing.
              // cfx_complete fires when a real outro completes: architectural
              // preset or user-configured outro selector. INTRO_MODE_NONE is the
              // default fade-to-black on every light-off — not a meaningful
              // outro, must not fire cfx_complete.
              if (!captured_act->suppress_complete_event &&
                  !captured_act->is_sequence_outro &&
                  captured_act->active_outro_mode != INTRO_MODE_NONE) {
#ifdef USE_CFX_SEQUENCE
                if (captured_sequence != nullptr) {
                  // Sequence-driven path: route through report_event_complete()
                  // so that on_cfx_complete YAML automations fire in addition
                  // to the HA cfx_complete event. Without this, only the HA
                  // event fires and on-device triggers are silently skipped.
                  captured_sequence->report_event_complete();
                } else
#endif
                {
                  // Standalone (no sequence bound): fire HA event directly.
                  // Use instance act_->strip_tag — no singleton dependency.
                  if (!captured_act->strip_tag.empty()) {
                    std::string evt =
                        std::string("cfx_complete:") + captured_act->strip_tag;
                    chimera_fx::CFXEventManager::get().fire_event(evt.c_str());
                  }
                }
              }
#endif
              for (auto *r : *captured_runners)
                delete r;
              captured_runners->clear();
              captured_act->outro_start_time = 0; // Reset for the NEXT outro
              delete captured_act;
            }
            return done;
          });

          return;
        }
      }
#endif
    }

    // Normal Stop / Cleanup (Failsafe)
    if (!act_->segment_runners.empty()) {
      for (auto *r : act_->segment_runners) {
        if (r != act_->runner && act_->controller) {
          act_->controller->unregister_runner(r);
        }
        if (r != act_->runner) {
          delete r;
        }
      }
      act_->segment_runners.clear();
      act_->segments_initialized = false;
    }
    if (act_->runner != nullptr) {
      if (act_->controller) {
        act_->controller->unregister_runner(act_->runner);
      }
      delete act_->runner;
      act_->runner = nullptr;
    }
    act_->controller = nullptr;
    act_->intro_active = false;
    act_->outro_active = false;
  }

  // Free the activation struct — released back to heap until next start().
  delete this->act_;
  this->act_ = nullptr;

} // CFXAddressableLightEffect::stop()

void CFXAddressableLightEffect::apply(light::AddressableLight &it,
                                      const Color &current_color) {
  // Guard against apply() being called before start() allocates act_.
  if (this->act_ == nullptr)
    return;

  cfx_light::CFXLightOutput *diag_out = resolve_diag_output(this);

  if (diag_out != nullptr && diag_out->is_spi_transport() &&
      this->act_->spi_diag_apply_logs < 6) {
    std::string seq_name_storage = "-";
#ifdef USE_CFX_SEQUENCE
    if (this->act_->active_sequence != nullptr) {
      seq_name_storage = this->act_->active_sequence->get_name();
    }
#endif
    ESP_LOGD("chimera_fx",
             "SPI diag apply[%u]: runner=%s tag=%s seq=%s seq_ptr=%p act=%p "
             "mono_idle=%d intro=%d outro=%d completion=%d",
             static_cast<unsigned>(this->act_->spi_diag_apply_logs),
             this->act_->cached_runner_name.c_str(),
             this->act_->strip_tag.c_str(), seq_name_storage.c_str(),
             this->act_->active_sequence, this->act_, this->act_->mono_idle,
             this->act_->intro_active, this->act_->outro_active,
             this->act_->completion_pending);
    this->act_->spi_diag_apply_logs++;
  }

  // CFX-004: Use RAII InstanceGuard so the global pointer is always restored
  // on every return path, including the throttle early-exit below.
  // This prevents "strip bleeding" in multi-strip configurations.
  chimera_fx::InstanceGuard apply_guard(act_->runner);

  // CFX-048: Global Frame Budget Guard
  // Protect the 30ms API heartbeat window. If other strips or components have 
  // already consumed the loop budget, defer this frame to keep the connection alive.
  // IDLE segments (mono_idle) bypass this guard — they do no computation and
  // skipping them causes pixel starvation on the DMA buffer (segments go dark).
  static uint32_t frame_budget_window_start_ms = 0;
  static uint8_t spi_budget_skip_logs = 0;
  uint32_t now_ms = esphome::millis();
  if (frame_budget_window_start_ms == 0 ||
      (now_ms - frame_budget_window_start_ms) >= 30) {
    frame_budget_window_start_ms = now_ms;
  }
  if (diag_out != nullptr && diag_out->is_spi_transport()) {
    uint32_t delta_ms = 0;
    if (this->act_->spi_diag_last_apply_ms != 0) {
      delta_ms = now_ms - this->act_->spi_diag_last_apply_ms;
    }
    this->act_->spi_diag_last_apply_ms = now_ms;

    if (this->act_->spi_diag_heartbeat_logs < 8 &&
        (this->act_->spi_diag_heartbeat_logs == 0 || delta_ms > 25 ||
         this->act_->spi_diag_apply_logs == 6)) {
      SPIDiagCensus diag_census = collect_spi_diag_census();
      ESP_LOGD("cfx_seq",
               "SPI diag heartbeat[%u]: effect=%s act=%p dt=%ums totals(e=%u,se=%u) "
               "active(e=%u,se=%u,spi=%u) bound=%u runners=%u",
               static_cast<unsigned>(this->act_->spi_diag_heartbeat_logs),
               this->act_->cached_runner_name.c_str(), this->act_, delta_ms,
               static_cast<unsigned>(diag_census.total_effects),
               static_cast<unsigned>(diag_census.total_segment_effects),
               static_cast<unsigned>(diag_census.active_effects),
               static_cast<unsigned>(diag_census.active_segment_effects),
               static_cast<unsigned>(diag_census.active_spi_effects),
               static_cast<unsigned>(diag_census.bound_sequences),
               static_cast<unsigned>(diag_census.runner_count));
      this->act_->spi_diag_heartbeat_logs++;
    }
  }
  uint32_t budget_elapsed_ms = now_ms - frame_budget_window_start_ms;
  if (!act_->mono_idle && budget_elapsed_ms > 22) {
    if (spi_budget_skip_logs < 8) {
      SPIDiagCensus diag_census = collect_spi_diag_census();
      if (diag_census.active_spi_effects > 0) {
        ESP_LOGD("cfx_seq",
                 "SPI diag budget-skip[%u]: effect=%s tag=%s elapsed=%ums "
                 "active(e=%u,se=%u,spi=%u) bound=%u runners=%u",
                 static_cast<unsigned>(spi_budget_skip_logs),
                 this->act_->cached_runner_name.c_str(),
                 this->act_->strip_tag.c_str(),
                 budget_elapsed_ms,
                 static_cast<unsigned>(diag_census.active_effects),
                 static_cast<unsigned>(diag_census.active_segment_effects),
                 static_cast<unsigned>(diag_census.active_spi_effects),
                 static_cast<unsigned>(diag_census.bound_sequences),
                 static_cast<unsigned>(diag_census.runner_count));
        spi_budget_skip_logs++;
      }
    }
    ESP_LOGV("chimera_fx",
             "Frame Budget Exceeded (%ums), skipping render for '%s'",
             budget_elapsed_ms, this->get_name().c_str());
    return;
  }

  const uint64_t now = millis_64();
  if (now - this->last_run_ < this->update_interval_) {
    return;
  }
  this->last_run_ = now;
  esphome::App.feed_wdt();

  // CFX-047: apply()-level frame timing for idle FPS/Time/Jitter measurement.
  // Mirrors FrameDiagnostics::frame_start() but driven by apply() timestamps
  // so it captures true DMA throughput regardless of runner suppression.
  if (act_->mono_idle) {
    uint32_t now_us = cfx_micros();
    if (act_->idle_period_start_ms == 0)
      act_->idle_period_start_ms = (uint32_t)(now & 0xFFFFFFFF);
    if (act_->idle_last_frame_us > 0) {
      uint32_t delta_us = now_us - act_->idle_last_frame_us;
      if (delta_us < act_->idle_min_frame_us) act_->idle_min_frame_us = delta_us;
      if (delta_us > act_->idle_max_frame_us) act_->idle_max_frame_us = delta_us;
      act_->idle_total_frame_us += delta_us;
      act_->idle_frame_count++;
      if (delta_us < act_->idle_target_frame_us / 2 ||
          delta_us > act_->idle_target_frame_us * 3 / 2)
        act_->idle_jitter_count++;
    }
    act_->idle_last_frame_us = now_us;
  }

  // --- Ensure Runner(s) ---
  if (act_->runner == nullptr) {
    ESP_LOGE(
        "chimera_fx",
        "[%s] Runner is null in apply()! mono_idle=%d seg_runners=%u",
        act_->cached_runner_name.c_str(),
        (int)act_->mono_idle,
        (unsigned)act_->segment_runners.size());
    chimera_fx::instance = nullptr;
    return;
  }

  // Sync Debug State (must be AFTER runner creation to avoid null deref)
  bool debug_active =
      CFXControl::global_debug_enabled_; // Default to Master global state
  if (act_->controller && act_->controller->get_debug()) {
    debug_active = act_->controller->get_debug()->state; // Master local state
  } else if (this->local_debug_switch_()) {
    debug_active = this->local_debug_switch_()->state; // Legacy fallback
  }

  bool force_white_requested =
      this->has_force_white_preset_()
          ? this->force_white_preset_val_()
          : (act_->controller != nullptr &&
             act_->controller->get_force_white() != nullptr &&
             act_->controller->get_force_white()->state);
  uint8_t force_white_palette =
      act_->runner != nullptr ? act_->runner->getPalette()
                              : this->get_palette_index_();
  act_->active_force_white =
      this->resolve_force_white_active_(force_white_requested,
                                        force_white_palette);

  // Use the name cached in start() — avoids heap allocation every frame
  // (audit 1.1).
  const std::string &runner_name = act_->cached_runner_name;

  // Sync color and settings to all runners
  auto *state_ptr = this->get_light_state();
  uint32_t color = 0;

  if (state_ptr != nullptr) {
    // While a CFX effect is active, ESPHome's transition transformer must not
    // keep ownership of the state progression. Otherwise a non-zero
    // default_transition_length can behave like a delayed effect start.
    if (chimera_fx::LightStateProxy::has_active_transformer(state_ptr)) {
      state_ptr->current_values = state_ptr->remote_values;
      chimera_fx::LightStateProxy::stop_state_transformer(state_ptr);
    }

    // We use remote_values (the target) instead of current_values (the
    // transitioning color) to ensure the runner has the "full" color
    // immediately. This solves the "black strip on first run" issue. Also, we
    // use the RAW channels (get_*) which DO NOT have brightness applied,
    // because global_brightness_ will be applied later in setPixelColor().
    // This avoids "Double Brightness Scaling".
    float r = state_ptr->remote_values.get_red();
    float g = state_ptr->remote_values.get_green();
    float b = state_ptr->remote_values.get_blue();
    float w = state_ptr->remote_values.get_white();

    color = (uint32_t(roundf(w * 255.0f)) << 24) |
            (uint32_t(roundf(r * 255.0f)) << 16) |
            (uint32_t(roundf(g * 255.0f)) << 8) | uint32_t(roundf(b * 255.0f));

    // CFX-065: Virtual Segment Default Color Restoration
    // When sequence/cfx_set turns ON a light without specifying a color, ESPHome 
    // uses the last saved state. If no state was ever saved (e.g. fresh boot), the color 
    // channels remain 0. This causes monochromatic effects to play invisible (if dim) 
    // or go dark in mono_idle. Fallback to White if ON but color is missing.
    if (color == 0 && state_ptr->remote_values.is_on()) {
      color = 0xFFFFFFFF; // 100% White
    }

  } else {
    static uint64_t null_state_log = 0;
    if (millis_64() - null_state_log > 5000) {
      ESP_LOGW("chimera_fx", "apply() - get_light_state() is NULL!");
      null_state_log = millis_64();
    }
  }

  if (!act_->segment_runners.empty()) {
    for (auto *r : act_->segment_runners) {
      r->target_light = &it; // INJECT: Ensure we write to current buffer
      r->setDebug(debug_active);
      if (!runner_name.empty())
        r->setName(runner_name.c_str());
      r->setColor(color);
    }
  } else if (act_->runner) {
    act_->runner->target_light =
        &it; // INJECT: Ensure we write to current buffer
    act_->runner->setDebug(debug_active);
    if (!runner_name.empty())
      act_->runner->setName(runner_name.c_str());
    act_->runner->setColor(color);
  }

  // Update controls via Controller or Local entities (Crucial for
  // Speed/Intensity)
  this->run_controls_();

  // === Dynamic Gamma Update ===
  // Sync the Runner's gamma LUT with the light's current gamma setting.
  // state_ptr is already fetched above (audit 1.2).
  float current_gamma =
      state_ptr != nullptr ? state_ptr->get_gamma_correct() : 2.8f;
  if (!act_->segment_runners.empty()) {
    for (auto *r : act_->segment_runners) {
      if (abs(r->_gamma - current_gamma) > 0.01f) {
        r->setGamma(current_gamma);
      }
    }
  } else {
    if (abs(act_->runner->_gamma - current_gamma) > 0.01f) {
      act_->runner->setGamma(current_gamma);
    }
  }

  // (Lazy Binding removed — binding happens in cfx_sequence::start())

  // Sync Brightness to Runners (Master + Light Brightness)
  float bri = 1.0f;
  auto *bri_state = state_ptr; // audit 1.2: reuse cached pointer
  if (bri_state != nullptr) {
    if (
#ifdef USE_CFX_SEQUENCE
        act_->active_sequence != nullptr
#else
        false
#endif
    ) {
      // During a sequence, stop transitions only if one is actually running
      if (bri_state->current_values.get_state() < 1.0f) {
        chimera_fx::LightStateProxy::stop_state_transformer(bri_state);
      }
      
      float state_bri = bri_state->current_values.get_brightness();
      // CFX-066: Virtual Segment Default Brightness Restoration
      // When sequence/cfx_set turns ON a light without specifying a brightness,
      // ESPHome uses the last saved state. If no state was ever saved (e.g. fresh boot), 
      // the brightness remains 0.0f. This causes the segment to multiply by 0 and go dark.
      if (state_bri == 0.0f && bri_state->remote_values.is_on()) {
        state_bri = 1.0f;
      }
      
      bri = state_bri * bri_state->current_values.get_state();
    } else {
      float state_bri = bri_state->current_values.get_brightness();
      if (state_bri == 0.0f && bri_state->remote_values.is_on()) {
        state_bri = 1.0f;
      }
      bri = state_bri;
      if (act_->state == TRANSITION_NONE && !act_->intro_active &&
          act_->state != OUTRO_RUNNING) {
        bri *= bri_state->current_values.get_state();
      }
    }
  }

  // Main CFX effect Running — Multi-Segment Swap-on-Service
  // MUST RUN before intro/outro masks!
  bool is_mono_preset =
      this->get_monochromatic_preset_(this->effect_id_).is_active;

  // ── CFX-045: Monochromatic idle suppression ──────────────────────────────
  // Phase 1 — intro/outro skip (existing behaviour, preserved as-is).
  // Phase 2 — post-intro idle: once the effect has settled into solid color,
  //   suppress service() every frame. Wake for exactly one frame when color
  //   or speed changes so the new state is committed to the DMA buffer.
  bool skip_service = is_mono_preset &&
                      (act_->intro_active || act_->state == OUTRO_RUNNING);

  if (is_mono_preset && act_->mono_idle &&
      !act_->intro_active && act_->state != OUTRO_RUNNING) {

    // Detect color change: compare the color already pushed to the runner(s)
    // this frame (set unconditionally above at lines ~1176/1184) against the
    // last committed color. Using the first runner as representative — all
    // runners on a mono effect share the same color.
    uint32_t current_color = act_->runner ? act_->runner->_segment.colors[0] : 0;
    uint8_t  current_speed = act_->runner ? act_->runner->getSpeed() : 128;
    bool current_force_white = act_->active_force_white;

    if (current_color != act_->mono_last_color ||
        current_speed != act_->mono_last_speed ||
        current_force_white != act_->mono_last_force_white) {
      // Parameter changed — wake for one frame.
      act_->mono_dirty            = true;
      act_->mono_last_color       = current_color;
      act_->mono_last_speed       = current_speed;
      act_->mono_last_force_white = current_force_white;
    }

    if (act_->mono_dirty) {
      // Service this frame, then go back to sleep.
      act_->mono_dirty = false;
      skip_service = false;
    } else {
      // Truly idle — skip service entirely, but still emit diagnostic logs
      // at the normal interval so the debug output stays populated.
      const char *idle_name = act_->cached_runner_name.empty()
                                  ? nullptr
                                  : act_->cached_runner_name.c_str();
      bool did_log = false;
      if (!act_->segment_runners.empty()) {
        for (auto *r : act_->segment_runners) {
          r->diagnostics.idle_log(idle_name,
                                  act_->idle_frame_count,
                                  act_->idle_period_start_ms,
                                  act_->idle_total_frame_us,
                                  act_->idle_jitter_count);
          if (r->diagnostics.last_log_time > act_->idle_period_start_ms)
            did_log = true;
        }
      } else if (act_->runner) {
        act_->runner->diagnostics.idle_log(idle_name,
                                           act_->idle_frame_count,
                                           act_->idle_period_start_ms,
                                           act_->idle_total_frame_us,
                                           act_->idle_jitter_count);
        if (act_->runner->diagnostics.last_log_time > act_->idle_period_start_ms)
          did_log = true;
      }
      // Reset all timing accumulators after logging so next period is clean.
      if (did_log) {
        act_->idle_frame_count     = 0;
        act_->idle_period_start_ms = 0;
        act_->idle_last_frame_us   = 0;
        act_->idle_min_frame_us    = UINT32_MAX;
        act_->idle_max_frame_us    = 0;
        act_->idle_total_frame_us  = 0;
        act_->idle_jitter_count    = 0;
      }

      // Fix 2 — DMA scrub detection.
      // If the Master Light has an active ESPHome transformer (e.g. a colour
      // transition triggered by a state change), it is writing a solid colour
      // into the shared DMA buffer every frame, silently overwriting the pixel
      // data our idling runners last committed.  mono_dirty is already the
      // correct "wake for one frame" signal — set it here so the next iteration
      // re-services all runners and repaints the correct colour.  Cost when
      // truly idle and no transformer is active: one pointer dereference per
      // frame — negligible compared to CFX-045 savings.
      auto *master_ls = this->get_light_state();
      if (master_ls != nullptr &&
          chimera_fx::LightStateProxy::has_active_transformer(master_ls)) {
        act_->mono_dirty = true;
        ESP_LOGV("chimera_fx",
                 "[%s] mono_idle: Master Light transformer active — "
                 "scheduling repaint to shield DMA buffer.",
                 act_->cached_runner_name.c_str());
      }

      skip_service = true;
    }
  }

  if (!skip_service) {
    CFXScheduler::get().set_force_sequential(
        should_force_spi_sequential_dispatch());

    if (!act_->segment_runners.empty()) {
      // ── Set per-runner brightness BEFORE dispatch ────────────────────────
      // global_brightness_ must be written on Core 1 before service_runners()
      // is called: the scheduler may hand runners to Core 0 immediately after
      // xTaskNotifyGive(). Writing brightness inside the Core 0 task would race.
      auto *cfx_out = static_cast<cfx_light::CFXLightOutput *>(
          this->get_light_state()->get_output());
      for (size_t i = 0; i < act_->segment_runners.size(); i++) {
        auto *r = act_->segment_runners[i];
        float seg_bri = bri;
        if (cfx_out != nullptr &&
            i < cfx_out->get_segment_light_states().size()) {
          auto *seg_state = cfx_out->get_segment_light_states()[i];
          if (seg_state != nullptr) {
            float inner_state_bri = seg_state->current_values.get_brightness();
            // CFX-066 Fallback
            if (inner_state_bri == 0.0f && seg_state->remote_values.is_on()) {
              inner_state_bri = 1.0f;
            }
            seg_bri *= inner_state_bri;
            
            if (act_->state == TRANSITION_NONE && !act_->intro_active &&
                act_->state != OUTRO_RUNNING) {
              seg_bri *= seg_state->current_values.get_state();
            }
          }
        }
        r->global_brightness_ = seg_bri;
      }

      // ── Parallel dispatch — Core 0 handles second half on dual-core chips ─
      // Falls through to sequential loop on single-core or when task not live.
      CFXScheduler::get().service_runners(act_->segment_runners);

      // ── Post-dispatch: completion flag + WDT ─────────────────────────────
      // Semaphore barrier in service_runners() guarantees all runners are done.
#ifdef USE_CFX_SEQUENCE
      for (auto *r : act_->segment_runners) {
        if (r->effect_complete_) {
          // CFX-044: Stack bypass. Do not call stop() synchronously in apply().
          act_->completion_pending = true;
          break;
        }
      }
#endif
      // CFX-043: Feed WDT once after all multi-segment service() calls finish.
      esphome::App.feed_wdt();
    } else if (act_->runner) {
      act_->runner->global_brightness_ = bri;
      CFXScheduler::get().service_runner(act_->runner);
#ifdef USE_CFX_SEQUENCE
      // CFX-044: Stack bypass. Do not call stop() synchronously in apply().
      if (act_->runner->effect_complete_) {
        act_->completion_pending = true;
      }
#endif
    }
  }

#ifdef USE_CFX_SEQUENCE
  // CFX-run: Check effect_complete_ regardless of skip_service so that
  // IDLE monochromatic runners (skip_service=true) can still signal sequence
  // completion. The flag is set at mono_idle entry when sequence_iterations > 0.
  if (act_->completion_pending == false) {
    if (!act_->segment_runners.empty()) {
      for (auto *r : act_->segment_runners) {
        if (r->effect_complete_) {
          act_->completion_pending = true;
          break;
        }
      }
    } else if (act_->runner && act_->runner->effect_complete_) {
      act_->completion_pending = true;
    }
  }
#endif

  // === State Machine: Intro vs Main Effect ===

  // CFX-035b: Detect right here — after the service block — whether the main
  // effect is progressive (Wipe/Sweep etc.). The main runner has been serviced
  // above and its current_leading_pixel is up-to-date. If it is >= 0 the effect
  // will supply its own 0→100% milestone data after the intro, so we must NOT
  // also fire milestone events from the intro's wipe progress.
  // Monochromatic effects (skip_service=true → runner stalled → lp = -1) and
  // non-progressive effects (Aurora, Ocean → no leading pixel → lp = -1) leave
  // this false so their intro progress fires cfx_reach normally.
  if (act_->intro_active && !act_->intro_suppresses_milestones) {
    int32_t main_lp = -1;
    if (!act_->segment_runners.empty())
      main_lp = act_->segment_runners[0]->current_leading_pixel;
    else if (act_->runner)
      main_lp = act_->runner->current_leading_pixel;
    if (main_lp >= 0 && act_->active_intro_mode != INTRO_MODE_IMPACT_FLARE &&
        act_->active_intro_mode != INTRO_MODE_TIDAL_SURGE) {
      act_->intro_suppresses_milestones = true;
    }
  }

  bool needs_autotune = (act_->autotune_active &&
#ifdef USE_CFX_SEQUENCE
                         (act_->active_sequence == nullptr || is_mono_preset));
#else
                         true);
#endif

  if (act_->intro_active &&
#ifdef USE_CFX_SEQUENCE
      (act_->active_sequence == nullptr || is_mono_preset)) {
#else
      true) {
#endif
    // Run intro on ALL segments (swap-on-service pattern)
    // This acts as a mask on top of the already-rendered main effect.
    if (!act_->segment_runners.empty()) {
      for (auto *r : act_->segment_runners) {
        chimera_fx::InstanceGuard intro_seg_guard(
            r); // CFX-004: scoped per-iteration
        this->run_intro(it, current_color);
      }
    } else {
      chimera_fx::InstanceGuard intro_guard(
          act_->runner); // CFX-004: scoped single-runner
      this->run_intro(it, current_color);
    }

    // CFX-035b: Fire progress-based milestones from the intro for monochromatic
    // effects (intro IS the effect) and non-progressive intros (Aurora,
    // Ocean…). Progressive effects set intro_suppresses_milestones=true above
    // so this branch is skipped for them — their milestones come from their own
    // runner.
    if (!act_->intro_suppresses_milestones) {
      float milestone_pct = 0.0f;

      uint64_t _elapsed = millis_64() - act_->intro_start_time;
      float _prog = (act_->active_intro_duration_ms > 0)
                        ? ((float)_elapsed / (float)act_->active_intro_duration_ms)
                        : 1.0f;
      if (_prog > 1.0f)
        _prog = 1.0f;

      // INTRO_MODE_IMPACT_FLARE: milestones must track the meteor head pixel,
      // not wall-clock time. run_intro() has already written current_leading_pixel.
      // We ONLY fire milestones during Phase 1 (Meteor bead movement, 0->75% progress).
      // Phase 2 (Impact/Reverse-Wipe) is excluded to prevent double milestones.
      if (act_->active_intro_mode == INTRO_MODE_IMPACT_FLARE) {
        if (_prog <= 0.75f) {
          CFXRunner *r = !act_->segment_runners.empty() ? act_->segment_runners[0]
                                                        : act_->runner;
          int32_t lp = (r != nullptr) ? r->current_leading_pixel : -1;
          if (lp >= 0) {
            uint16_t slen = (r != nullptr) ? r->_segment.length() : 1;
            if (slen == 0)
              slen = 1;
            milestone_pct = ((float)lp / (float)slen) * 100.0f;
            if (milestone_pct > 100.0f)
              milestone_pct = 100.0f;
            // Always fire per-light milestones with own tag; also call sequence
            // for on_cfx_reach YAML triggers (same split as check_positional_triggers).
            this->check_milestones_(milestone_pct);
#ifdef USE_CFX_SEQUENCE
            if (act_->active_sequence != nullptr)
               act_->active_sequence->check_positional_triggers((int32_t)(milestone_pct * 10.0f), 1001);
#endif
          }
        }
      } else {
        milestone_pct = _prog * 100.0f;
        // Always fire per-light milestones with own tag; also call sequence
        // for on_cfx_reach YAML triggers (same split as check_positional_triggers).
        this->check_milestones_(milestone_pct);
#ifdef USE_CFX_SEQUENCE
        if (act_->active_sequence != nullptr)
           act_->active_sequence->check_positional_triggers((int32_t)(milestone_pct * 10.0f), 1001);
#endif
      }
    }

    if (millis_64() - act_->intro_start_time > act_->active_intro_duration_ms) {
      act_->intro_active = false;

      // CFX-031: Reset milestones when intro ends so the main effect starts
      // its own 0->100% sweep cleanly. Without this, the intro's wipe pass
      // already consumed milestones 5..100, and the effect's first forward
      // pass would appear to start at whatever the intro left behind.
      this->reset_milestones_();

      // Check if Transition is enabled via config
      float trans_dur = (this->local_transition_duration_() != nullptr &&
                         this->local_transition_duration_()->has_state())
                            ? this->local_transition_duration_()->state
                            : 1.5f;

      MonochromaticPreset preset =
          this->get_monochromatic_preset_(this->effect_id_);
      if (preset.is_active) {
        trans_dur = 0.5f; // v4.1 Smooth Transition

        // CFX-045: Intro complete — enter idle suppression.
        // mono_dirty=true schedules one final service() call so the solid
        // color is committed to the DMA buffer before runners go silent.
        // mono_last_color/speed sentinels are reset so the first real frame
        // always commits regardless of previous state.
        act_->mono_idle  = true;
        act_->mono_dirty = true;
        act_->mono_last_color = 0xFFFFFFFF;
        act_->mono_last_speed = 0xFF;
        act_->mono_last_force_white = act_->active_force_white;
        act_->idle_frame_count     = 0;
        act_->idle_period_start_ms = 0;
        act_->idle_last_frame_us   = 0;
        act_->idle_min_frame_us    = UINT32_MAX;
        act_->idle_max_frame_us    = 0;
        act_->idle_total_frame_us  = 0;
        act_->idle_jitter_count    = 0;
        act_->idle_target_frame_us = this->update_interval_ * 1000;

        // CFX-run: Monochromatic presets complete when the sweep reaches 100%
        // (i.e. the intro finishes and the effect goes idle). The runner never
        // calls service() again so effect_complete_ would never be set otherwise.
        // Signal completion here when the sequence has a finite iteration count.
        if (act_->active_sequence != nullptr && act_->sequence_iterations > 0) {
          if (!act_->segment_runners.empty()) {
            for (auto *r : act_->segment_runners) {
              r->iteration_count_++;
              if (r->iteration_count_ >= act_->sequence_iterations)
                r->effect_complete_ = true;
            }
          } else if (act_->runner != nullptr) {
            act_->runner->iteration_count_++;
            if (act_->runner->iteration_count_ >= act_->sequence_iterations)
              act_->runner->effect_complete_ = true;
          }
        }
      }

      if (trans_dur > 0.0f) {
        // Snapshot Intro End State
        // Pre-allocate to strip size before capture — no realloc during
        // transition (audit 3.1).
        act_->intro_snapshot.reserve(it.size());
        act_->intro_snapshot.resize(it.size());
        for (int i = 0; i < it.size(); i++) {
          act_->intro_snapshot[i] = it[i].get();
          if ((i & 0x1F) == 0) esphome::App.feed_wdt(); // Every 32 pixels
        }
        act_->state = TRANSITION_RUNNING; // Use RUNNING to signify Active Blend
        act_->transition_start_ms = millis_64();
        act_->active_transition_duration_ms =
            (uint32_t)(trans_dur * 1000.0f);
      } else {
        act_->state = TRANSITION_NONE;
        act_->active_transition_duration_ms = 0;
      }

      // CFX-035: Reset the runner so we clear the stale current_leading_pixel
      // and is_return_phase_ that accumulated during the invisible background
      // run. Without reset(), the first frame after intro sees a mid-sweep
      // pixel position against a freshly-wiped milestone table and bursts all
      // milestones 5..100 in a single pass.
      chimera_fx::InstanceGuard start_guard(act_->runner);
      act_->runner->reset();
      act_->runner->start();
    }
  }

  // Handle Intro→Main Blending
  if (act_->state == TRANSITION_RUNNING) {
    uint32_t trans_elapsed =
        (uint32_t)(millis_64() - act_->transition_start_ms);
    float trans_dur_ms =
        (act_->active_transition_duration_ms > 0)
            ? (float) act_->active_transition_duration_ms
            : ((this->local_transition_duration_()
                    ? this->local_transition_duration_()->state * 1000.0f
                    : 1500.0f));

    // Soft Dissolve (Fairy Dust with Crossfade) Logic
    const float softness = 0.2f; // Configurable softness
    // Scale progress to ensure all pixels complete transition even with
    // softness delay
    float progress = ((float)trans_elapsed / trans_dur_ms) * (1.0f + softness);

    // Seed for deterministic random mask
    uint32_t seed = (uint32_t)act_->transition_start_ms;

    for (int i = 0; i < it.size(); i++) {
      if (i >= act_->intro_snapshot.size())
        break; // Safety

      // Generate stable random threshold for this pixel [0.0, 1.0]
      uint32_t h = i + seed;
      h = ((h >> 16) ^ h) * 0x45d9f3b;
      h = ((h >> 16) ^ h) * 0x45d9f3b;
      h = (h >> 16) ^ h;
      float threshold = (h & 0xFF) / 255.0f;

      // Calculate mix factor based on progress relative to threshold
      // If progress < threshold, mix is 0 (Intro).
      // If progress > threshold + softness, mix is 1 (Main).
      // In between, it linearly interpolates.
      float diff = (progress - threshold) / softness;
      float mix = diff < 0.0f ? 0.0f : (diff > 1.0f ? 1.0f : diff);

      if (mix < 1.0f) {
        if (mix <= 0.0f) {
          it[i] = act_->intro_snapshot[i];
        } else {
          // Blend Intro -> Main using fixed-point (audit 1.3):
          // multiply by 256 and shift right 8 — avoids 8 float muls per pixel.
          Color buf = act_->intro_snapshot[i];
          Color main = it[i].get();
          uint16_t mix_fp = (uint16_t)(mix * 256.0f); // [0, 256]
          uint16_t imix_fp = 256u - mix_fp;           // [256, 0]
          uint8_t r = (uint8_t)((buf.r * imix_fp + main.r * mix_fp) >> 8);
          uint8_t g = (uint8_t)((buf.g * imix_fp + main.g * mix_fp) >> 8);
          uint8_t b = (uint8_t)((buf.b * imix_fp + main.b * mix_fp) >> 8);
          uint8_t w = (uint8_t)((buf.w * imix_fp + main.w * mix_fp) >> 8);
          it[i] = Color(r, g, b, w);
        }
      }
      if ((i & 0x1F) == 0) esphome::App.feed_wdt(); // Every 32 pixels
    }

    // End transition when fully complete
    if (progress >= (1.0f + softness)) {
      act_->state = TRANSITION_NONE;
    }
  }

  int32_t leading_pixel = -1;
  int32_t total_pixels = 0;
  if (!act_->segment_runners.empty()) {
    leading_pixel = act_->segment_runners[0]->current_leading_pixel;
    total_pixels = act_->segment_runners[0]->_segment.length();
  } else if (act_->runner) {
    leading_pixel = act_->runner->current_leading_pixel;
    total_pixels = act_->runner->_segment.length();
  }

  if (leading_pixel >= 0 && total_pixels > 0) {
    float current_percentage = (float)leading_pixel / (float)total_pixels;
    if (leading_pixel != act_->last_leading_pixel) {
      // Iteration tracking: Detect cycle using percentage wrap (>0.8 to <0.2).
      // Only count on genuine new loops (erase→forward transition), not on
      // the forward→erase transition. is_return_phase_ is set by effects like
      // Color Wipe to distinguish the two wrap directions.
#ifdef USE_CFX_SEQUENCE
      bool _return_phase =
          act_->runner ? act_->runner->is_return_phase_ : false;
      if (act_->active_sequence != nullptr && act_->sequence_iterations > 0 &&
          !_return_phase) {
        if (act_->last_triggered_percentage > 0.8f &&
            current_percentage < 0.2f) {
          if (!act_->segment_runners.empty()) {
            act_->segment_runners[0]->iteration_count_++;
            if (act_->segment_runners[0]->iteration_count_ >=
                act_->sequence_iterations) {
              act_->segment_runners[0]->effect_complete_ = true;
            }
          } else if (act_->runner) {
            act_->runner->iteration_count_++;
            if (act_->runner->iteration_count_ >= act_->sequence_iterations) {
              act_->runner->effect_complete_ = true;
            }
          }
        }
      }
#endif

      act_->last_leading_pixel = leading_pixel;
      this->check_positional_triggers(leading_pixel, total_pixels);
    }
  }

  // (Duplicate completion handler removed — handled in service loop above)

  // CFX-032: Scrub OFF segments before DMA fires.
  // The effect writes into the full strip buffer without knowing which
  // segments are OFF. If it bleeds into an OFF segment's pixel range,
  // those pixels go live on the next schedule_show(). Fix: zero out every
  // OFF segment's range here, after the effect has painted but before DMA.
  // Only applies to non-virtual-segment effects (virtual segments each own
  // their own strip and have no siblings to protect).
  //
  // CFX-045 guard: do NOT scrub a segment that has an active CFX effect,
  // even if its light state briefly reads is_on()=false during a state sync.
  // IDLE segments (mono_idle) hold valid pixel data that must not be zeroed —
  // a false is_on() reading would permanently darken them since mono_dirty
  // would not be set to trigger a repaint.
  if (!this->is_virtual_segment_) {
    auto *cfx_out = static_cast<cfx_light::CFXLightOutput *>(
        this->get_light_state()->get_output());
    if (cfx_out != nullptr && cfx_out->has_segments()) {
      const auto &seg_defs = cfx_out->get_segment_defs();
      const auto &seg_states = cfx_out->get_segment_light_states();
      for (size_t si = 0; si < seg_defs.size() && si < seg_states.size();
           si++) {
        auto *seg_ls = seg_states[si];
        // Skip scrub if segment has an active effect — it owns its pixels.
        bool effect_active = (seg_ls->get_effect_name() != "None");
        if (!seg_ls->remote_values.is_on() && !effect_active) {
          const auto &def = seg_defs[si];
          for (int p = def.start; p < def.stop; p++) {
            it[p] = Color::BLACK;
          }
        }
        // CFX-043: Feed WDT while scrubbing large strips with many segments
        esphome::App.feed_wdt();
      }
    }
  }

  // CFX-033: Deferred diagnostics — flush pending heap queries AFTER all
  // runners finish but BEFORE DMA fires. Zero cost when debug is off.
  if (act_->runner)
    act_->runner->diagnostics.flush_log();
  for (auto *sr : act_->segment_runners)
    sr->diagnostics.flush_log();

  it.schedule_show();
  chimera_fx::instance = nullptr;
}

uint8_t CFXAddressableLightEffect::get_pal_idx(select::Select *s) {
  if (s == nullptr)
    return 0;

  // audit 2.2: current_option() returns const std::string& — call c_str()
  // directly instead of copying into a new std::string first.
  const char *option = s->current_option().c_str();
  if (option == nullptr)
    return 0;
  if (strcmp(option, "Aurora") == 0)
    return 1;
  if (strcmp(option, "Forest") == 0)
    return 2;
  if (strcmp(option, "Halloween") == 0)
    return 3;
  if (strcmp(option, "Rainbow") == 0)
    return 4;
  if (strcmp(option, "Fire") == 0)
    return 5;
  if (strcmp(option, "Sunset") == 0)
    return 6;
  if (strcmp(option, "Ice") == 0)
    return 7;
  if (strcmp(option, "Party") == 0)
    return 8;
  if (strcmp(option, "Pastel") == 0)
    return 10;
  if (strcmp(option, "Ocean") == 0)
    return 11;
  if (strcmp(option, "HeatColors") == 0)
    return 12;
  if (strcmp(option, "Sakura") == 0)
    return 13;
  if (strcmp(option, "Rivendell") == 0)
    return 14;
  if (strcmp(option, "Cyberpunk") == 0)
    return 15;
  if (strcmp(option, "OrangeTeal") == 0)
    return 16;
  if (strcmp(option, "Christmas") == 0)
    return 17;
  if (strcmp(option, "RedBlue") == 0)
    return 18;
  if (strcmp(option, "Matrix") == 0)
    return 19;
  if (strcmp(option, "SunnyGold") == 0)
    return 20;
  if (strcmp(option, "None") == 0 || strcmp(option, "Solid") == 0)
    return 255;
  if (strcmp(option, "Smart Random") == 0)
    return 254;
  if (strcmp(option, "Fairy") == 0)
    return 22;
  if (strcmp(option, "Twilight") == 0)
    return 9;

  if (strcmp(option, "Default") == 0) {
    // Resolve the natural default for this effect
    if (act_->runner) {
      uint8_t m = act_->runner->getMode();
      return this->get_default_palette_id_(m);
    }
    return 1; // Fallback to Aurora if no runner
  }

  return 0; // Unknown palette name
}

uint8_t CFXAddressableLightEffect::get_palette_index_() {
#ifdef USE_CFX_SEQUENCE
  if (act_->sequence_palette.has_value()) {
    return act_->sequence_palette.value();
  }
#endif

  select::Select *palette_sel = this->local_palette_();
  if (act_->controller != nullptr && act_->controller->get_palette()) {
    palette_sel = act_->controller->get_palette();
  }

  if (this->has_palette_preset_()) {
    return this->palette_preset_val_();
  } else if (palette_sel != nullptr) {
    return this->get_pal_idx(palette_sel);
  }
  // Default fallback if no UI and no preset
  return this->get_default_palette_id_(this->effect_id_);
}

uint8_t CFXAddressableLightEffect::get_default_palette_id_(uint8_t effect_id) {
  if (this->is_monochromatic_(effect_id)) {
    return 255; // Monochromatic series ALWAYS defaults to Solid
  }

  switch (effect_id) {
  case 0:       // Static
  case 1:       // Blink
  case 2:       // Breath
  case 3:       // Wipe
  case 4:       // Wipe Random
  case 6:       // Sweep
  case 15:      // Running Lights
  case 16:      // Saw
  case 18:      // Dissolve
  case 20:      // Sparkle
  case 21:      // Sparkle Dark
  case 22:      // Sparkle+
  case 23:      // Strobe
  case 24:      // Strobe Rainbow
  case 25:      // Multi Strobe
  case 26:      // Blink Rainbow
  case 28:      // Chase
  case 40:      // Scanner
  case 54:      // Chase Tri
  case 60:      // Scanner Dual
  case 68:      // BPM
  case 76:      // Meteor
  case 91:      // Bouncing Balls
  case 95:      // Popcorn
  case 96:      // Drip
  case 98:      // Percent
  case 100:     // Heartbeat
  case 152:     // Center Gauge
  case 154:     // Reactor Beat
  case 156:     // Follow Me
  case 157:     // Follow Us
  case 164:     // Collider
    return 255; // Defaults to Solid

  case 7:     // Dynamic
  case 8:     // Rainbow
  case 9:     // Colorloop (WLED Cycle)
  case 64:    // Juggle
  case 74:    // Color Twinkle
  case 79:    // Ripple
  case 87:    // Glitter
  case 90:    // Fireworks
  case 105:   // Phased
  case 107:   // Noise Pal
  case 110:   // Flow
  case 155:   // Kaleidos
    return 4; // Defaults to Rainbow

  case 63:    // Colorloop (ChimeraFX internal mapping)
  case 97:    // Plasma
    return 8; // Defaults to Party

  case 66:    // Fire
  case 53:    // Twin Flames
    return 5; // Defaults to Fire Palette

  case 101:    // Ocean (Pacifica)
  case 151:    // Dropping Time
  case 160:    // Fluid Rain
  case 169:    // Dropping Fill
    return 11; // Defaults to Ocean

  case 38:    // Aurora
    return 1; // Defaults to Aurora Palette

  case 104:    // Sunrise
    return 12; // Defaults to HeatColors

  case 52:     // Running Dual
    return 13; // Defaults to Sakura

  case 180:    // Interference
    return 15; // Defaults to Cyberpunk

  case 170:     // Assembly
  case 171:     // Inertia Sweep
  case 172:     // Sonar Reveal
  case 173:     // Venetian
  case 174:     // Crystallize
  case 175:     // Deep Breathe
    return 255; // Defaults to Solid

  default:
    return 1; // General fallback to Aurora
  }
}

bool CFXAddressableLightEffect::resolve_force_white_active_(bool requested,
                                                            uint8_t palette_id) const {
  if (!requested) {
    return false;
  }

  if (this->is_monochromatic_(this->effect_id_)) {
    return true;
  }

  return cfx::palette_supports_force_white(palette_id);
}

std::string CFXAddressableLightEffect::get_palette_name_(uint8_t pal_id) {
  switch (pal_id) {
  case 1:
    return "Aurora";
  case 2:
    return "Forest";
  case 3:
    return "Halloween";
  case 4:
    return "Rainbow";
  case 5:
    return "Fire";
  case 6:
    return "Sunset";
  case 7:
    return "Ice";
  case 8:
    return "Party";

  case 10:
    return "Pastel";
  case 11:
    return "Ocean";
  case 12:
    return "HeatColors";
  case 13:
    return "Sakura";
  case 14:
    return "Rivendell";
  case 15:
    return "Cyberpunk";
  case 16:
    return "OrangeTeal";
  case 17:
    return "Christmas";
  case 18:
    return "RedBlue";
  case 19:
    return "Matrix";
  case 20:
    return "SunnyGold";
  case 22:
    return "Fairy";
  case 9:
    return "Twilight";
  case 23:
    return "Default";
  case 254:
    return "Smart Random";
  case 255:
    return "Solid";
  default:
    return "Default";
  }
}

std::string CFXAddressableLightEffect::get_intro_name_(uint8_t intro_id) {
  switch (intro_id) {
  case INTRO_MODE_NONE: return "None";
  case INTRO_MODE_CENTER: return "Center";
  case INTRO_MODE_ASSEMBLY: return "Construct";
  case INTRO_MODE_CRYSTALLIZE: return "Crystallize";
  case INTRO_MODE_DEEP_BREATHE: return "Deep Breathe";
  case INTRO_MODE_DROPPING: return "Dropping";
  case INTRO_MODE_ECLIPSE: return "Eclipse";
  case INTRO_MODE_FADE: return "Fade";
  case INTRO_MODE_GAS_DISCHARGE: return "Gas Discharge";
  case INTRO_MODE_GLITTER: return "Glitter";
  case INTRO_MODE_HARMONIC_SETTLE: return "Harmonic Settle";
  case INTRO_MODE_IMPACT_FLARE: return "Impact Flare";
  case INTRO_MODE_INERTIA_SWEEP: return "Inertia Sweep";
  case INTRO_MODE_INTERFERENCE: return "Interference";
  case INTRO_MODE_LITHOGRAPH: return "Lithograph";
  case INTRO_MODE_MOIRE_SHIFT: return "Moiré Shift";
  case INTRO_MODE_MORSE: return "Morse Code";
  case INTRO_MODE_HYDRAULICS: return "Pressurize";
  case INTRO_MODE_QUADRANT: return "Quadrant";
  case INTRO_MODE_RESONANCE_FILL: return "Resonance";
  case INTRO_MODE_SONAR_REVEAL: return "Sonar Reveal";
  case INTRO_MODE_STELLAR_DUST: return "Stellar Dust";
  case INTRO_MODE_TELEMETRY: return "Telemetry";
  case INTRO_MODE_TIDAL_SURGE: return "Tidal Surge";
  case INTRO_MODE_TWIN_PULSE: return "Twin Pulse";
  case INTRO_MODE_VENETIAN: return "Venetian";
  case INTRO_MODE_WIPE: return "Wipe";
  default: return "None";
  }
}

std::string CFXAddressableLightEffect::get_outro_name_(uint8_t outro_id) {
  switch (outro_id) {
  case INTRO_MODE_NONE: return "None";
  case INTRO_MODE_CENTER: return "Center";
  case OUTRO_MODE_CENTER_SQUEEZE: return "Center Squeeze";
  case INTRO_MODE_VENETIAN: return "Close Blinds";
  case INTRO_MODE_INERTIA_SWEEP: return "Decelerate";
  case INTRO_MODE_ASSEMBLY: return "Dismantle";
  case INTRO_MODE_HYDRAULICS: return "Drain";
  case INTRO_MODE_ECLIPSE: return "Eclipse";
  case INTRO_MODE_DROPPING: return "Emptying";
  case INTRO_MODE_CRYSTALLIZE: return "Erode";
  case INTRO_MODE_DEEP_BREATHE: return "Exhale";
  case INTRO_MODE_FADE: return "Fade";
  case INTRO_MODE_GAS_DISCHARGE: return "Gas Discharge";
  case INTRO_MODE_GLITTER: return "Glitter";
  case INTRO_MODE_HARMONIC_SETTLE: return "Harmonic Settle";
  case INTRO_MODE_INTERFERENCE: return "Interference Fade";
  case INTRO_MODE_LITHOGRAPH: return "Lithograph";
  case INTRO_MODE_MOIRE_SHIFT: return "Moiré Fade";
  case INTRO_MODE_MORSE: return "Morse Code";
  case INTRO_MODE_QUADRANT: return "Quadrant";
  case INTRO_MODE_RESONANCE_FILL: return "Resonance Fade";
  case INTRO_MODE_SONAR_REVEAL: return "Sonar Fade";
  case INTRO_MODE_STELLAR_DUST: return "Stellar Fade";
  case INTRO_MODE_TELEMETRY: return "Telemetry Fade";
  case INTRO_MODE_TIDAL_SURGE: return "Tidal Recede";
  case INTRO_MODE_TWIN_PULSE: return "Twin Pulse";
  case INTRO_MODE_WIPE: return "Wipe";
  default: return "None";
  }
}

uint32_t
CFXAddressableLightEffect::get_intro_mode_min_duration_ms_(uint8_t intro_mode) const {
  switch (intro_mode) {
  case INTRO_MODE_SONAR_REVEAL:
    return 2000;
  case INTRO_MODE_VENETIAN:
    return 1200;
  case INTRO_MODE_CRYSTALLIZE:
    return 1500;
  case INTRO_MODE_DEEP_BREATHE:
    return 1800;
  case INTRO_MODE_MOIRE_SHIFT:
    return 1200;
  case INTRO_MODE_RESONANCE_FILL:
    return 1400;
  case INTRO_MODE_TELEMETRY:
    return 1200;
  case INTRO_MODE_STELLAR_DUST:
    return 2000;
  case INTRO_MODE_INTERFERENCE:
    return 1500;
  case INTRO_MODE_ECLIPSE:
    return 1500;
  case INTRO_MODE_GAS_DISCHARGE:
    return 2200;
  case INTRO_MODE_HARMONIC_SETTLE:
    return 1600;
  case INTRO_MODE_LITHOGRAPH:
    return 1100;
  default:
    return 0;
  }
}

uint32_t
CFXAddressableLightEffect::get_outro_mode_min_duration_ms_(uint8_t outro_mode) const {
  switch (outro_mode) {
  case INTRO_MODE_MOIRE_SHIFT:
    return 1200;
  case INTRO_MODE_INTERFERENCE:
    return 1500;
  case INTRO_MODE_ECLIPSE:
    return 1500;
  case INTRO_MODE_GAS_DISCHARGE:
    return 1800;
  case INTRO_MODE_HARMONIC_SETTLE:
    return 1600;
  case INTRO_MODE_LITHOGRAPH:
    return 1100;
  default:
    return this->get_intro_mode_min_duration_ms_(outro_mode);
  }
}

uint8_t CFXAddressableLightEffect::get_default_speed_(uint8_t effect_id) {
#ifdef USE_CFX_SEQUENCE
  if (act_->sequence_speed.has_value()) {
    return act_->sequence_speed.value();
  }
#endif

  // Per-effect speed defaults from effects_preset.md
  switch (effect_id) {
  case 38:
    return 24; // Aurora
  case 28:
    return 110; // Chase
  case 54:
    return 60; // Chase Multi
  case 153:
    return 64; // Twin Flames (same as Fire)
  case 64:
    return 64; // Juggle
  case 66:
    return 64; // Fire
  case 68:
    return 64; // BPM
  case 104:
    return 60; // Sunrise
  case 151:
    return 15; // Dropping Time
  case 155:
    return 60; // Kaleidos
  case 156:
    return 140; // Follow Me (Default Speed)
  case 157:
    return 128; // Follow Us (Default Speed 128)
  case 163:
    return 1; // Monochromatic series (fastest speed)
  case 168:
    return 128; // Aqueous Flow (Default Speed)
  case 169:
  case 171:
  case 172:
  case 173:
  case 174:
  case 175:
    return 1; // Monochromatic effects (Speed slider controls duration)
  case 164:
    return 100; // Collider (Default Speed)
  case 180:
    return 160; // Interference (Default Speed)
  case 189:
    return 160; // Standing Wave (same as Interference intro/outro)
  default:
    return 128; // WLED default
  }
}

uint8_t CFXAddressableLightEffect::get_default_intensity_(uint8_t effect_id) {
#ifdef USE_CFX_SEQUENCE
  if (act_->sequence_intensity.has_value()) {
    return act_->sequence_intensity.value();
  }
#endif

  // Per-effect intensity defaults from effects_preset.md
  switch (effect_id) {
  case 28:
    return 40; // Chase
  case 54:
    return 70; // Chase Multi
  case 153:
    return 160; // Twin Flames(same as Fire)
  case 66:
    return 160; // Fire
  case 155:
    return 150; // Kaleidos
  case 156:
    return 40; // Follow Me (Default Intensity)
  case 157:
    return 128; // Follow Us (Default Intensity 128)
  case 163:
    return 1; // Monochromatic series (No blur)
  case 168:
    return 128; // Aqueous Flow (Default Viscosity)
  case 169:
  case 171:
  case 172:
  case 173:
  case 174:
  case 175:
    return 1; // Monochromatic effects (No blur)
  case 164:
    return 170; // Collider (Default Intensity)
  case 180:
    return 180; // Interference (Default Intensity)
  case 189:
    return 180; // Standing Wave (same as Interference intro/outro)
  default:
    return 128; // WLED default
  }
}

void CFXAddressableLightEffect::run_controls_() {
  // 1. Find controller if not linked (with throttle to prevent log flooding)
  if (act_->controller == nullptr) {
    static uint64_t last_ctrl_check = 0;
    uint64_t now = millis_64();
    if (now - last_ctrl_check > 5000) {
      last_ctrl_check = now;
      act_->controller = CFXControl::find(this->get_light_state());
      ESP_LOGD("chimera_fx",
               "CFXAddressableLightEffect: Finding controller for light %p. "
               "Found: %p",
               this->get_light_state(), act_->controller);
    }
  }

  // 2. Register ALL runners with the controller (segment runners or single
  // runner)
  if (act_->controller) {
    if (!act_->segment_runners.empty()) {
      for (auto *r : act_->segment_runners) {
        act_->controller->register_runner(r);
      }
    } else if (act_->runner) {
      act_->controller->register_runner(act_->runner);
    }
  }

  CFXControl *c = act_->controller;

  // QoL FIX: Live force-white sync — re-read the switch every frame so
  // toggling it mid-effect takes effect immediately (not just at start()).
  bool force_white_requested =
      this->has_force_white_preset_()
          ? this->force_white_preset_val_()
          : (c != nullptr && c->get_force_white() != nullptr &&
             c->get_force_white()->state);

  select::Select *palette_sel =
      (c && c->get_palette()) ? c->get_palette() : this->local_palette_();

  // --- Autotune Auto-Disable State Machine ---
  bool current_autotune_state =
      true; // Constraint: No switch = always respect defaults
  switch_::Switch *autotune_sw =
      (c && c->get_autotune()) ? c->get_autotune() : this->local_autotune_();
  if (act_->sequence_autotune.has_value()) {
    current_autotune_state = act_->sequence_autotune.value();
  } else if (autotune_sw != nullptr) {
    current_autotune_state = autotune_sw->state;
  }

  // Handle manual OFF -> ON transition
  if (current_autotune_state && !act_->autotune_active) {
    this->apply_autotune_defaults_();
    act_->autotune_active = true;
  }
  // Handle manual ON -> OFF transition
  else if (!current_autotune_state && act_->autotune_active) {
    act_->autotune_active = false;
  }
  // Handle expected ON state, but detecting manual UI overrides
  else if (current_autotune_state && act_->autotune_active &&
           !act_->sequence_autotune.has_value() &&
           autotune_sw != nullptr) {
    bool manual_override = false;
    // In 1:1 mode, this segment IS always the target.
    bool is_currently_target = true;

    number::Number *speed_num =
        (c && c->get_speed()) ? c->get_speed() : this->local_speed_();
    if (speed_num && speed_num->state != act_->autotune_expected_speed)
      manual_override = true;

    number::Number *intensity_num = (c && c->get_intensity())
                                        ? c->get_intensity()
                                        : this->local_intensity_();
    if (intensity_num &&
        intensity_num->state != act_->autotune_expected_intensity)
      manual_override = true;

    if (is_currently_target && palette_sel && palette_sel->has_state() &&
        palette_sel->current_option() != act_->autotune_expected_palette)
      manual_override = true;

    if (manual_override) {
      autotune_sw->turn_off();
      act_->autotune_active = false;
    }
  }

  // --- Visualizer: Dynamic Palette Sync ---
  if (!this->is_virtual_segment_ && palette_sel && palette_sel->has_state()) {
    // audit 2.2: c_str() directly on the reference — no std::string copy
    const char *opt = palette_sel->current_option().c_str();
    std::string current_pal = opt ? opt : "";
    if (!current_pal.empty() && current_pal != act_->last_sent_palette) {
      auto *out = static_cast<cfx_light::CFXLightOutput *>(
          this->get_light_state()->get_output());
      if (out != nullptr) {
        out->send_visualizer_metadata(this->get_name(), current_pal);
      }
      act_->last_sent_palette = current_pal;
    }
  }

  // --- Visualizer: Periodic Metadata Refresh (Every 5s) ---
  if (!this->is_virtual_segment_) {
    uint64_t now = millis_64();
    if (now - act_->last_metadata_refresh > 5000) {
      auto *out = static_cast<cfx_light::CFXLightOutput *>(
          this->get_light_state()->get_output());
      if (out != nullptr) {
        std::string pal_name = "";
        if (palette_sel && palette_sel->has_state()) {
          // audit 2.2: c_str() directly on the reference — no std::string copy
          const char *opt = palette_sel->current_option().c_str();
          if (opt != nullptr)
            pal_name = opt;
        }

        // Deep Palette Resolution: If UI says "Default", ask the runner what's
        // actually rendering
        if ((pal_name.empty() || pal_name == "Default") && act_->runner) {
          pal_name = this->get_palette_name_(act_->runner->getPalette());
        }

        out->send_visualizer_metadata(this->get_name(), pal_name);
      }
      act_->last_metadata_refresh = now;
    }
  } // End of Visualizer block

  if (act_->runner) {
    // Helper lambda for Palette Index Lookup
    // New indices: 0=Default, 1=Aurora, 2=Forest, 3=Ocean, 4=Rainbow, etc.
    auto get_pal_idx = [this](select::Select *sel) -> uint8_t {
      if (!sel || !sel->has_state())
        return 0;
      // audit 2.2: avoid copying current_option() into a new std::string
      const char *opt = sel->current_option().c_str();
      if (!opt)
        return 0;
      if (strcmp(opt, "Aurora") == 0)
        return 1;
      if (strcmp(opt, "Forest") == 0)
        return 2;
      if (strcmp(opt, "Halloween") == 0)
        return 3;
      if (strcmp(opt, "Rainbow") == 0)
        return 4;
      if (strcmp(opt, "Fire") == 0)
        return 5;
      if (strcmp(opt, "Sunset") == 0)
        return 6;
      if (strcmp(opt, "Ice") == 0)
        return 7;
      if (strcmp(opt, "Party") == 0)
        return 8;
      if (strcmp(opt, "Twilight") == 0)
        return 9;
      if (strcmp(opt, "Pastel") == 0)
        return 10;
      if (strcmp(opt, "Ocean") == 0)
        return 11;
      if (strcmp(opt, "HeatColors") == 0)
        return 12;
      if (strcmp(opt, "Sakura") == 0)
        return 13;
      if (strcmp(opt, "Rivendell") == 0)
        return 14;
      if (strcmp(opt, "Cyberpunk") == 0)
        return 15;
      if (strcmp(opt, "OrangeTeal") == 0)
        return 16;
      if (strcmp(opt, "Christmas") == 0)
        return 17;
      if (strcmp(opt, "RedBlue") == 0)
        return 18;
      if (strcmp(opt, "Matrix") == 0)
        return 19;
      if (strcmp(opt, "SunnyGold") == 0)
        return 20;
      if (strcmp(opt, "None") == 0 || strcmp(opt, "Solid") == 0)
        return 255;
      if (strcmp(opt, "Smart Random") == 0)
        return 254;
      if (strcmp(opt, "Fairy") == 0)
        return 22;
      if (strcmp(opt, "Default") == 0) {
        // Resolve the natural default for this effect
        // FIX: Use this->effect_id_ (Requested Effect) instead of
        // runner_->getMode() (Current/Old Effect) This ensures that when
        // switching effects, we get the default palette of the NEW effect.
        return this->get_default_palette_id_(this->effect_id_);
      }
      return 0; // Unknown palette name
    };
    // Speed/Intensity/Palette/Mirror PULL — only when NO controller exists.
    // When a controller IS present, these are managed by PUSH callbacks
    // in cfx_control.h which respect the Target Segment filter.
    if (!c) {
      // 2. Speed (standalone mode) — sequence value takes priority over UI entity
      const bool transient_autotune_context = current_autotune_state &&
#ifdef USE_CFX_SEQUENCE
          (act_->active_sequence != nullptr || act_->sequence_autotune.has_value());
#else
          false;
#endif
      uint8_t current_speed = this->get_default_speed_(this->effect_id_);
#ifdef USE_CFX_SEQUENCE
      if (act_->sequence_speed.has_value())
        current_speed = act_->sequence_speed.value();
      else
#endif
      if (!transient_autotune_context && this->local_speed_()) {
        current_speed = (uint8_t)this->local_speed_()->state;
      }

      // 3. Intensity (standalone mode) — sequence value takes priority
      uint8_t current_intensity =
          this->get_default_intensity_(this->effect_id_);
#ifdef USE_CFX_SEQUENCE
      if (act_->sequence_intensity.has_value())
        current_intensity = act_->sequence_intensity.value();
      else
#endif
      if (!transient_autotune_context && this->local_intensity_()) {
        current_intensity = (uint8_t)this->local_intensity_()->state;
      }

      // 4. Palette (standalone mode) — sequence value takes priority
      uint8_t current_palette = this->get_default_palette_id_(this->effect_id_);
      if (this->is_monochromatic_(this->effect_id_)) {
        current_palette = 255;
      } else {
#ifdef USE_CFX_SEQUENCE
        if (act_->sequence_palette.has_value())
          current_palette = act_->sequence_palette.value();
        else
#endif
        if (!transient_autotune_context && this->local_palette_()) {
          current_palette = get_pal_idx(this->local_palette_());
        }
      }

      // 5. Mirror (standalone mode) — sequence override takes priority
      bool current_mirror = false;
#ifdef USE_CFX_SEQUENCE
      if (act_->sequence_mirror.has_value()) {
        current_mirror = act_->sequence_mirror.value();
      } else if (this->has_mirror_preset_()) {
#else
      if (this->has_mirror_preset_()) {
#endif
        current_mirror = this->mirror_preset_val_();
      } else if (this->local_mirror_()) {
        current_mirror = this->local_mirror_()->state;
      }

      // Apply to ALL physical segment runners. act_->runner ==
      // segment_runners[0], so iterating only act_->runner silently drops
      // segments 1..N. segment_runners is ONLY populated for non-virtual master
      // effects, so virtual segment effects (empty segment_runners) are
      // unaffected.
      if (!act_->segment_runners.empty()) {
        for (auto *r : act_->segment_runners) {
          r->setSpeed(current_speed);
          r->setIntensity(current_intensity);
          r->setPalette(current_palette);
          r->setMirror(current_mirror);
        }
      } else {
        act_->runner->setSpeed(current_speed);
        act_->runner->setIntensity(current_intensity);
        act_->runner->setPalette(current_palette);
        act_->runner->setMirror(current_mirror);
      }
    } else {
      // Controller present: sequence/cfx_set params take priority over UI
      // sliders. sequence_speed_/intensity_/palette_ are set by cfx_sequence or
      // cfx_set and override the controller number entities for the duration of
      // the run.
      const bool transient_autotune_context = current_autotune_state &&
#ifdef USE_CFX_SEQUENCE
          (act_->active_sequence != nullptr || act_->sequence_autotune.has_value());
#else
          false;
#endif
      uint8_t current_speed = this->get_default_speed_(this->effect_id_);
      bool has_seq_speed = false;
      bool has_seq_intensity = false;
      bool has_seq_palette = false;
#ifdef USE_CFX_SEQUENCE
      has_seq_speed = act_->sequence_speed.has_value();
      has_seq_intensity = act_->sequence_intensity.has_value();
      has_seq_palette = act_->sequence_palette.has_value();
      if (has_seq_speed)
        current_speed = act_->sequence_speed.value();
      else if (!transient_autotune_context)
#endif
          if (c->get_speed())
        current_speed = (uint8_t)c->get_speed()->state;

      uint8_t current_intensity = this->get_default_intensity_(this->effect_id_);
#ifdef USE_CFX_SEQUENCE
      if (has_seq_intensity)
        current_intensity = act_->sequence_intensity.value();
      else if (!transient_autotune_context)
#endif
          if (c->get_intensity())
        current_intensity = (uint8_t)c->get_intensity()->state;

      uint8_t current_palette = this->get_default_palette_id_(this->effect_id_);
      if (this->is_monochromatic_(this->effect_id_)) {
        current_palette = 255;
      }
#ifdef USE_CFX_SEQUENCE
      else if (has_seq_palette)
        current_palette = act_->sequence_palette.value();
#endif
      else if (!transient_autotune_context && c->get_palette())
        current_palette = get_pal_idx(c->get_palette());

      bool current_mirror = false;
      // CFX-056: Sequence mirror override takes priority over controller's UI
      // switch, matching the priority chain used for speed/intensity/palette.
#ifdef USE_CFX_SEQUENCE
      if (act_->sequence_mirror.has_value()) {
        current_mirror = act_->sequence_mirror.value();
      } else if (this->has_mirror_preset_()) {
#else
      if (this->has_mirror_preset_()) {
#endif
        current_mirror = this->mirror_preset_val_();
      } else if (c->get_mirror()) {
        current_mirror = c->get_mirror()->state;
      }

      bool sequence_override_active =
          (has_seq_speed || has_seq_intensity || has_seq_palette ||
           transient_autotune_context);

      // Apply UI/sequence overrides to ALL physical segment runners.
      // segment_runners is only non-empty for master (non-virtual) effects, so
      // virtual segments (single runner, empty segment_runners) use the else
      // branch.
      if (!act_->segment_runners.empty()) {
        auto *cfx_out = static_cast<cfx_light::CFXLightOutput *>(
            this->get_light_state()->get_output());

        for (size_t i = 0; i < act_->segment_runners.size(); i++) {
          auto *r = act_->segment_runners[i];

          uint8_t r_speed = current_speed; // from sequence or master
          uint8_t r_intensity = current_intensity;
          uint8_t r_palette = current_palette;
          bool r_mirror = current_mirror;

          // If no sequence override is forcing global synchronization,
          // prioritize the segment's independent UI!
          if (!sequence_override_active) {
            if (i < cfx_out->get_segment_light_states().size()) {
              auto *seg_state = cfx_out->get_segment_light_states()[i];
              CFXControl *seg_c = CFXControl::find(seg_state);
              if (seg_c) {
                if (seg_c->get_speed() && seg_c->get_speed()->has_state())
                  r_speed = (uint8_t)seg_c->get_speed()->state;

                if (seg_c->get_intensity() &&
                    seg_c->get_intensity()->has_state())
                  r_intensity = (uint8_t)seg_c->get_intensity()->state;

                if (seg_c->get_mirror() && seg_c->get_mirror()->has_state())
                  r_mirror = seg_c->get_mirror()->state;

                if (seg_c->get_palette() && seg_c->get_palette()->has_state()) {
                  // audit 2.2: check non-empty via c_str() directly —
                  // no intermediate std::string copy needed.
                  const char *opt_ptr =
                      seg_c->get_palette()->current_option().c_str();
                  if (opt_ptr && opt_ptr[0] != '\0') {
                    r_palette = get_pal_idx(seg_c->get_palette());
                  }
                }

                if (seg_c->get_light() != seg_state) {
                  ESP_LOGW("chimera_fx",
                           "Segment %zu: FALLBACK! seg_c is %p (Target %p), "
                           "but expected %p",
                           i, seg_c, seg_c->get_light(), seg_state);
                }
              }
            }
          }

          r->setSpeed(r_speed);
          r->setIntensity(r_intensity);
          r->setPalette(r_palette);
          r->setMirror(r_mirror);
        }
      } else {
        act_->runner->setSpeed(current_speed);
        act_->runner->setIntensity(current_intensity);
        act_->runner->setPalette(current_palette);
        act_->runner->setMirror(current_mirror);
      }
    }

    uint8_t force_white_palette =
        act_->runner != nullptr ? act_->runner->getPalette()
                                : this->get_palette_index_();
    bool force_white_active =
        this->resolve_force_white_active_(force_white_requested,
                                          force_white_palette);
    if (!act_->segment_runners.empty()) {
      for (auto *r : act_->segment_runners) {
        r->force_white_active_ = force_white_active;
      }
    } else if (act_->runner) {
      act_->runner->force_white_active_ = force_white_active;
    }
    act_->active_force_white = force_white_active;

    // 7. Debug
    if (c && c->get_debug()) {
      ensure_cfg_();
      cfg_->debug_switch = c->get_debug();
    }
  }
}

// Intro Routine Implementation
void CFXAddressableLightEffect::run_intro(light::AddressableLight &it,
                                          const Color &target_color) {
  uint32_t elapsed = (uint32_t)(millis_64() - act_->intro_start_time);

  // Safety: If mode is NONE, abort immediately and release control
  // Ensure we clear the flag so next frame service() runs.
  if (act_->active_intro_mode == INTRO_NONE) {
    act_->intro_active = false;
    return;
  }

  uint32_t duration = 1000; // Initial Default: 1.0s
  number::Number *dur_num = this->local_inout_duration_();
  if (dur_num == nullptr && act_->controller != nullptr)
    dur_num = act_->controller->get_intro_duration();

  MonochromaticPreset preset =
      this->get_monochromatic_preset_(this->effect_id_);

  if (auto duration_override =
          this->resolve_inout_duration_override_ms_(dur_num);
      duration_override.has_value()) {
    duration = duration_override.value();
  } else if (preset.is_active) {
    // Monochromatic Preset Fallback: Speed Slider
    number::Number *speed_num = this->local_speed_();
    if (speed_num == nullptr && act_->controller != nullptr)
      speed_num = act_->controller->get_speed();

    if (speed_num != nullptr && speed_num->has_state()) {
      // Map Speed (0-255) to Duration (500ms up to 10000ms)
      float speed_val = speed_num->state;
      duration = (uint32_t)(500.0f + (speed_val / 255.0f * 9500.0f));
    }
  }

  // Morse Code Timing Override
  if (act_->active_intro_mode == INTRO_MODE_MORSE) {
    uint32_t speed_val = act_->active_intro_speed;
    uint32_t unit_ms = 80 + ((255 - speed_val) * 100 / 255);
    duration = 19 * unit_ms;
  }

  if (duration == 0)
    duration = 1; // Prevent div by zero

  float progress = (float)elapsed / (float)duration;
  if (progress > 1.0f)
    progress = 1.0f;

  // 2. Determine Mode
  // Use the pre-resolved mode from start() to avoid async issues
  uint8_t mode = act_->active_intro_mode;

  // 3. Setup Color/Palette
  // BUG 11 FIX: Use Current Color (current_values) for smooth fade-in
  Color c = target_color;
  auto *state = this->get_light_state();
  if (state) {
    auto v = state->current_values;
    c = Color((uint8_t)(v.get_red() * 255), (uint8_t)(v.get_green() * 255),
              (uint8_t)(v.get_blue() * 255), (uint8_t)(v.get_white() * 255));
  }
  if (c.r == 0 && c.g == 0 && c.b == 0 && c.w == 0) {
    c = Color::WHITE;
  }

  // Define helper lambdas at function scope to avoid redeclaration issues and
  // fix missing symbol errors in newer cases like Interference.
  auto dim = [](Color col, uint8_t f) -> Color {
    return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                 (uint8_t)(((uint16_t)col.g * f) >> 8),
                 (uint8_t)(((uint16_t)col.b * f) >> 8),
                 (uint8_t)(((uint16_t)col.w * f) >> 8));
  };
  auto boost = [](Color col, uint8_t b) -> Color {
    return Color((uint8_t)((int)col.r + b > 255 ? 255 : col.r + b),
                 (uint8_t)((int)col.g + b > 255 ? 255 : col.g + b),
                 (uint8_t)((int)col.b + b > 255 ? 255 : col.b + b),
                 (uint8_t)((int)col.w + b > 255 ? 255 : col.w + b));
  };

  // NOTE: Brightness is handled by global_brightness_ on the runner.
  // Do NOT apply user_brightness here — it would cause double-application.

  // BUG 13 FIX: Apply force_white to Intro transitions
  bool force_white_requested =
      this->has_force_white_preset_()
          ? this->force_white_preset_val_()
          : (act_->controller != nullptr &&
             act_->controller->get_force_white() != nullptr &&
             act_->controller->get_force_white()->state);
  uint8_t force_white_palette =
      act_->runner != nullptr ? act_->runner->getPalette()
                              : this->get_palette_index_();
  bool force_white_active =
      this->resolve_force_white_active_(force_white_requested,
                                        force_white_palette);
  if (force_white_active)
    cfx::apply_force_white(c.r, c.g, c.b, c.w);

  // Check for Palette usage
  bool use_palette = false;
  uint8_t pal = 0;

  // CFX-026: Always derive intro color from the active effect palette.
  // Compute dominant color = average of 16 evenly-spaced palette samples.
  // For solid palettes (255) or no runner, fall back to the solid color c.
  if (chimera_fx::instance != nullptr) {
    pal = chimera_fx::instance->_segment.palette;
    if (pal == 0)
      pal = this->get_default_palette_id_(chimera_fx::instance->getMode());
    if (pal != 255) {
      use_palette = true;
    }
  }

  if (use_palette && chimera_fx::instance != nullptr) {
    // Force update the runner's palette immediately
    chimera_fx::instance->_segment.palette = pal;

    // CFX-026: Derive intro dominant color per palette.
    // Hand-tuned representative colors — easy to adjust per palette.
    // Replaces raw per-pixel palette mapping which produced stripes.
    // Falls back to averaged math for unknown/future palette IDs.
    uint8_t dr = 128, dg = 128, db = 128; // default: mid-grey fallback
    switch (pal) {
    case 1:
      dr = 0;
      dg = 180;
      db = 200;
      break; // Aurora      — teal-blue
    case 2:
      dr = 20;
      dg = 120;
      db = 20;
      break; // Forest      — mid green
    case 3:
      dr = 200;
      dg = 60;
      db = 0;
      break; // Halloween   — deep orange
    case 4:
      dr = 160;
      dg = 0;
      db = 160;
      break; // Rainbow     — violet (midpoint)
    case 5:
      dr = 220;
      dg = 60;
      db = 0;
      break; // Fire        — orange-red
    case 6:
      dr = 220;
      dg = 100;
      db = 20;
      break; // Sunset      — warm orange
    case 7:
      dr = 0;
      dg = 180;
      db = 220;
      break; // Ice         — cyan-blue
    case 8:
      dr = 200;
      dg = 0;
      db = 200;
      break; // Party       — magenta
    case 9:
      dr = 220;
      dg = 20;
      db = 0;
      break; // Lava        — deep red
    case 10:
      dr = 200;
      dg = 160;
      db = 180;
      break; // Pastel      — soft pink
    case 11:
      dr = 0;
      dg = 80;
      db = 200;
      break; // Ocean       — deep blue
    case 12:
      dr = 220;
      dg = 80;
      db = 0;
      break; // HeatColors  — orange
    case 13:
      dr = 220;
      dg = 100;
      db = 140;
      break; // Sakura      — cherry pink
    case 14:
      dr = 60;
      dg = 120;
      db = 80;
      break; // Rivendell   — muted green
    case 15:
      dr = 180;
      dg = 0;
      db = 220;
      break; // Cyberpunk   — neon purple
    case 16:
      dr = 0;
      dg = 180;
      db = 160;
      break; // OrangeTeal  — teal (cooler)
    case 17:
      dr = 180;
      dg = 20;
      db = 20;
      break; // Christmas   — red
    case 18:
      dr = 20;
      dg = 20;
      db = 200;
      break; // RedBlue     — blue (cool side)
    case 19:
      dr = 0;
      dg = 200;
      db = 0;
      break; // Matrix      — green
    case 20:
      dr = 220;
      dg = 180;
      db = 0;
      break; // SunnyGold   — golden yellow
    case 22:
      dr = 180;
      dg = 100;
      db = 220;
      break; // Fairy       — lavender
    case 23:
      dr = 60;
      dg = 20;
      db = 140;
      break; // Twilight    — deep indigo
    case 254: {
      // Smart Random — compute average from current random palette
      uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
      for (uint8_t s = 0; s < 16; s++) {
        uint8_t idx = s * 16;
        uint32_t cp = chimera_fx::instance->_segment.color_from_palette(
            idx, false, true, 255, 255);
        r_sum += (cp >> 16) & 0xFF;
        g_sum += (cp >> 8) & 0xFF;
        b_sum += (cp) & 0xFF;
      }
      dr = (uint8_t)(r_sum / 16);
      dg = (uint8_t)(g_sum / 16);
      db = (uint8_t)(b_sum / 16);
      break;
    }
    default: {
      // Unknown palette — fall back to average
      uint32_t r_sum = 0, g_sum = 0, b_sum = 0;
      for (uint8_t s = 0; s < 16; s++) {
        uint8_t idx = s * 16;
        uint32_t cp = chimera_fx::instance->_segment.color_from_palette(
            idx, false, true, 255, 255);
        r_sum += (cp >> 16) & 0xFF;
        g_sum += (cp >> 8) & 0xFF;
        b_sum += (cp) & 0xFF;
      }
      dr = (uint8_t)(r_sum / 16);
      dg = (uint8_t)(g_sum / 16);
      db = (uint8_t)(b_sum / 16);
      break;
    }
    }
    c = Color(dr, dg, db, 0);
    use_palette = false; // render as solid from here — no per-pixel mapping
  }

  // Segment Aware Bounds
  int seg_len = it.size();
  int seg_start = 0;
  int seg_stop = seg_len;
  if (chimera_fx::instance != nullptr) {
    chimera_fx::instance->now = cfx_millis();
    seg_len = chimera_fx::instance->_segment.length();
    seg_start =
        (it.size() == seg_len) ? 0 : chimera_fx::instance->_segment.start;
    seg_stop = seg_start + seg_len;
  }

  // Control State
  bool reverse = false;
#ifdef USE_CFX_SEQUENCE
  if (act_->sequence_mirror.has_value()) {
    reverse = act_->sequence_mirror.value();
  } else {
#endif
    switch_::Switch *mirror_sw = this->local_mirror_();
    if (mirror_sw == nullptr && act_->controller != nullptr)
      mirror_sw = act_->controller->get_mirror();
    if (mirror_sw != nullptr && mirror_sw->state)
      reverse = true;
#ifdef USE_CFX_SEQUENCE
  }
#endif

  bool symmetry = false;
  bool quadrant = false;
  if (mode == INTRO_MODE_CENTER) {
    symmetry = true;
    mode = INTRO_MODE_WIPE; // Use Wipe logic with symmetry
  } else if (mode == INTRO_MODE_QUADRANT) {
    quadrant = true;
    mode = INTRO_MODE_WIPE; // Use Wipe logic with quadrant tiling
  }

  switch (mode) {
  case INTRO_MODE_WIPE: {
    int logical_len = seg_len;
    if (symmetry)
      logical_len = seg_len / 2;
    if (quadrant)
      logical_len = (seg_len + 3) / 4;

    // Intensity defines blur radius
    float blur_percent = 0.0f;
    number::Number *intensity_num = this->local_intensity_();
    if (intensity_num == nullptr && act_->controller != nullptr) {
      intensity_num = act_->controller->get_intensity();
    }
    if (intensity_num != nullptr && intensity_num->has_state()) {
      blur_percent = (intensity_num->state / 255.0f) * 0.5f;
    }

    int blur_radius = (int)(logical_len * blur_percent);
    float exact_lead = progress * (logical_len + blur_radius);
    int lead = (int)exact_lead;

    // Quadrant logic: we split the strip into 4 "wings"
    // Each pair of wings (quadrant) works like a Curtain Sweep
    // Pair 1: Center at 25% (range 0% to 50%)
    // Pair 2: Center at 75% (range 50% to 100%)
    int q_len = seg_len / 4;
    int q1_center = q_len;
    int q2_center = q_len * 3;

    for (int i = 0; i < logical_len; i++) {

      float alpha =
          0.0f; // 0.0 means ERASED (black), 1.0 means KEPT (background)

      if (!reverse) {
        if (i <= lead - blur_radius) {
          alpha = 1.0f;
        } else if (i <= lead && blur_radius > 0) {
          float distance_into_blur = exact_lead - i;
          alpha = distance_into_blur / blur_radius;
          if (alpha < 0.0f)
            alpha = 0.0f;
          if (alpha > 1.0f)
            alpha = 1.0f;
        }
      } else {
        int rev_i = logical_len - 1 - i;
        if (rev_i <= lead - blur_radius) {
          alpha = 1.0f;
        } else if (rev_i <= lead && blur_radius > 0) {
          float distance_into_blur = exact_lead - rev_i;
          alpha = distance_into_blur / blur_radius;
          if (alpha < 0.0f)
            alpha = 0.0f;
          if (alpha > 1.0f)
            alpha = 1.0f;
        }
      }

      Color pixel_c1 = Color::BLACK;
      Color pixel_c2 = Color::BLACK; // Mirrored color for even/odd wings
      if (alpha > 0.0f) {
        if (use_palette && chimera_fx::instance) {
          uint8_t map_idx =
              (uint8_t)((i * 255) / (logical_len > 1 ? logical_len - 1 : 1));

          // Base index depends on reverse (mirror) flag
          uint8_t m1 = reverse ? (255 - map_idx) : map_idx;
          uint8_t m2 = 255 - m1;

          uint32_t cp1 = chimera_fx::instance->_segment.color_from_palette(
              m1, false, true, 255, 255);
          uint32_t cp2 = chimera_fx::instance->_segment.color_from_palette(
              m2, false, true, 255, 255);

          pixel_c1 =
              Color((uint8_t)((cp1 >> 16) & 0xFF), (uint8_t)((cp1 >> 8) & 0xFF),
                    (uint8_t)(cp1 & 0xFF), 0);
          pixel_c2 =
              Color((uint8_t)((cp2 >> 16) & 0xFF), (uint8_t)((cp2 >> 8) & 0xFF),
                    (uint8_t)(cp2 & 0xFF), 0);
        } else {
          pixel_c1 = c;
          pixel_c2 = c;
        }

        // Apply Alpha Blending to Background (Black)
        if (alpha < 1.0f) {
          pixel_c1 = Color(
              (uint8_t)(pixel_c1.r * alpha), (uint8_t)(pixel_c1.g * alpha),
              (uint8_t)(pixel_c1.b * alpha), (uint8_t)(pixel_c1.w * alpha));
          pixel_c2 = Color(
              (uint8_t)(pixel_c2.r * alpha), (uint8_t)(pixel_c2.g * alpha),
              (uint8_t)(pixel_c2.b * alpha), (uint8_t)(pixel_c2.w * alpha));
        }
      }

      // Apply
      // Respect segment bounds and mirror
      int global_idx1 = seg_start + i;
      int global_idx2 = seg_stop - 1 - i;

      if (quadrant) {
        // Quadrant Logic: 4 wings converging from edges/midpoints to centers
        // logical_len = seg_len/4.
        // WINGS: [0->25%] [50%<-25%] [50%->75%] [100%<-75%]
        int idx1 = i;
        int idx2 = (seg_len / 2) - 1 - i;
        int idx3 = (seg_len / 2) + i;
        int idx4 = seg_len - 1 - i;

        if (idx1 >= 0 && idx1 < seg_len)
          it[seg_start + idx1] = pixel_c1;
        if (idx2 >= 0 && idx2 < seg_len)
          it[seg_start + idx2] = pixel_c2;
        if (idx3 >= 0 && idx3 < seg_len)
          it[seg_start + idx3] = pixel_c1;
        if (idx4 >= 0 && idx4 < seg_len)
          it[seg_start + idx4] = pixel_c2;
      } else {
        it[global_idx1] = pixel_c1;
        if (symmetry && global_idx2 >= 0) {
          it[global_idx2] = pixel_c2;
        }
      }
    }

    if (quadrant) {
      // Special case for center pixels of quadrants if lengths are odd
      // But for simple quadrant split, we just ensure the centers are filled
      if (progress >= 1.0f || (reverse && lead > 0)) {
        Color pixel_c = c;
        if (use_palette && chimera_fx::instance) {
          uint32_t cp = chimera_fx::instance->_segment.color_from_palette(
              128, false, true, 255, 255);
          pixel_c = Color((cp >> 16) & 0xFF, (cp >> 8) & 0xFF, cp & 0xFF,
                          (cp >> 24) & 0xFF);
        }
        // Centers are at q1_center and q2_center (plus/minus depending on
        // rounding)
        // For simplicity, we just let the loop handle it
      }
    } else if (symmetry && (seg_len % 2 != 0)) {
      int mid = seg_start + (seg_len / 2);
      bool fill_center = (progress >= 1.0f) || (reverse && lead > 0);
      if (fill_center) {
        if (use_palette && chimera_fx::instance) {
          uint8_t map_idx = 128; // Center of palette gradient
          uint32_t cp = chimera_fx::instance->_segment.color_from_palette(
              map_idx, false, true, 255, 255);
          it[mid] = Color((cp >> 16) & 0xFF, (cp >> 8) & 0xFF, cp & 0xFF,
                          (cp >> 24) & 0xFF);
        } else {
          it[mid] = c;
        }
      } else {
        it[mid] = Color::BLACK;
      }
    }
    break;
  }
  case INTRO_MODE_FADE: {
    // Global Dimming
    uint8_t brightness = (uint8_t)(progress * 255.0f);

    for (int i = 0; i < seg_len; i++) {
      int global_idx = seg_start + i;
      Color base_c = c;
      if (use_palette && chimera_fx::instance) {
        // Map whole segment to spectrum for Fade
        uint8_t map_idx = (uint8_t)((i * 255) / (seg_len > 0 ? seg_len : 1));
        uint32_t cp = chimera_fx::instance->_segment.color_from_palette(
            map_idx, false, true, 255, 255);
        base_c =
            Color((uint8_t)((cp >> 16) & 0xFF), (uint8_t)((cp >> 8) & 0xFF),
                  (uint8_t)(cp & 0xFF), (uint8_t)((cp >> 24) & 0xFF));
      }

      // Explicit Scaling avoiding operator ambiguity
      it[global_idx] =
          Color((base_c.r * brightness) / 255, (base_c.g * brightness) / 255,
                (base_c.b * brightness) / 255, (base_c.w * brightness) / 255);
    }
    break;
  }
  case INTRO_MODE_GLITTER: {
    // Dissolve / Glitter Effect
    // Random pixels turn on based on progress
    uint8_t threshold = (uint8_t)(progress * 255.0f);

    for (int i = 0; i < seg_len; i++) {
      int global_idx = seg_start + i;
      // Deterministic pseudo-random value for this pixel position
      // so it stays ON once it turns ON (Dissolve behavior)
      uint16_t hash = (global_idx * 33) + (global_idx * global_idx);
      uint8_t val = hash % 256;

      bool active = (threshold >= val);

      Color pixel_c = Color::BLACK;
      if (active) {
        if (use_palette && chimera_fx::instance) {
          uint8_t map_idx = (uint8_t)((i * 255) / (seg_len > 0 ? seg_len : 1));
          uint32_t cp = chimera_fx::instance->_segment.color_from_palette(
              map_idx, false, true, 255, 255);
          pixel_c =
              Color((uint8_t)((cp >> 16) & 0xFF), (uint8_t)((cp >> 8) & 0xFF),
                    (uint8_t)(cp & 0xFF), (uint8_t)((cp >> 24) & 0xFF));
        } else {
          pixel_c = c;
        }
      }
      it[global_idx] = pixel_c;
    }
    break;
  }
  case INTRO_MODE_TWIN_PULSE: {
    // Twin Pulse Intro
    // 1. First cursor (symmetric fade up/down)
    // 2. Wide Gap
    // 3. Second cursor (symmetric fade up/down)
    // 4. Long Gap
    // 5. Main Wipe (soft leading edge)
    // Everything moves strictly proportionally to `progress`.

    // Define proportional sizes based on segment length
    float length = (float)seg_len;
    float c_size = length * 0.08f; // Each cursor is 8% of strip
    if (c_size < 3.0f)
      c_size = 3.0f;
    float short_gap = length * 0.12f; // Gap between cursors (increased to 12%)
    if (short_gap < 1.0f)
      short_gap = 1.0f;
    float long_gap = length * 0.10f; // Gap before wipe (10%)
    if (long_gap < 1.0f)
      long_gap = 1.0f;
    float wipe_fade = length * 0.05f; // Leading edge fade of wipe
    if (wipe_fade < 1.0f)
      wipe_fade = 1.0f;

    // Calculate layout offsets from the leading edge (cursor 1 front)
    // These are negative offsets behind the traveling head
    float c1_front = 0.0f;
    float c1_back = c1_front - c_size;
    float c2_front = c1_back - short_gap;
    float c2_back = c2_front - c_size;
    float w_front = c2_back - long_gap;
    float w_solid = w_front - wipe_fade;

    // Total distance the front must travel to pull the solid wipe
    // entirely across the strip.
    float total_distance = length - w_solid;
    float head_pos = (progress * total_distance) + c1_front;

    for (int i = 0; i < seg_len; i++) {
      int idx = reverse ? (seg_len - 1 - i) : i;
      int global_idx = seg_start + idx;
      float fi = (float)idx;

      // Calculate this pixel's relative position behind the traveling head
      // positive value means it is behind the head_pos
      float relative_pos = head_pos - fi;
      float alpha = 0.0f;

      if (relative_pos < 0.0f) {
        // Ahead of the entire formation
        alpha = 0.0f;
      }
      // --- CURSOR 1 ---
      else if (relative_pos <= -c1_back) {
        float c_radius = c_size / 2.0f;
        float center_pos = (-c1_front + -c1_back) / 2.0f;
        float dist_to_center = abs(relative_pos - center_pos);
        if (dist_to_center < c_radius) {
          alpha = 1.0f - (dist_to_center / c_radius);
        }
      }
      // --- SHORT GAP ---
      else if (relative_pos < -c2_front) {
        alpha = 0.0f;
      }
      // --- CURSOR 2 ---
      else if (relative_pos <= -c2_back) {
        float c_radius = c_size / 2.0f;
        float center_pos = (-c2_front + -c2_back) / 2.0f;
        float dist_to_center = abs(relative_pos - center_pos);
        if (dist_to_center < c_radius) {
          alpha = 1.0f - (dist_to_center / c_radius);
        }
      }
      // --- LONG GAP ---
      else if (relative_pos < -w_front) {
        alpha = 0.0f;
      }
      // --- WIPE ---
      else {
        float internal_pos = relative_pos + w_front; // 0 at wipe front edge
        if (internal_pos < wipe_fade) {
          alpha = internal_pos / wipe_fade; // Soft leading edge
        } else {
          alpha = 1.0f; // Solid block
        }
      }

      // Clamp alpha to safe rendering limits
      if (alpha < 0.0f)
        alpha = 0.0f;
      if (alpha > 1.0f)
        alpha = 1.0f;

      // Render Pixel
      if (alpha > 0.0f) {
        Color base_c = c;
        if (use_palette && chimera_fx::instance) {
          uint8_t map_idx =
              (uint8_t)((idx * 255) / (seg_len > 0 ? seg_len : 1));
          uint32_t cp = chimera_fx::instance->_segment.color_from_palette(
              map_idx, false, true, 255, 255);
          base_c =
              Color((uint8_t)((cp >> 16) & 0xFF), (uint8_t)((cp >> 8) & 0xFF),
                    (uint8_t)(cp & 0xFF), (uint8_t)((cp >> 24) & 0xFF));
        }

        it[global_idx] =
            Color((uint8_t)(base_c.r * alpha), (uint8_t)(base_c.g * alpha),
                  (uint8_t)(base_c.b * alpha), (uint8_t)(base_c.w * alpha));
      } else {
        it[global_idx] = Color::BLACK;
      }
    }
    break;
  }
  case INTRO_MODE_HYDRAULICS: {
    uint64_t now_ms = millis_64();
    if (act_->hydraulics_last_ms == 0) {
      act_->hydraulics_last_ms = now_ms;
      act_->hydraulics_fluid_level = 0.0f;
      act_->hydraulics_fluid_velocity = 0.0f;
      act_->hydraulics_particle_count =
          0; // audit 3.3: fixed array, reset count
    }
    uint32_t dt_ms = (uint32_t)(now_ms - act_->hydraulics_last_ms);
    if (dt_ms == 0)
      dt_ms = 1;
    act_->hydraulics_last_ms = now_ms;

    float speed_scale = act_->active_intro_speed / 255.0f;
    float intensity_val = 127 / 255.0f;
    if (act_->controller && act_->controller->get_intensity()) {
      intensity_val = act_->controller->get_intensity()->state / 255.0f;
    }

    // 1. Organic Physics (Surge & Slosh)
    float target_l = (float)seg_len;
    float dt = dt_ms / 1000.0f;
    float damping = 1.0f + (intensity_val * 4.0f);
    float pressure = 10.0f + (speed_scale * 50.0f);

    float force = (target_l - act_->hydraulics_fluid_level) * pressure;
    float accel = force - (damping * act_->hydraulics_fluid_velocity);
    act_->hydraulics_fluid_velocity += accel * dt;
    act_->hydraulics_fluid_level += act_->hydraulics_fluid_velocity * dt;

    // --- Impact Spawning (When water hits the end of the pipe) ---
    if (act_->hydraulics_fluid_level > target_l) {
      act_->hydraulics_fluid_level = target_l;

      if (act_->hydraulics_fluid_velocity > 15.0f) {
        int splash_count = (cfx::hw_random8(4)) + 3; // 3 to 6 drops — CFX-023
        for (int d = 0; d < splash_count; d++) {
          if (act_->hydraulics_particle_count <
              MAX_HYDRAULICS_PARTICLES) { // audit 3.3
            act_->hydraulics_particles[act_->hydraulics_particle_count++] = {
                target_l,
                -act_->hydraulics_fluid_velocity *
                    (0.2f + (cfx::hw_random8(50)) / 100.0f), // CFX-023
                true};
          }
        }
      }
      act_->hydraulics_fluid_velocity *= -0.3f; // Slosh dampening
    }

    if (act_->hydraulics_fluid_level < 0.0f) {
      act_->hydraulics_fluid_level = 0.0f;
      act_->hydraulics_fluid_velocity = 0.0f;
    }

    // --- Continuous Spray Spawning (While moving fast) ---
    if (act_->hydraulics_fluid_velocity > 8.0f &&
        act_->hydraulics_particle_count <
            MAX_HYDRAULICS_PARTICLES) { // audit 3.3
      if (cfx::hw_random8(100) < 40) {  // CFX-023
        act_->hydraulics_particles[act_->hydraulics_particle_count++] = {
            act_->hydraulics_fluid_level,
            act_->hydraulics_fluid_velocity *
                (1.1f + (cfx::hw_random8(40)) / 100.0f), // CFX-023
            true};
      }
    }

    // 2. Strict Buffer Clearing
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // 3. Render Shimmering Fluid Mass (Coherent Waves)
    int floor_level = (int)act_->hydraulics_fluid_level;
    float vel_glow = 0.15f * (abs(act_->hydraulics_fluid_velocity) / target_l);
    float wave_time = now_ms * 0.005f;

    for (int i = 0; i < floor_level; i++) {
      if (i < seg_len) {
        // Overlapping Sine Waves create a flowing "liquid" texture
        float wave1 = sinf(i * 0.5f - wave_time);
        float wave2 = sinf(i * 0.8f - (wave_time * 1.3f));
        float liquid_noise = (wave1 + wave2) * 0.15f;

        float brightness =
            0.7f + liquid_noise + vel_glow; // v4.1 Match White Peak

        // The "Froth" (Water is brighter/turbulent at the leading edge)
        float dist_to_head = act_->hydraulics_fluid_level - i;
        if (dist_to_head < 5.0f) {
          brightness += (5.0f - dist_to_head) * 0.15f;
        }

        if (brightness > 1.0f)
          brightness = 1.0f;
        if (brightness < 0.1f)
          brightness = 0.1f;
        uint8_t b = (uint8_t)(255 * brightness);
        uint8_t r = b, g = b, b_val = b, w = b;
        if (act_->active_force_white)
          cfx::apply_force_white(r, g, b_val, w);
        it[seg_start + i] = Color(r, g, b_val, w);
      }
    }

    // Anti-aliased exact head
    if (floor_level < seg_len && floor_level >= 0) {
      float fraction = act_->hydraulics_fluid_level - floor_level;
      uint8_t b = (uint8_t)(255 * (0.8f + fraction * 0.2f));
      uint8_t r = b, g = b, b_val = b, w = b;
      if (act_->active_force_white)
        cfx::apply_force_white(r, g, b_val, w);
      it[seg_start + floor_level] = Color(r, g, b_val, w);
    }

    // 4. Droplets / Particles Rendering
    float gravity = 25.0f + (intensity_val * 20.0f);
    for (uint8_t _pi = 0; _pi < act_->hydraulics_particle_count;
         _pi++) { // audit 3.3
      auto &p = act_->hydraulics_particles[_pi];
      if (!p.active)
        continue;
      p.vel -= gravity * dt;
      p.pos += p.vel * dt;
      if (p.pos <= act_->hydraulics_fluid_level) {
        p.active = false;
        continue;
      }
      if (p.pos >= target_l) {
        p.pos = target_l - 0.1f;
        p.vel *= -0.3f;
      }
      int p_idx = (int)p.pos;
      if (p_idx >= 0 && p_idx < seg_len) {
        uint8_t r = 255, g = 255, b_val = 255, w = 255;
        if (act_->active_force_white)
          cfx::apply_force_white(r, g, b_val, w);
        it[seg_start + p_idx] = Color(r, g, b_val, w);
      }
    }
    // Compact inactive particles (audit 3.3: replaces erase/remove_if on
    // vector)
    {
      uint8_t w = 0;
      for (uint8_t _pi = 0; _pi < act_->hydraulics_particle_count; _pi++)
        if (act_->hydraulics_particles[_pi].active)
          act_->hydraulics_particles[w++] = act_->hydraulics_particles[_pi];
      act_->hydraulics_particle_count = w;
    }
    break;
  }
  case INTRO_MODE_MORSE: {
    uint32_t speed_val = act_->active_intro_speed;
    uint32_t unit_ms = 80 + ((255 - speed_val) * 100 / 255);
    uint32_t elapsed_m = (uint32_t)(millis_64() - act_->intro_start_time);
    uint32_t current_bit = elapsed_m / unit_ms;

    uint64_t mask = 0b1110111011100011101ULL;
    uint8_t total_bits = 19;

    bool is_on = false;
    if (current_bit >= total_bits) {
      is_on = true; // Hold ON after sequence
    } else {
      is_on = (mask >> (total_bits - 1 - current_bit)) & 0x01;
    }

    for (int i = 0; i < seg_len; i++) {
      int global_idx = seg_start + i;
      if (is_on) {
        uint8_t map_idx = (uint8_t)((i * 255) / (seg_len > 0 ? seg_len : 1));
        if (use_palette && chimera_fx::instance) {
          uint32_t cp = chimera_fx::instance->_segment.color_from_palette(
              map_idx, false, true, 255, 255);
          it[global_idx] =
              Color((uint8_t)((cp >> 16) & 0xFF), (uint8_t)((cp >> 8) & 0xFF),
                    (uint8_t)(cp & 0xFF), (uint8_t)((cp >> 24) & 0xFF));
        } else {
          it[global_idx] = target_color;
        }
      } else {
        it[global_idx] = Color::BLACK;
      }
    }
    break;
  }
  case INTRO_MODE_DROPPING: {
    // ── 1. Duration fetch ───────────────────────────────────────────────────
    uint32_t duration = 1000;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();

    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value()) {
      duration = duration_override.value();
    }
    if (duration == 0)
      duration = 1;

    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;

    // ── 2. Fill level: ease-out cubic ───────────────────────────────────────
    float inv = 1.0f - prog;
    float eased = 1.0f - (inv * inv * inv); // cubic ease-out
    int fill_level = (int)(eased * (float)seg_len);
    if (fill_level > seg_len)
      fill_level = seg_len;

    // ── 3. Color helpers (Monochromatic) ────────────────────────────────────
    // Integer-only dim: factor 0=black, 255=full brightness
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };

    // Additive boost (white-flash for splash/shimmer)
    auto boost = [](Color col, uint8_t b) -> Color {
      return Color((uint8_t)((int)col.r + b > 255 ? 255 : col.r + b),
                   (uint8_t)((int)col.g + b > 255 ? 255 : col.g + b),
                   (uint8_t)((int)col.b + b > 255 ? 255 : col.b + b),
                   (uint8_t)((int)col.w + b > 255 ? 255 : col.w + b));
    };

    // ── 4. Clear everything above fill level ────────────────────────────────
    for (int i = fill_level; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // ── 5. Draw water body with depth shading + surface shimmer ─────────────
    uint32_t shimmer_t = elapsed % 120; // 120 ms period
    uint8_t shimmer = (shimmer_t < 60)
                          ? (uint8_t)(shimmer_t * 3)          // 0→180 rise
                          : (uint8_t)((120 - shimmer_t) * 3); // 180→0 fall

    for (int i = 0; i < fill_level; i++) {
      Color base = target_color;

      // Surface shimmer on top 2 pixels
      int dist_from_surface = (fill_level - 1) - i;
      if (dist_from_surface == 0)
        base = boost(base, shimmer);
      else if (dist_from_surface == 1)
        base = boost(base, shimmer / 3);

      it[seg_start + i] = base;
    }

    // ── 6. Falling drops — 3 staggered, gravity-accelerated ─────────────────
    if (fill_level < seg_len) {
      const int NUM_DROPS = 3;
      uint32_t period_ms = (uint32_t)(duration / 5);
      if (period_ms < 100)
        period_ms = 100;
      if (period_ms > 350)
        period_ms = 350;

      for (int d = 0; d < NUM_DROPS; d++) {
        uint32_t phase = (uint32_t)(d * period_ms / NUM_DROPS);
        uint32_t t = (elapsed + phase) % period_ms;
        float drop_t = (float)t / (float)period_ms; // 0=top → 1=surface

        // Gravity: quadratic ease-in (accelerates toward surface)
        float fall = drop_t * drop_t;

        int span = (seg_len - 1) - fill_level;
        if (span <= 0)
          break;

        // drop_px: seg_len-1 at fall=0, fill_level at fall=1
        int drop_px = (seg_len - 1) - (int)(fall * (float)span);
        if (drop_px < fill_level)
          drop_px = fill_level;
        if (drop_px >= seg_len)
          continue;

        // Drop head: bright white-boosted
        it[seg_start + drop_px] = boost(target_color, 90);

        // Two-pixel trailing tail (fades upward from head)
        if (drop_px + 1 < seg_len)
          it[seg_start + drop_px + 1] = dim(target_color, 70);
        if (drop_px + 2 < seg_len)
          it[seg_start + drop_px + 2] = dim(target_color, 25);

        // ── Impact splash: surface flares as drop arrives ────────────────
        if (drop_t > 0.85f && fill_level > 0) {
          float st = (drop_t - 0.85f) / 0.15f;            // 0→1 over last 15%
          float env = (st < 0.6f) ? (st / 0.6f)           // fast rise
                                  : ((1.0f - st) / 0.4f); // slower decay
          uint8_t splash_b = (uint8_t)(env * 200.0f);
          int surf = fill_level - 1;
          it[seg_start + surf] = boost(target_color, splash_b);
          if (surf - 1 >= 0)
            it[seg_start + surf - 1] = boost(target_color, splash_b / 3);
        }
      }
    }
    break;
  }
  case INTRO_MODE_ASSEMBLY: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1000;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();

    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value()) {
      duration = duration_override.value();
    }
    if (duration == 0)
      duration = 1;

    // ── 2. Mirroring
    // ──────────────────────────────────────────────────────────
    bool reverse = false;
#ifdef USE_CFX_SEQUENCE
    if (act_->sequence_mirror.has_value()) {
      reverse = act_->sequence_mirror.value();
    } else {
#endif
      switch_::Switch *mirror_sw = this->local_mirror_();
      if (mirror_sw == nullptr && act_->controller != nullptr)
        mirror_sw = act_->controller->get_mirror();
      reverse = (mirror_sw != nullptr && mirror_sw->state);
#ifdef USE_CFX_SEQUENCE
    }
#endif

    // ── 3. Knuth multiplicative hash for deterministic block sizes
    // ────────────
    auto block_sz = [](int idx) -> int {
      uint32_t h = (uint32_t)idx * 2654435761u;
      return (int)(h >> 30) + 1; // 1 to 4 pixels
    };

    // ── 4. Precompute block layout
    // ────────────────────────────────────────────
    struct BlockInfo {
      int start;
      int size;
    };
    std::vector<BlockInfo> blocks;
    int cursor = 0;
    while (cursor < seg_len) {
      int sz = block_sz(blocks.size());
      if (cursor + sz > seg_len)
        sz = seg_len - cursor;
      blocks.push_back({cursor, sz});
      cursor += sz;
    }

    // ── 5. Timing partitioning
    // ──────────────────────────────────────────────── Each block has a
    // staggered start time. The last block lands at the end of the duration.
    int num_blocks = blocks.size();
    float f_duration = (float)duration;
    // Each block takes 40% of the total duration to fall.
    // The starts are staggered over the remaining 60%.
    float fall_duration = f_duration * 0.4f;
    float total_stagger = f_duration * 0.6f;
    float block_interval =
        (num_blocks > 1) ? (total_stagger / (float)(num_blocks - 1)) : 0.0f;

    // ── 6. Rendering
    // ────────────────────────────────────────────────────────── Clear strip
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    float current_time = progress * f_duration;

    for (int i = 0; i < num_blocks; i++) {
      BlockInfo b = blocks[i];
      float start_time = (float)i * block_interval;

      // Determine physical target position based on mirror
      int target_start = reverse ? (seg_len - b.size - b.start) : b.start;

      if (current_time >= start_time + fall_duration) {
        // Landed
        for (int j = 0; j < b.size; j++)
          it[seg_start + target_start + j] = c;
      } else if (current_time > start_time) {
        // Falling
        float b_elapsed = current_time - start_time;
        float b_prog = b_elapsed / fall_duration;
        float fall_prog = b_prog * b_prog; // quadratic ease-in

        // Direction: Fall from "Off-screen" towards "Target"
        // Minimal offset to ensure immediate visibility upon start
        int offset = 5;
        int f_start = reverse ? -offset : (seg_len + offset);
        int f_end = target_start;
        int span = f_end - f_start;
        int current_pos = f_start + (int)(fall_prog * (float)span);

        for (int j = 0; j < b.size; j++) {
          int px = current_pos + (reverse ? j : (j - (b.size - 1)));
          if (px >= 0 && px < seg_len)
            it[seg_start + px] = c;
        }
      }
    }
    break;
  }

    // ── Shared monochromatic lambdas (defined per scope to match existing
    // pattern) ──

  case INTRO_MODE_INERTIA_SWEEP: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1000;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Local helpers
    // ──────────────────────────────────────────────────────
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };
    auto boost = [](Color col, uint8_t b) -> Color {
      return Color((uint8_t)((int)col.r + b > 255 ? 255 : col.r + b),
                   (uint8_t)((int)col.g + b > 255 ? 255 : col.g + b),
                   (uint8_t)((int)col.b + b > 255 ? 255 : col.b + b),
                   (uint8_t)((int)col.w + b > 255 ? 255 : col.w + b));
    };
    auto ease_in_out = [](float p) -> float {
      return p * p * (3.0f - 2.0f * p);
    };

    // ── 3. Eased fill position
    // ────────────────────────────────────────────────
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;

    float eased = ease_in_out(prog);
    int fill_px = (int)(eased * (float)seg_len);
    if (fill_px > seg_len)
      fill_px = seg_len;

    // ── 4. Leading-edge glow: 3-pixel brightness ramp at the cursor
    // ───────────
    static const uint8_t LEAD_BRIGHT[3] = {255, 160, 60};

    // ── 5. Clear strip
    // ────────────────────────────────────────────────────────
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // ── 6. Draw filled body
    // ───────────────────────────────────────────────────
    for (int i = 0; i < fill_px; i++)
      it[seg_start + i] = c;

    // ── 7. Draw hot leading edge
    // ──────────────────────────────────────────────
    for (int g = 0; g < 3; g++) {
      int px = fill_px - 1 - g;
      if (px >= 0 && px < seg_len)
        it[seg_start + px] = boost(c, LEAD_BRIGHT[g] >> 1);
    }
    if (fill_px < seg_len) {
      uint8_t ahead_b = (uint8_t)(128.0f * (1.0f - prog));
      it[seg_start + fill_px] = dim(c, ahead_b);
    }
    break;
  }

  case INTRO_MODE_SONAR_REVEAL: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 2000;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Local helpers
    // ──────────────────────────────────────────────────────
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };

    // ── 3. Layout: 4 passes, each raising the floor
    // ───────────────────────────
    const int NUM_PASSES = 4;

    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;

    float pass_prog = prog * (float)NUM_PASSES;
    int pass_done = (int)pass_prog;
    if (pass_done > NUM_PASSES)
      pass_done = NUM_PASSES;
    float within = pass_prog - (float)pass_done;

    // Smooth gentle fade-in matching the outro behavior
    uint8_t floor_b = (uint8_t)(prog * 255.0f);

    float scan_t = (pass_done % 2 == 0) ? within : (1.0f - within);
    int scan_px = (int)(scan_t * (float)(seg_len - 1));

    int scan_w = seg_len / 20;
    if (scan_w < 3)
      scan_w = 3;

    // ── 4. Draw floor + scanner
    // ───────────────────────────────────────────────
    for (int i = 0; i < seg_len; i++) {
      int dist = i - scan_px;
      if (dist < 0)
        dist = -dist;

      if (dist < scan_w) {
        uint8_t beam_b = (uint8_t)(255 - (dist * 255 / scan_w));
        uint8_t final_b = beam_b > floor_b ? beam_b : floor_b;
        it[seg_start + i] = dim(c, final_b);
      } else {
        it[seg_start + i] = dim(c, floor_b);
      }
    }
    break;
  }

  case INTRO_MODE_VENETIAN: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1200;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Local helpers
    // ──────────────────────────────────────────────────────
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };
    auto ease_in_out = [](float p) -> float {
      return p * p * (3.0f - 2.0f * p);
    };

    // ── 3. Two-phase split: 0→0.5 = evens snap on, 0.5→1.0 = odds fade in ───
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;

    uint8_t even_b, odd_b;
    if (prog <= 0.5f) {
      float p1 = prog * 2.0f;
      float e1 = ease_in_out(p1);
      even_b = (uint8_t)(e1 * 255.0f);
      odd_b = 0;
    } else {
      float p2 = (prog - 0.5f) * 2.0f;
      float e2 = ease_in_out(p2);
      even_b = 255;
      odd_b = (uint8_t)(e2 * 255.0f);
    }

    // ── 4. Draw
    // ───────────────────────────────────────────────────────────────
    for (int i = 0; i < seg_len; i++) {
      uint8_t b = (i % 2 == 0) ? even_b : odd_b;
      it[seg_start + i] = dim(c, b);
    }
    break;
  }

  case INTRO_MODE_CRYSTALLIZE: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1500;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Local helpers
    // ──────────────────────────────────────────────────────
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };

    // ── 3. Seed points: 4 nodes, spread via Knuth hash
    // ────────────────────────
    const int NUM_SEEDS = 4;
    int seeds[NUM_SEEDS];
    for (int s = 0; s < NUM_SEEDS; s++) {
      uint32_t h = (uint32_t)(s * 1234567u + 0xDEADBEEFu) * 2654435761u;
      seeds[s] = (int)((h >> 16) % (uint32_t)seg_len);
    }

    // ── 4. Ease-out cubic radius
    // ──────────────────────────────────────────────
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;

    float inv = 1.0f - prog;
    float eased = 1.0f - (inv * inv * inv);
    float radius = eased * (float)seg_len;

    const int EDGE_PX = 3;

    // ── 5. Draw each pixel based on distance to nearest seed
    // ──────────────────
    for (int i = 0; i < seg_len; i++) {
      int min_dist = seg_len;
      for (int s = 0; s < NUM_SEEDS; s++) {
        int d = i - seeds[s];
        if (d < 0)
          d = -d;
        if (d < min_dist)
          min_dist = d;
      }

      float dist_f = (float)min_dist;
      uint8_t b;
      if (dist_f <= radius - EDGE_PX) {
        b = 255;
      } else if (dist_f <= radius) {
        float t = (radius - dist_f) / (float)EDGE_PX;
        b = (uint8_t)(t * 255.0f);
      } else {
        b = 0;
      }
      it[seg_start + i] = dim(c, b);
    }
    break;
  }

  case INTRO_MODE_DEEP_BREATHE: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1800;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Local helpers
    // ──────────────────────────────────────────────────────
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };

    // ── 3. Gamma-corrected brightness — cubic ease-in, warm-up feel
    // ───────────
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;

    float eased = prog * prog * prog;
    uint8_t raw_b = (uint8_t)(eased * 255.0f);
    uint8_t b =
        (raw_b == 0) ? 0 : (uint8_t)(((uint16_t)raw_b * raw_b) >> 8) + 1;

    // ── 4. Triangular shimmer over strip length
    // ───────────────────────────────
    for (int i = 0; i < seg_len; i++) {
      int half = seg_len / 2;
      int dist = i < half ? i : seg_len - 1 - i;
      int shimmer = (dist * 24 / (half > 0 ? half : 1)) - 12;
      int final_b = (int)b + shimmer;
      if (final_b < 0)
        final_b = 0;
      if (final_b > 255)
        final_b = 255;
      it[seg_start + i] = dim(c, (uint8_t)final_b);
    }
    break;
  }

  case INTRO_MODE_MOIRE_SHIFT: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1200;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Local helpers
    // ──────────────────────────────────────────────────────
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };

    float prog = (float)elapsed / (duration > 0 ? (float)duration : 1.0f);
    if (prog > 1.0f)
      prog = 1.0f;
    float eased_p = prog * prog * (3.0f - 2.0f * prog);
    uint8_t env = (uint8_t)(eased_p * 255.0f);

    uint8_t t1 = (uint8_t)(elapsed >> 4);
    uint8_t t2 = (uint8_t)((elapsed * 3u) >> 5);

    // Restore original Interference/Moire ripples for the full duration
    // The "gentle turn on" is handled by the global transition after intro ends
    for (int i = 0; i < seg_len; i++) {
      uint8_t s = cfx::sin8((uint8_t)(i * 3u) + t1);
      uint8_t co = cfx::sin8((uint8_t)((uint8_t)(i * 5u) - t2 + 64u));
      uint8_t avg = (uint8_t)(((uint16_t)s + co) >> 1);
      uint8_t gam = (uint8_t)(((uint16_t)avg * avg) >> 8);

      uint8_t final_b = (uint8_t)(((uint16_t)gam * env) >> 8);
      it[seg_start + i] = dim(c, final_b);
    }
    break;
  }

  case INTRO_MODE_RESONANCE_FILL: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1400;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // -- 2. Duration fetch already handled --
    // Lambdas moved to function scope

    // ── 3. Sweep position (smoothstep)
    // ────────────────────────────────────────
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;
    float eased = prog * prog * (3.0f - 2.0f * prog);
    int sweep_pos = (int)(eased * (float)seg_len);
    if (sweep_pos > seg_len)
      sweep_pos = seg_len;

    uint8_t inv_prog_b = (uint8_t)((1.0f - prog) * 255.0f);

    // ── 4. Clear strip
    // ────────────────────────────────────────────────────────
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // ── 5. Draw filled region with ripple behind the sweep head ──────────────
    for (int i = 0; i < sweep_pos; i++) {
      int dist = sweep_pos - 1 - i; // 0 at head, grows toward strip start

      uint8_t dist_factor =
          (uint8_t)((dist * 255) / (seg_len > 0 ? seg_len : 1));
      // Deepen falloff
      uint8_t falloff = (uint8_t)(((uint16_t)dist_factor * inv_prog_b) >> 7);

      // Ripple: sine wave over distance, amplitude decays with inv_prog_b
      uint8_t ripple_raw = cfx::sin8((uint8_t)(dist * 25u));
      // Double the ripple amplitude for visibility
      int ripple = (((int)ripple_raw - 128) * 2 * inv_prog_b) / 255;

      int bri = 255 - (int)falloff + ripple;
      if (bri < 0)
        bri = 0;
      if (bri > 255)
        bri = 255;

      it[seg_start + i] = dim(c, (uint8_t)bri);
    }

    // ── 6. Hot leading edge
    // ───────────────────────────────────────────────────
    if (sweep_pos < seg_len) {
      it[seg_start + sweep_pos] = boost(c, 60);
      if (sweep_pos + 1 < seg_len)
        it[seg_start + sweep_pos + 1] = dim(c, 80);
    }
    break;
  }

  case INTRO_MODE_TELEMETRY: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1200;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // Lambdas moved to function scope

    // ── 3. Dash geometry
    // ──────────────────────────────────────────────────────
    const int DASH_LEN = 6;
    const int GAP_LEN = 2;

    // ── 4. Two-Stage Sweep Logic (Intro) ────────────────────────
    // Stage 1 (0.0 - 0.5): Wipe in the blocks/dashes
    // Stage 2 (0.5 - 1.0): Wipe in the gap-fill
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;

    float stage1_prog = prog / 0.5f;
    if (stage1_prog > 1.0f)
      stage1_prog = 1.0f;

    float stage2_prog = (prog - 0.5f) / 0.5f;
    if (stage2_prog < 0.0f)
      stage2_prog = 0.0f;

    int sweep1_pos = (int)(stage1_prog * (float)seg_len);
    int sweep2_pos = (int)(stage2_prog * (float)seg_len);

    // ── 5. Clear strip
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // ── 6. Rendering Logic
    for (int i = 0; i < seg_len; i++) {
      int phase = i % DASH_LEN;
      bool is_dash = (phase < (DASH_LEN - GAP_LEN));

      if (is_dash) {
        if (i < sweep1_pos) {
          int dist_in_unit = (DASH_LEN - GAP_LEN - 1) - phase;
          uint8_t blade_b = (uint8_t)(255 - dist_in_unit * 18);
          it[seg_start + i] = dim(c, blade_b);
        }
      } else {
        if (i < sweep2_pos) {
          it[seg_start + i] = c;
        }
      }
    }

    // ── 7. Hot leading edges
    if (sweep1_pos > 0 && sweep1_pos < seg_len && stage1_prog < 1.0f) {
      it[seg_start + sweep1_pos - 1] =
          boost(it[seg_start + sweep1_pos - 1].get(), 50);
    }
    if (sweep2_pos > 0 && sweep2_pos < seg_len && stage2_prog > 0.0f) {
      it[seg_start + sweep2_pos - 1] =
          boost(it[seg_start + sweep2_pos - 1].get(), 30);
    }
    break;
  }

  case INTRO_MODE_STELLAR_DUST: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 2000;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // Lambdas moved to function scope

    // Restore pure stars for the full duration
    float prog = (float)elapsed / (duration > 0 ? (float)duration : 1.0f);
    if (prog > 1.0f)
      prog = 1.0f;
    float eased = prog * (2.0f - prog); // Quadratic ease-out
    uint8_t env = (uint8_t)(eased * 255.0f);
    uint8_t t = (uint8_t)(elapsed >> 5);

    for (int i = 0; i < seg_len; i++) {
      uint8_t phase = (uint8_t)((uint32_t)(i) * 2654435761u >> 24);
      uint8_t osc = cfx::sin8(t + phase);
      uint8_t floor_b = (uint8_t)(((uint16_t)80u * env) >> 8);
      uint8_t scaled = (uint8_t)(((uint16_t)osc * 175u) >> 8);
      uint8_t star_b = (uint8_t)(floor_b + scaled);

      uint8_t final_b = (uint8_t)(((uint16_t)star_b * env) >> 8);
      it[seg_start + i] = dim(c, final_b);
    }
    break;
  }

  case INTRO_MODE_INTERFERENCE: {
    // ── Interference Intro: Soft fade-in with Moire-like pattern ──
    float prog = (float)elapsed / (duration > 0 ? (float)duration : 1.0f);
    if (prog > 1.0f)
      prog = 1.0f;
    float eased = prog * (2.0f - prog); // Quadratic ease-out
    uint8_t env = (uint8_t)(eased * 255.0f);

    uint8_t t1 = (uint8_t)(elapsed >> 4);
    uint8_t t2 = (uint8_t)((elapsed * 3u) >> 5);

    for (int i = 0; i < seg_len; i++) {
      uint8_t s = cfx::sin8((uint8_t)(i * 3u) + t1);
      uint8_t co = cfx::sin8((uint8_t)((uint8_t)(i * 5u) - t2 + 64u));
      uint8_t avg = (uint8_t)(((uint16_t)s + co) >> 1);
      uint8_t gam = (uint8_t)(((uint16_t)avg * avg) >> 8);

      // Blend from black (alpha=env) towards the pattern
      uint8_t final_b = (uint8_t)(((uint16_t)gam * env) >> 8);
      it[seg_start + i] = dim(c, final_b);
    }
    break;
  }

  case INTRO_MODE_ECLIPSE: {
    // ── 1. Duration / Intensity fetch
    // ──────────────────────────────────────────
    uint32_t duration = 1500;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    uint8_t intensity = 128;
    if (this->local_intensity_() != nullptr &&
        this->local_intensity_()->has_state())
      intensity = (uint8_t)this->local_intensity_()->state;
    else if (act_->controller != nullptr &&
             act_->controller->get_intensity() != nullptr &&
             act_->controller->get_intensity()->has_state())
      intensity = (uint8_t)act_->controller->get_intensity()->state;
    else if (this->has_intensity_preset_())
      intensity = this->intensity_preset_val_();

    // ── 2. Global brightness envelope: cubic ease-in (lingers dark, then
    // glows) ─
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;
    uint8_t env = (uint8_t)(cfx::ease_in_out(prog) * 255.0f);

    // ── 3. Shadow geometry
    // ─────────────────────────────────────────────────────
    const uint8_t BASE_B = 180;
    // Map Intensity to shadow width (from 10% up to 50% of the strip)
    float hw_frac = 0.10f + (intensity / 255.0f) * 0.40f;
    float shadow_hw = seg_len * hw_frac;
    if (shadow_hw < 4.0f)
      shadow_hw = 4.0f;

    // Smooth sweep exactly across the string (including margins for shadow
    // width)
    float total_range = (float)seg_len + 2.0f * shadow_hw;
    float shadow_px = -shadow_hw + prog * total_range;

    // ── 4. Draw strip: base brightness minus smoothstep shadow dip
    // ────────────
    for (int i = 0; i < seg_len; i++) {
      float dist = fabsf((float)i - shadow_px);
      if (dist > shadow_hw)
        dist = shadow_hw;

      // Smoothstep profile for a "wetter", smoother, premium shadow
      float d_norm = 1.0f - dist / shadow_hw;
      float smooth_d = d_norm * d_norm * (3.0f - 2.0f * d_norm);
      uint8_t shadow_depth = (uint8_t)(smooth_d * 215.0f);

      int bri = (int)BASE_B - (int)shadow_depth;
      if (bri < 0)
        bri = 0;

      // Apply intro envelope
      uint8_t final_b = (uint8_t)(((uint16_t)(uint8_t)bri * env) >> 8);
      it[seg_start + i] = dim(c, final_b);
    }

    // Smooth the physical edges
    if (act_->runner) {
      act_->runner->_segment.blur(32);
    }
    break;
  }

  case INTRO_MODE_GAS_DISCHARGE: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 2200; // default longer: the stutter IS the experience
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Phase boundaries
    // ───────────────────────────────────────────────────
    uint32_t p1_end = duration * 35 / 100; // sparse flashes
    uint32_t p2_end = duration * 65 / 100; // rapid strikes
    uint32_t p3_end = duration * 88 / 100; // buzz stabilisation
    // p4: remainder → lock-in

    uint8_t brightness = 0;

    if (elapsed < p1_end) {
      // ── Phase 1: Sparse violent flashes ──────────────────────────────────
      //    80 ms time slots.  High-bit of hash decides flash vs dark.
      //    A flash slot itself is split: first 25 ms bright, then 55 ms dark
      //    (simulates the fast-extinguishing of a struggling arc).
      const uint32_t SLOT_MS = 80;
      uint32_t slot = elapsed / SLOT_MS;
      uint32_t within_slot = elapsed % SLOT_MS;
      uint32_t h = cfx::knuth32(slot * 7u + 1u);
      bool is_flash_slot = (h >> 29) > 5u; // ~25% of slots are flashes

      if (is_flash_slot && within_slot < 25u) {
        brightness = 255;
      } else {
        brightness = 0;
      }
    } else if (elapsed < p2_end) {
      // ── Phase 2: Rapid strikes ────────────────────────────────────────────
      //    30 ms slots.  Higher flash density (~55%).  Between flashes, a
      //    dim afterglow (brightness 40) from the residual ionised gas.
      const uint32_t SLOT_MS = 30;
      uint32_t phase_t = elapsed - p1_end;
      uint32_t slot = phase_t / SLOT_MS;
      uint32_t within_slot = phase_t % SLOT_MS;
      uint32_t h = cfx::knuth32(slot * 13u + 2u);
      bool is_flash_slot = (h >> 29) > 3u; // ~55% are flashes

      if (is_flash_slot && within_slot < 18u) {
        brightness = 255;
      } else {
        // Dim afterglow: linearly fades from 60 → 0 over the dark portion
        uint32_t dark_t = within_slot > 18u ? within_slot - 18u : 0u;
        brightness = (uint8_t)(60u > dark_t * 2u ? 60u - dark_t * 2u : 0u);
      }
    } else if (elapsed < p3_end) {
      // ── Phase 3: Buzz — oscillates 150–255, amplitude shrinks as stabilises
      // ─
      //    Period starts at 30 ms (fast flutter) and extends to 60 ms (calm
      //    buzz)
      uint32_t phase_t = elapsed - p2_end;
      uint32_t phase_dur = p3_end - p2_end;
      float norm = (float)phase_t / (float)phase_dur; // 0→1
      // Amplitude: starts at 52 (full 150-255 swing), shrinks to 18 (237-255)
      uint8_t amp = (uint8_t)(52.0f * (1.0f - norm * 0.65f));
      // Period: 30 ms → 55 ms
      uint32_t period = 30u + (uint32_t)(norm * 25.0f);
      uint8_t t = (uint8_t)((phase_t % period) * 255u / period);
      // sin8 oscillation centred at (255 - amp)
      brightness =
          (255u - amp) + (uint8_t)(((uint16_t)cfx::sin8(t) * amp) >> 8);
    } else {
      // ── Phase 4: Final lock-in — linear ramp 210 → 255 ───────────────────
      uint32_t phase_t = elapsed - p3_end;
      uint32_t phase_dur = duration - p3_end;
      if (phase_dur == 0)
        phase_dur = 1;
      brightness = (uint8_t)(210u + (phase_t * 45u / phase_dur));
      if (brightness > 255u)
        brightness = 255u;
    }

    // ── 3. Apply brightness to full strip ────────────────────────────────────
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = dim(c, brightness);
    break;
  }

  case INTRO_MODE_HARMONIC_SETTLE: {
    // ── 1. Duration / Intensity fetch
    // ──────────────────────────────────────────
    uint32_t duration = 1600;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    uint8_t intensity = 128;
    if (this->local_intensity_() != nullptr &&
        this->local_intensity_()->has_state())
      intensity = (uint8_t)this->local_intensity_()->state;
    else if (act_->controller != nullptr &&
             act_->controller->get_intensity() != nullptr &&
             act_->controller->get_intensity()->has_state())
      intensity = (uint8_t)act_->controller->get_intensity()->state;
    else if (this->has_intensity_preset_())
      intensity = this->intensity_preset_val_();

    // ── 2. Spring position
    // ────────────────────────────────────────────────────
    float t_norm = (float)elapsed / (float)duration;
    if (t_norm > 1.0f)
      t_norm = 1.0f;

    // Intensity controls the stiffness (number of oscillations)
    float oscillations =
        1.0f + (intensity / 255.0f) * 3.0f; // 1 to 4 full bounces
    float omega = oscillations * 6.283185f;
    float decay_rate = 2.0f + oscillations * 0.5f;

    float decay_term = expf(-decay_rate * t_norm);
    float osc_term = cosf(omega * t_norm);

    // Envelope to kill off ringing smoothly at conclusion
    float kill_env = 1.0f - t_norm;
    kill_env = kill_env * kill_env;

    float fill_frac = 1.0f - (decay_term * osc_term * kill_env);

    int fill_px = (int)(fill_frac * (float)seg_len);

    // ── 3. Clear strip
    // ────────────────────────────────────────────────────────
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // ── 4. Draw filled body
    // ───────────────────────────────────────────────────
    int draw_px = fill_px;
    if (draw_px > seg_len)
      draw_px = seg_len;
    if (draw_px < 0)
      draw_px = 0;

    for (int i = 0; i < draw_px; i++)
      it[seg_start + i] = c;

    // ── 5. Overshoot indicator & Tension line
    // ─────────────────────────────────
    if (fill_px > seg_len) {
      uint8_t over_b = (uint8_t)((fill_px - seg_len) * 40);
      if (over_b > 80)
        over_b = 80;
      it[seg_start + seg_len - 1] = boost(c, over_b);
    }

    if (fill_px > 0 && fill_px < seg_len) {
      int tension_px = (intensity / 32) + 2; // Scales nicely up to 10
      if (fill_px < tension_px)
        tension_px = fill_px;
      for (int g = 0; g < tension_px; g++) {
        int px = fill_px - 1 - g;
        if (px >= 0)
          it[seg_start + px] = dim(c, (uint8_t)(100 - g * (100 / tension_px)));
      }
    }
    break;
  }

  case INTRO_MODE_LITHOGRAPH: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1100;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_intro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Sweep cursor (ease-in-out)
    // ─────────────────────────────────────────
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;
    int sweep_px = (int)(cfx::ease_in_out(prog) * (float)seg_len);
    if (sweep_px > seg_len)
      sweep_px = seg_len;

    // ── 3. Scroll position: 1 pixel per 8 ms  (125 px/sec scanner speed)
    // ──────
    uint32_t scroll = elapsed >> 3;

    // ── 4. Build pattern lookup (segment index → lit or dark)
    // ─────────────────
    //       Each segment has a hash-derived width (1–7 px) and lit/dark state.
    //       We walk forward through segments until we've covered seg_len +
    //       the maximum possible scroll offset we'll ever use.
    //       PATTERN_SLOTS: enough segments to guarantee full coverage.
    const int PATTERN_SLOTS = 128;
    //       Pre-build cumulative start positions and lit flags into stack
    //       arrays. Stack cost: 128 * (2+1) = 384 bytes — acceptable on ESP32.
    uint16_t seg_start_arr[PATTERN_SLOTS];
    bool seg_lit[PATTERN_SLOTS];
    int pattern_total = 0;
    int n_segs = 0;

    for (int s = 0; s < PATTERN_SLOTS; s++) {
      uint32_t h = cfx::knuth32((uint32_t)s * 31u + 7u);
      int width = (int)(h >> 29) + 1; // 1–8 px per segment
      bool lit = (h >> 28) & 1u;      // 50/50 lit vs dark

      seg_start_arr[s] = (uint16_t)pattern_total;
      seg_lit[s] = lit;
      pattern_total += width;
      n_segs = s + 1;

      // Stop once pattern is wide enough to cover strip + full scroll range
      if (pattern_total >= seg_len * 3)
        break;
    }
    if (pattern_total == 0)
      pattern_total = 1;

    // ── 5. Clear strip
    // ────────────────────────────────────────────────────────
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // ── 6. Draw barcode behind the sweep cursor
    // ────────────────────────────────
    for (int i = 0; i < sweep_px; i++) {
      // Virtual position in the pattern (wrapping)
      uint32_t vpos = ((uint32_t)i + scroll) % (uint32_t)pattern_total;

      // Binary search would be cleaner for large strips but linear scan over
      // PATTERN_SLOTS is fast enough: ~128 iterations, pure integer arithmetic.
      bool is_lit = false;
      for (int s = 0; s < n_segs - 1; s++) {
        if (vpos >= seg_start_arr[s] && vpos < seg_start_arr[s + 1]) {
          is_lit = seg_lit[s];
          break;
        }
      }
      // Last segment covers the remainder
      if (vpos >= (uint32_t)seg_start_arr[n_segs - 1])
        is_lit = seg_lit[n_segs - 1];

      // Razor-sharp: strictly 0 or 255 — no dim(), no interpolation
      it[seg_start + i] = is_lit ? c : Color::BLACK;
    }

    // ── 7. Scan cursor: single bright pixel at the leading edge
    // ───────────────
    if (sweep_px < seg_len)
      it[seg_start + sweep_px] = boost(c, 80);
    break;
  }

  case INTRO_MODE_TIDAL_SURGE: {
    // Tidal Surge intro: oscillates between waypoints [30, 20, 50, 70, 40,
    // 100]. Snaps to WP[0]=30% on the first frame, then interpolates through 5
    // transitions. The strip is filled solid up to `lit` pixels; the rest are
    // black. No leading-pixel sweep — personality is tidal, not wipe.
    static constexpr uint8_t WAYPOINTS[] = {30, 20, 50, 70, 40, 100};
    static constexpr uint8_t NUM_WAYPOINTS = 6;
    static constexpr uint8_t NUM_SEGS = NUM_WAYPOINTS - 1; // 5 transitions

    uint32_t seg_dur = (NUM_SEGS > 0) ? (duration / NUM_SEGS) : 1;
    if (seg_dur == 0)
      seg_dur = 1;

    // Which inter-waypoint segment are we in?
    uint8_t seg_idx = (uint8_t)std::min((uint32_t)(elapsed / seg_dur),
                                        (uint32_t)(NUM_SEGS - 1));
    uint32_t seg_elapsed = elapsed - (uint32_t)seg_idx * seg_dur;

    float pct;
    if (elapsed < seg_dur / 4) {
      // First quarter of first segment: snap to WP[0] to avoid 0->30% wipe look
      pct = (float)WAYPOINTS[0];
    } else {
      uint8_t prev_pct = WAYPOINTS[seg_idx];
      uint8_t curr_pct = WAYPOINTS[seg_idx + 1];
      float t = std::min(1.0f, (float)seg_elapsed / (float)seg_dur);
      pct = prev_pct + (curr_pct - (int)prev_pct) * t;
    }

    int lit = (int)((pct / 100.0f) * (float)seg_len);
    if (lit > seg_len)
      lit = seg_len;
    if (lit < 0)
      lit = 0;

    for (int i = 0; i < seg_len; i++) {
      int idx = reverse ? (seg_len - 1 - i) : i;
      it[seg_start + idx] = (i < lit) ? c : Color::BLACK;
    }
    break;
  }

  case INTRO_MODE_IMPACT_FLARE: {
    // Phase 1: Realistic Meteor (0.0 -> 0.75 progress)
    // Phase 2: Impact / Reverse Fill (0.75 -> 1.0 progress)
    if (progress <= 0.75f) {
      float p1 = progress / 0.75f;
      float exact_lead = p1 * (float)seg_len;
      int lead = (int)exact_lead;
      if (lead >= seg_len)
        lead = seg_len - 1;

      // Track the head for milestones (0-100% strip)
      if (act_->runner)
        act_->runner->current_leading_pixel = lead;

      // --- 1. DECAY LOOP (Deterministic Meteor Trail) ---
      // Retention: shifted range (approx 140..210) for faster fade
      uint8_t retention = 140 + ((uint16_t)act_->active_intro_intensity * 70 / 255);
      for (int i = 0; i < seg_len; i++) {
        int idx = reverse ? (seg_len - 1 - i) : i;
        int global_idx = seg_start + idx;

        // Clear-Ahead: explicitly kill light ahead of the bead to prevent persistence
        if (i > lead) {
          it[global_idx] = Color::BLACK;
          continue;
        }

        Color cur = it[global_idx].get();
        // Multiplicative decay every frame
        it[global_idx] = Color(scale8(cur.r, retention), scale8(cur.g, retention),
                               scale8(cur.b, retention), scale8(cur.w, retention));
      }

      // --- 2. DRAW METEOR HEAD (with Energy Boost) ---
      int meteor_size = 1 + seg_len / 40; // Small sharp head
      for (int j = 0; j < meteor_size; j++) {
        int i = lead + j;
        if (i < seg_len) {
          int idx = reverse ? (seg_len - 1 - i) : i;
          int global_idx = seg_start + idx;
          // Energy Boost: Add white to the primary monochromatic color
          it[global_idx] = Color(qadd8(c.r, 80), qadd8(c.g, 80), qadd8(c.b, 80),
                                 qadd8(c.w, 80));
        }
      }
    } else {
      // Impact Phase: Reverse fill from end back to start (3x meteor speed)
      float p2 = (progress - 0.75f) / 0.25f;
      int fill_head = (int)((1.0f - p2) * (float)seg_len);
      if (fill_head < 0)
        fill_head = 0;

      // Milestone stays pegged at 100% during impact phase
      if (act_->runner)
        act_->runner->current_leading_pixel = seg_len - 1;

      for (int i = 0; i < seg_len; i++) {
        int idx = reverse ? (seg_len - 1 - i) : i;
        int global_idx = seg_start + idx;
        it[global_idx] = (i >= fill_head) ? c : Color::BLACK;
      }
    }
    break;
  }

  case INTRO_MODE_NONE:
  default:
    for (int i = 0; i < seg_len; i++) {
      it[seg_start + i] = Color::BLACK;
    }
    break;
  }

  // (CFX-035b: removed the legacy current_leading_pixel overwrite here so
  // that the main runner's actual leading pixel remains clean for detection
  // in apply(). Our new intro_suppresses_milestones tracking handles all this
  // natively).
}

bool CFXAddressableLightEffect::run_outro_frame(light::AddressableLight &it,
                                                CFXRunner *runner) {
  if (runner == nullptr)
    return true;

  auto *state = this->get_light_state();
  if (state != nullptr && state->remote_values.is_on()) {
    // Light turned back ON during Outro!
    // Return true immediately so the callback is released and
    // the captured runner is safely destroyed.
    return true;
  }

  uint32_t duration_ms = act_->active_outro_duration_ms;

  uint32_t elapsed = (uint32_t)(millis_64() - act_->outro_start_time);
  float progress = (float)elapsed / (duration_ms > 0 ? duration_ms : 1);
  if (progress > 1.0f)
    progress = 1.0f;

  float fade_scaler = 1.0f - progress;

  // 1. Advance the underlying effect in the background
  runner->service();

  // 1b. CRITICAL: Stop ESPHome's internal transition from dimming our
  // pixels! Non-segmented: ESPHome is actively fading brightness to 0.0f.
  // We must
  //   temporarily force it to 1.0f so the outro renders visibly.
  // Segmented (virtual segments): Each segment has its OWN ColorCorrection
  //   that is independent from the Master. Skip the Master brightness hack
  //   to avoid corrupting it for other concurrently-active segments.
  float original_brightness = 0.0f;
  float user_brightness = 1.0f;
  auto *ls = this->get_light_state();
  if (ls != nullptr) {
    original_brightness = ls->current_values.get_brightness();
    // Capture user's intended brightness from initial state BEFORE override
    // During outro, remote_values is usually 0, so we use the snapshot
    // taken at start_outro
    user_brightness = act_->active_outro_brightness;
    if (user_brightness < 0.01f)
      user_brightness = 0.01f;
    if (!this->is_virtual_segment_) {
      ls->current_values.set_brightness(1.0f);
    }
  }

  // 2. Render background frame onto the output buffer (scaled by user
  // brightness)
  int seg_len = runner->_segment.length();
  int seg_start = (it.size() == seg_len) ? 0 : runner->_segment.start;
  int seg_stop = seg_start + seg_len;

  // Define helper lambdas at function scope to avoid redeclaration issues and
  // fix missing symbol errors in newer cases like Interference.
  auto dim = [](Color col, uint8_t f) -> Color {
    return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                 (uint8_t)(((uint16_t)col.g * f) >> 8),
                 (uint8_t)(((uint16_t)col.b * f) >> 8),
                 (uint8_t)(((uint16_t)col.w * f) >> 8));
  };
  auto boost = [](Color col, uint8_t b) -> Color {
    return Color((uint8_t)((int)col.r + b > 255 ? 255 : col.r + b),
                 (uint8_t)((int)col.g + b > 255 ? 255 : col.g + b),
                 (uint8_t)((int)col.b + b > 255 ? 255 : col.b + b),
                 (uint8_t)((int)col.w + b > 255 ? 255 : col.w + b));
  };

  for (int i = 0; i < seg_len; i++) {
    int global_idx = seg_start + i;
    uint32_t c = runner->_segment.getPixelColor(i);
    uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * user_brightness);
    uint8_t g = (uint8_t)(((c >> 8) & 0xFF) * user_brightness);
    uint8_t b = (uint8_t)((c & 0xFF) * user_brightness);
    uint8_t w = (uint8_t)(((c >> 24) & 0xFF) * user_brightness);

    // BUG 13 FIX: Apply force_white to Outro transitions
    if (act_->active_outro_force_white)
      cfx::apply_force_white(r, g, b, w);

    it[global_idx] = Color(r, g, b, w);
  }

  // Restore the scaling factor so we don't permanently corrupt the
  // LightState
  if (ls != nullptr && !this->is_virtual_segment_) {
    ls->current_values.set_brightness(original_brightness);
  }

  uint8_t mode = act_->active_outro_mode;

  bool reverse = act_->active_outro_mirror;

  bool symmetry = false;
  bool quadrant = false;
  if (mode == INTRO_MODE_CENTER) {
    symmetry = true;
    mode = INTRO_MODE_WIPE;
  } else if (mode == INTRO_MODE_QUADRANT) {
    quadrant = true;
    mode = INTRO_MODE_WIPE;
  }

  switch (mode) {
  case INTRO_MODE_WIPE: {
    int logical_len = seg_len;
    if (symmetry)
      logical_len = seg_len / 2;
    if (quadrant)
      logical_len = (seg_len + 3) / 4;

    // Intensity defines blur radius (up to 50% of the strip)
    // Use the cached active_outro_intensity_ because controller_ is null
    // during Outro
    float blur_percent = (act_->active_outro_intensity / 255.0f) * 0.5f;

    int blur_radius = (int)(logical_len * blur_percent);
    float progress_erasing = 1.0f - progress;
    float exact_lead = progress_erasing * (logical_len + blur_radius);
    int lead = (int)exact_lead;

    for (int i = 0; i < logical_len; i++) {

      float alpha =
          0.0f; // 0.0 means ERASED (black), 1.0 means KEPT (background)

      if (!reverse) {
        if (i <= lead - blur_radius) {
          alpha = 1.0f;
        } else if (i <= lead && blur_radius > 0) {
          float distance_into_blur = exact_lead - i;
          alpha = distance_into_blur / blur_radius;
          if (alpha < 0.0f)
            alpha = 0.0f;
          if (alpha > 1.0f)
            alpha = 1.0f;
        }
      } else {
        int rev_i = logical_len - 1 - i;
        if (rev_i <= lead - blur_radius) {
          alpha = 1.0f;
        } else if (rev_i <= lead && blur_radius > 0) {
          float distance_into_blur = exact_lead - rev_i;
          alpha = distance_into_blur / blur_radius;
          if (alpha < 0.0f)
            alpha = 0.0f;
          if (alpha > 1.0f)
            alpha = 1.0f;
        }
      }

      // Helper to apply alpha to a specific index relative to segment start
      auto apply_alpha = [&](int idx) {
        if (idx >= 0 && idx < seg_len) {
          int global_idx = seg_start + idx;
          if (alpha <= 0.0f) {
            it[global_idx] = Color::BLACK;
          } else if (alpha < 1.0f) {
            Color c = it[global_idx].get();
            it[global_idx] =
                Color((uint8_t)(c.r * alpha), (uint8_t)(c.g * alpha),
                      (uint8_t)(c.b * alpha), (uint8_t)(c.w * alpha));
          }
        }
      };

      if (quadrant) {
        // Quadrant Logic: 4 wings converging from edges/midpoints to
        // centers
        apply_alpha(i);
        apply_alpha((seg_len / 2) - 1 - i);
        apply_alpha((seg_len / 2) + i);
        apply_alpha(seg_len - 1 - i);
      } else {
        apply_alpha(i);
        if (symmetry) {
          apply_alpha(seg_len - 1 - i);
        }
      }
    }

    if (symmetry && (seg_len % 2 != 0)) {
      int mid = seg_start + (seg_len / 2);
      bool fill_center = (lead > 0);
      if (!fill_center) {
        it[mid] = Color::BLACK;
      }
    }
    break;
  }
  case INTRO_MODE_DROPPING: {
    // ── 1. Duration fetch ───────────────────────────────────────────────────
    uint32_t duration = duration_ms;
    if (duration == 0)
      duration = 1;

    float prog = progress;

    // ── 2. Drain level: ease-in cubic ───────────────────────────────────────
    float drain_eased = prog * prog * prog; // cubic ease-in
    int fill_level = seg_len - (int)(drain_eased * (float)seg_len);
    if (fill_level < 0)
      fill_level = 0;

    // ── 3. Color helpers (Monochromatic) ────────────────────────────────────
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };

    auto boost = [](Color col, uint8_t b) -> Color {
      return Color((uint8_t)((int)col.r + b > 255 ? 255 : col.r + b),
                   (uint8_t)((int)col.g + b > 255 ? 255 : col.g + b),
                   (uint8_t)((int)col.b + b > 255 ? 255 : col.b + b),
                   (uint8_t)((int)col.w + b > 255 ? 255 : col.w + b));
    };

    // ── 4. Clear above fill level ────────────────────────────────────────────
    for (int i = fill_level; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // ── 5. Draw water body with depth shading + surface shimmer ─────────────
    uint32_t shimmer_period =
        (uint32_t)(120.0f * (1.0f - prog * 0.6f)); // 120→48 ms
    if (shimmer_period < 40)
      shimmer_period = 40;
    uint32_t shimmer_t = elapsed % shimmer_period;
    uint8_t shimmer = (shimmer_t < shimmer_period / 2)
                          ? (uint8_t)(shimmer_t * 240 / (shimmer_period / 2))
                          : (uint8_t)((shimmer_period - shimmer_t) * 240 /
                                      (shimmer_period / 2));

    for (int i = 0; i < fill_level; i++) {
      Color base = it[seg_start + i].get();
      int dist_from_surface = (fill_level - 1) - i;
      if (dist_from_surface == 0)
        base = boost(base, shimmer);
      else if (dist_from_surface == 1)
        base = boost(base, shimmer / 3);
      it[seg_start + i] = base;
    }

    // ── 6. Rising bubbles ────────────────────────────────────────────────────
    if (fill_level > 1 && prog > 0.1f) {
      const int NUM_BUBBLES = 3;
      uint32_t bubble_period = (uint32_t)(200.0f * (1.0f - prog * 0.5f));
      if (bubble_period < 80)
        bubble_period = 80;

      for (int b = 0; b < NUM_BUBBLES; b++) {
        uint32_t phase = (uint32_t)(b * bubble_period / NUM_BUBBLES);
        uint32_t t = (elapsed + phase) % bubble_period;
        float bubble_t =
            (float)t / (float)bubble_period; // 0=bottom → 1=surface

        float rise = 1.0f - (1.0f - bubble_t) * (1.0f - bubble_t);

        int bottom_bound = (int)(fill_level * 0.10f);
        int span = (fill_level - 1) - bottom_bound;
        if (span <= 1)
          continue;

        int bubble_px = bottom_bound + (int)(rise * (float)span);
        if (bubble_px < 0 || bubble_px >= fill_level)
          continue;

        it[seg_start + bubble_px] = dim(it[seg_start + bubble_px].get(), 40);

        if (bubble_t > 0.88f && fill_level > 0) {
          float pt = (bubble_t - 0.88f) / 0.12f;
          float env = (pt < 0.5f) ? (pt / 0.5f) : ((1.0f - pt) / 0.5f);
          uint8_t pop_b = (uint8_t)(env * 160.0f);
          it[seg_start + fill_level - 1] =
              boost(it[seg_start + fill_level - 1].get(), pop_b);
        }
      }
    }
    break;
  }
  case INTRO_MODE_GLITTER: {
    // Glitter Outro: More and more black pixels appear
    uint8_t threshold = (uint8_t)(progress * 255.0f);
    for (int i = 0; i < seg_len; i++) {
      int global_idx = seg_start + i;
      uint16_t hash = (global_idx * 33) + (global_idx * global_idx);
      uint8_t val = hash % 256;
      if (val < threshold) {
        it[global_idx] = Color::BLACK;
      }
      // Else: Keep 100% brightness of the underlying frame
    }
    break;
  }
  case INTRO_MODE_TWIN_PULSE: {
    // Twin Pulse Outro (Option B: Forward Chase)
    // The formation (Two pulses + trailing void) races forward.
    // Progress 0.0: Full light.
    // Progress 1.0: Full black.

    float length = (float)seg_len;
    float c_size = length * 0.08f;
    if (c_size < 3.0f)
      c_size = 3.0f;
    float short_gap = length * 0.12f;
    if (short_gap < 1.0f)
      short_gap = 1.0f;
    float long_gap = length * 0.10f;
    if (long_gap < 1.0f)
      long_gap = 1.0f;
    float wipe_fade = length * 0.05f;
    if (wipe_fade < 1.0f)
      wipe_fade = 1.0f;

    // Layout (identical to intro but relative to the "Eraser Head")
    // Leading edge (Eraser Head) is the front of the formation.
    float c1_front = 0.0f;
    float c1_back = c1_front - c_size;
    float c2_front = c1_back - short_gap;
    float c2_back = c2_front - c_size;
    float w_front = c2_back - long_gap;
    float w_solid = w_front - wipe_fade;

    // Total distance to cover so the void (w_solid) clears the strip
    float total_distance = length - w_solid;
    float head_pos = (progress * total_distance) + c1_front;

    for (int i = 0; i < seg_len; i++) {
      int idx = reverse ? (seg_len - 1 - i) : i;
      int global_idx = seg_start + idx;
      float fi = (float)idx;
      float relative_pos = head_pos - fi;
      float alpha = 1.0f; // 1.0 = Keep Original Color, 0.0 = Black

      if (relative_pos < 0.0f) {
        // Ahead of the eraser formation: Smoothly fade down from full
        // background
        if (relative_pos > -wipe_fade) {
          alpha = (-relative_pos) / wipe_fade; // relative_pos is negative
        } else {
          alpha = 1.0f;
        }
      } else if (relative_pos <= -c1_back) {
        // Cursor 1: Pulse of Light
        float c_radius = c_size / 2.0f;
        float center_pos = (-c1_front + -c1_back) / 2.0f;
        float dist_to_center = abs(relative_pos - center_pos);
        if (dist_to_center < c_radius) {
          // Pulse opacity (1.0 at center, fading down to 0.0 at edges)
          alpha = 1.0f - (dist_to_center / c_radius);
        } else {
          alpha = 0.0f;
        }
      } else if (relative_pos < -c2_front) {
        // Short Gap (Black)
        alpha = 0.0f;
      } else if (relative_pos <= -c2_back) {
        // Cursor 2: Pulse of Light
        float c_radius = c_size / 2.0f;
        float center_pos = (-c2_front + -c2_back) / 2.0f;
        float dist_to_center = abs(relative_pos - center_pos);
        if (dist_to_center < c_radius) {
          alpha = 1.0f - (dist_to_center / c_radius);
        } else {
          alpha = 0.0f;
        }
      } else if (relative_pos < -w_front) {
        // Long Gap (Black)
        alpha = 0.0f;
      } else {
        // The Void behind the formation
        alpha = 0.0f;
      }

      // Apply alpha to existing buffer color
      if (alpha <= 0.0f) {
        it[global_idx] = Color::BLACK;
      } else if (alpha < 1.0f) {
        Color cur = it[global_idx].get();
        it[global_idx] =
            Color((uint8_t)(cur.r * alpha), (uint8_t)(cur.g * alpha),
                  (uint8_t)(cur.b * alpha), (uint8_t)(cur.w * alpha));
      }
    }
    break;
  }
  case INTRO_MODE_HYDRAULICS: {
    uint64_t now_ms = millis_64();
    if (act_->hydraulics_last_ms == 0)
      act_->hydraulics_last_ms = now_ms;
    uint32_t dt_ms = (uint32_t)(now_ms - act_->hydraulics_last_ms);
    if (dt_ms == 0)
      dt_ms = 1;
    act_->hydraulics_last_ms = now_ms;

    float dt = dt_ms / 1000.0f;
    float intensity_val = act_->active_outro_intensity / 255.0f;
    float target_l = 0.0f;
    float damping = 1.0f + (intensity_val * 4.0f);
    float force_mag = 20.0f;

    float force = (target_l - act_->hydraulics_fluid_level) * force_mag;
    float accel = force - (damping * act_->hydraulics_fluid_velocity);

    float old_level = act_->hydraulics_fluid_level;
    act_->hydraulics_fluid_velocity += accel * dt;
    act_->hydraulics_fluid_level += act_->hydraulics_fluid_velocity * dt;

    if (act_->hydraulics_fluid_level < 0.0f) {
      act_->hydraulics_fluid_level = 0.0f;
      act_->hydraulics_fluid_velocity = 0.0f;
    }

    // Drops cling more based on intensity
    if (act_->hydraulics_fluid_level < old_level &&
        act_->hydraulics_particle_count <
            MAX_HYDRAULICS_PARTICLES) { // audit 3.3
      if ((cfx::hw_random8(100)) <
          (10 + (int)(intensity_val * 25))) { // CFX-023
        act_->hydraulics_particles[act_->hydraulics_particle_count++] = {
            old_level, 0.0f, true};
      }
    }

    // Strict Buffer Clearing
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // Render Shimmering Fluid Mass (Coherent Waves)
    int floor_level = (int)act_->hydraulics_fluid_level;
    float wave_time = now_ms * 0.005f;
    for (int i = 0; i < floor_level; i++) {
      if (i < seg_len) {
        float wave1 = sinf(i * 0.5f - wave_time);
        float wave2 = sinf(i * 0.8f - (wave_time * 1.3f));
        float liquid_noise = (wave1 + wave2) * 0.15f;
        float brightness = 0.7f + liquid_noise;
        if (brightness > 1.0f)
          brightness = 1.0f;
        uint8_t b = (uint8_t)(255 * brightness);
        uint8_t r = b, g = b, b_val = b, w = b;
        if (act_->active_force_white)
          cfx::apply_force_white(r, g, b_val, w);
        it[seg_start + i] = Color(r, g, b_val, w);
      }
    }
    if (floor_level < seg_len && floor_level >= 0) {
      float fraction = act_->hydraulics_fluid_level - floor_level;
      uint8_t b = (uint8_t)(255 * (0.75f + fraction * 0.25f));
      uint8_t r = b, g = b, b_val = b, w = b;
      if (act_->active_force_white)
        cfx::apply_force_white(r, g, b_val, w);
      it[seg_start + floor_level] = Color(r, g, b_val, w);
    }

    float gravity = 25.0f + (intensity_val * 20.0f);
    for (uint8_t _pi = 0; _pi < act_->hydraulics_particle_count;
         _pi++) { // audit 3.3
      auto &p = act_->hydraulics_particles[_pi];
      if (!p.active)
        continue;
      p.vel -= gravity * dt;
      p.pos += p.vel * dt;
      if (p.pos < 0.0f) {
        p.active = false;
        continue;
      }
      if (p.pos < act_->hydraulics_fluid_level) {
        p.active = false;
        continue;
      }
      int p_idx = (int)p.pos;
      if (p_idx >= 0 && p_idx < seg_len) {
        uint8_t r = 255, g = 255, b_val = 255, w = 255;
        if (act_->active_force_white)
          cfx::apply_force_white(r, g, b_val, w);
        it[seg_start + p_idx] = Color(r, g, b_val, w);
      }
    }
    // Compact inactive particles (audit 3.3: replaces erase/remove_if on
    // vector)
    {
      uint8_t w = 0;
      for (uint8_t _pi = 0; _pi < act_->hydraulics_particle_count; _pi++)
        if (act_->hydraulics_particles[_pi].active)
          act_->hydraulics_particles[w++] = act_->hydraulics_particles[_pi];
      act_->hydraulics_particle_count = w;
    }

    if (act_->hydraulics_fluid_level <= 0.01f &&
        act_->hydraulics_particle_count == 0) { // audit 3.3
      for (int i = 0; i < seg_len; i++)
        it[seg_start + i] = Color::BLACK;
      return true;
    }
    if (millis_64() - act_->outro_start_time >
        act_->active_outro_duration_ms + 2000) {
      for (int i = 0; i < seg_len; i++)
        it[seg_start + i] = Color::BLACK;
      return true;
    }
    return false;
  }
  case INTRO_MODE_TIDAL_SURGE: {
    // Outro: reversed waypoints — mirrored personality of the intro.
    // Waypoints: [100, 40, 70, 50, 20, 30, 0]
    //   - Mirrors the intro's organic oscillation in reverse.
    //   - Ends at 0 so the strip drains smoothly to black instead of
    //     cutting off abruptly when progress >= 1.0.
    // NOTE: Modulates the background pixels already rendered above.
    static constexpr uint8_t OUTRO_WAYPOINTS[] = {100, 40, 70, 50, 20, 30, 0};
    static constexpr uint8_t NUM_OUTRO_WP = 7;
    // 6 inter-waypoint transitions
    static constexpr uint8_t NUM_OUTRO_SEGS = NUM_OUTRO_WP - 1;

    uint32_t seg_dur =
        (NUM_OUTRO_SEGS > 0) ? (duration_ms / NUM_OUTRO_SEGS) : 1;
    if (seg_dur == 0)
      seg_dur = 1;

    uint8_t seg_idx = (uint8_t)std::min((uint32_t)(elapsed / seg_dur),
                                        (uint32_t)(NUM_OUTRO_SEGS - 1));

    uint32_t seg_elapsed = elapsed - (uint32_t)seg_idx * seg_dur;

    uint8_t prev_pct = OUTRO_WAYPOINTS[seg_idx];
    uint8_t curr_pct = OUTRO_WAYPOINTS[seg_idx + 1];

    float t = (seg_dur > 0)
                  ? std::min(1.0f, (float)seg_elapsed / (float)seg_dur)
                  : 1.0f;
    float pct = prev_pct + (curr_pct - (int)prev_pct) * t;
    uint16_t lit = (uint16_t)((pct / 100.0f) * (float)seg_len);
    if (lit > (uint16_t)seg_len)
      lit = (uint16_t)seg_len;

    for (int i = 0; i < seg_len; i++) {
      int idx = reverse ? (seg_len - 1 - i) : i;
      if (i >= lit) {
        it[seg_start + idx] = Color::BLACK;
      }
      // else: keep the background pixel already written by the preamble
    }
    break;
  }
  case INTRO_MODE_ASSEMBLY: {
    // ── 1. Mirroring & Duration
    // ───────────────────────────────────────────────
    bool reverse = act_->active_outro_mirror;
    float total_duration_ms = (float)act_->active_outro_duration_ms;
    if (total_duration_ms <= 0.0f)
      total_duration_ms = 1000.0f;

    // ── 1.5 Cache population (First frame only)
    // ───────────────────────────────
    if (act_->outro_color_cache.empty()) {
      for (int i = 0; i < seg_len; i++) {
        uint32_t c_raw = runner->_segment.getPixelColor(i);
        act_->outro_color_cache.push_back(Color(
            (uint8_t)((c_raw >> 16) & 0xFF), (uint8_t)((c_raw >> 8) & 0xFF),
            (uint8_t)(c_raw & 0xFF), (uint8_t)((c_raw >> 24) & 0xFF)));
      }
    }

    // ── 2. Block Definition (Deterministic matching intro)
    // ────────────────────
    struct BlockInfo {
      int target_pos;
      int size;
    };
    std::vector<BlockInfo> blocks;
    int current_fill = 0;

    auto block_sz = [](int idx) -> int {
      uint32_t h = (uint32_t)idx * 2654435761u; // Knuth hash
      return (int)(h >> 30) + 1;                // 1 to 4 pixels
    };

    while (current_fill < seg_len) {
      int b_size = block_sz(blocks.size());
      if (current_fill + b_size > seg_len)
        b_size = seg_len - current_fill;
      blocks.push_back({current_fill, b_size});
      current_fill += b_size;
    }

    // ── 3. Timing setup
    // ───────────────────────────────────────────────────────
    int num_blocks = blocks.size();
    float f_elapsed = (float)elapsed;
    float fall_duration = total_duration_ms * 0.4f;
    float total_stagger = total_duration_ms * 0.6f;
    float block_interval =
        (num_blocks > 1) ? (total_stagger / (float)(num_blocks - 1)) : 0.0f;

    // ── 4. Render loop
    // ──────────────────────────────────────────────────────── Clear strip
    // first
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    for (int i = 0; i < num_blocks; i++) {
      const auto &b =
          blocks[i]; // Peel from index 0 first (Eating from bottom if !reverse)
      float start_time = (float)i * block_interval;

      int target_idx =
          reverse ? (seg_len - b.size - b.target_pos) : b.target_pos;

      if (f_elapsed < start_time) {
        // Still part of the light - Draw from cache
        for (int j = 0; j < b.size; j++) {
          int px = target_idx + j;
          // Logical index in cache is target_idx + j if we didn't mirror the
          // cache itself Wait, target_idx is ALREADY mirrored if 'reverse' We
          // need the COLOR from the original pixel position. If reverse=false:
          // b.target_pos + j If reverse=true: (seg_len - b.size - b.target_pos)
          // + j
          int cache_idx = target_idx + j;
          if (px >= 0 && px < seg_len && cache_idx >= 0 &&
              (size_t)cache_idx < act_->outro_color_cache.size()) {
            Color c_cached = act_->outro_color_cache[cache_idx];
            uint8_t r = c_cached.r, g = c_cached.g, b_val = c_cached.b,
                    w = c_cached.w;
            if (act_->active_outro_force_white)
              cfx::apply_force_white(r, g, b_val, w);
            it[seg_start + px] = Color(r, g, b_val, w);
          }
        }
      } else if (f_elapsed < start_time + fall_duration) {
        // Falling away
        float b_prog = (f_elapsed - start_time) / fall_duration;
        float fall_prog = b_prog * b_prog;

        // Falling direction: move from current position to "the exits"
        // !reverse: fall DOWN to exit (index < 0)
        // reverse: move UP to exit (index >= seg_len)
        int offset = 10;
        int f_start = target_idx;
        int f_end = reverse ? (seg_len + offset) : (-b.size - offset);
        int span = f_end - f_start;
        int current_pos = f_start + (int)(fall_prog * (float)span);

        // Draw falling block with dimming
        float dim_factor = 1.0f - (b_prog * 0.7f);
        for (int j = 0; j < b.size; j++) {
          int px = current_pos + j;
          int cache_idx = target_idx + j;
          if (px >= 0 && px < seg_len && cache_idx >= 0 &&
              (size_t)cache_idx < act_->outro_color_cache.size()) {
            Color c_cached = act_->outro_color_cache[cache_idx];
            uint8_t r = (uint8_t)(c_cached.r * dim_factor);
            uint8_t g = (uint8_t)(c_cached.g * dim_factor);
            uint8_t b_val = (uint8_t)(c_cached.b * dim_factor);
            uint8_t w = (uint8_t)(c_cached.w * dim_factor);

            if (act_->active_outro_force_white)
              cfx::apply_force_white(r, g, b_val, w);
            it[seg_start + px] = Color(r, g, b_val, w);
          }
        }
      }
    }
    break;
  }
  case INTRO_MODE_INERTIA_SWEEP: {
    // Outro: Decelerate — fill drains from right with eased sweep
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };
    auto ease_in_out = [](float p) -> float {
      return p * p * (3.0f - 2.0f * p);
    };

    float eased = ease_in_out(progress);
    int keep_px = seg_len - (int)(eased * (float)seg_len);
    if (keep_px < 0)
      keep_px = 0;

    for (int i = 0; i < seg_len; i++) {
      if (i >= keep_px) {
        it[seg_start + i] = Color::BLACK;
      } else {
        // Dim the kept body slightly by remaining fraction
        uint8_t fade_b = (uint8_t)(255.0f * (1.0f - progress * 0.4f));
        Color orig = it[seg_start + i].get();
        it[seg_start + i] = dim(orig, fade_b);
      }
    }
    break;
  }
  case INTRO_MODE_SONAR_REVEAL: {
    // Outro: Sonar Fade — scanner sweeps while floor dims to black
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };

    const int NUM_SWEEPS = 3;
    float sweep_dur = (float)duration_ms / (float)NUM_SWEEPS;

    int sweep_idx = (int)((float)elapsed / sweep_dur);
    if (sweep_idx >= NUM_SWEEPS)
      sweep_idx = NUM_SWEEPS - 1;
    float within = ((float)elapsed - (float)sweep_idx * sweep_dur) / sweep_dur;

    float floor_fade = 1.0f - progress;
    uint8_t floor_b = (uint8_t)(floor_fade * 255.0f);

    float scan_t = (sweep_idx % 2 == 0) ? within : (1.0f - within);
    int scan_px = (int)(scan_t * (float)(seg_len - 1));

    int scan_w = seg_len / 20;
    if (scan_w < 3)
      scan_w = 3;

    for (int i = 0; i < seg_len; i++) {
      int dist = i - scan_px;
      if (dist < 0)
        dist = -dist;

      Color orig = it[seg_start + i].get();
      uint8_t beam_b = 0;
      if (dist < scan_w)
        beam_b = (uint8_t)(255 - (dist * 255 / scan_w));

      uint8_t final_b = beam_b > floor_b ? beam_b : floor_b;
      it[seg_start + i] = dim(orig, final_b);
    }
    break;
  }
  case INTRO_MODE_VENETIAN: {
    // Outro: Close Blinds — evens fade first, then odds
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };
    auto ease_in_out = [](float p) -> float {
      return p * p * (3.0f - 2.0f * p);
    };

    uint8_t even_b, odd_b;
    if (progress <= 0.5f) {
      float p1 = progress * 2.0f;
      float e1 = ease_in_out(p1);
      even_b = (uint8_t)((1.0f - e1) * 255.0f);
      odd_b = 255;
    } else {
      float p2 = (progress - 0.5f) * 2.0f;
      float e2 = ease_in_out(p2);
      even_b = 0;
      odd_b = (uint8_t)((1.0f - e2) * 255.0f);
    }

    for (int i = 0; i < seg_len; i++) {
      uint8_t b = (i % 2 == 0) ? even_b : odd_b;
      Color orig = it[seg_start + i].get();
      it[seg_start + i] = dim(orig, b);
    }
    break;
  }
  case INTRO_MODE_CRYSTALLIZE: {
    // Outro: Erode — crystal radius contracts from seeds inward
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };

    const int NUM_SEEDS = 4;
    int seeds[NUM_SEEDS];
    for (int s = 0; s < NUM_SEEDS; s++) {
      uint32_t h = (uint32_t)(s * 1234567u + 0xDEADBEEFu) * 2654435761u;
      seeds[s] = (int)((h >> 16) % (uint32_t)seg_len);
    }

    // Contract: radius shrinks from seg_len to 0
    float inv = 1.0f - progress;
    float eased = inv * inv * inv;
    float radius = eased * (float)seg_len;

    const int EDGE_PX = 3;

    for (int i = 0; i < seg_len; i++) {
      int min_dist = seg_len;
      for (int s = 0; s < NUM_SEEDS; s++) {
        int d = i - seeds[s];
        if (d < 0)
          d = -d;
        if (d < min_dist)
          min_dist = d;
      }

      float dist_f = (float)min_dist;
      uint8_t b;
      if (dist_f <= radius - EDGE_PX) {
        b = 255;
      } else if (dist_f <= radius) {
        float t = (radius - dist_f) / (float)EDGE_PX;
        b = (uint8_t)(t * 255.0f);
      } else {
        b = 0;
      }
      Color orig = it[seg_start + i].get();
      it[seg_start + i] = dim(orig, b);
    }
    break;
  }
  case INTRO_MODE_DEEP_BREATHE: {
    // Outro: Exhale — gamma-corrected uniform fade to black
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };

    float inv = 1.0f - progress;
    // Use a gentler fade curve so the light stays visible through the full
    // duration
    uint8_t raw_b = (uint8_t)(inv * 255.0f);
    uint8_t b =
        (raw_b == 0) ? 0 : (uint8_t)(((uint16_t)raw_b * raw_b) >> 8) + 1;

    for (int i = 0; i < seg_len; i++) {
      int half = seg_len / 2;
      int dist = i < half ? i : seg_len - 1 - i;
      int shimmer = (dist * 24 / (half > 0 ? half : 1)) - 12;
      int final_b = (int)b + shimmer;
      if (final_b < 0)
        final_b = 0;
      if (final_b > 255)
        final_b = 255;
      Color orig = it[seg_start + i].get();
      it[seg_start + i] = dim(orig, (uint8_t)final_b);
    }
    break;
  }
  case INTRO_MODE_MORSE: {

    uint32_t unit_ms = 80 + ((255 - act_->active_outro_intensity) * 100 / 255);
    uint32_t elapsed_morse = (uint32_t)(millis_64() - act_->outro_start_time);
    uint32_t current_bit = elapsed_morse / unit_ms;

    uint64_t mask = 0b11101110111000101011101000101011101ULL;
    uint8_t total_bits = 35;

    bool is_on = false;
    if (current_bit < total_bits) {
      is_on = (mask >> (total_bits - 1 - current_bit)) & 0x01;
    } else {
      is_on = false; // Hold OFF after sequence
    }

    if (!is_on) {
      for (int i = 0; i < seg_len; i++) {
        it[seg_start + i] = Color::BLACK;
      }
    }
    break;
  }
  case INTRO_MODE_MOIRE_SHIFT: {
    // Outro: Moire Dissolve — interference pattern fades envelope to black
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };
    // Envelope: smoothstep 1 → 0
    float inv_p = 1.0f - progress;
    float eased_p = inv_p * inv_p * (3.0f - 2.0f * inv_p);
    uint8_t env = (uint8_t)(eased_p * 255.0f);

    uint8_t t1 = (uint8_t)(elapsed >> 4);
    uint8_t t2 = (uint8_t)((elapsed * 3u) >> 5);

    for (int i = 0; i < seg_len; i++) {
      Color orig = it[seg_start + i].get();
      uint8_t s = cfx::sin8((uint8_t)(i * 3u) + t1);
      uint8_t co = cfx::sin8((uint8_t)((uint8_t)(i * 5u) - t2 + 64u));
      uint8_t avg = (uint8_t)(((uint16_t)s + co) >> 1);
      uint8_t gam = (uint8_t)(((uint16_t)avg * avg) >> 8);
      // As progress increases, fade FROM solid (255) TO the interference
      // pattern
      uint8_t blended_gam =
          (uint8_t)(gam + ((255.0f - gam) * (1.0f - progress)));
      uint8_t final_b = (uint8_t)(((uint16_t)blended_gam * env) >> 8);
      it[seg_start + i] = dim(orig, final_b);
    }
    break;
  }
  case INTRO_MODE_RESONANCE_FILL: {
    // Outro: Resonance Drain — sweep retreats from far end with decaying ripple
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };
    auto boost = [](Color col, uint8_t b) -> Color {
      return Color((uint8_t)((int)col.r + b > 255 ? 255 : col.r + b),
                   (uint8_t)((int)col.g + b > 255 ? 255 : col.g + b),
                   (uint8_t)((int)col.b + b > 255 ? 255 : col.b + b),
                   (uint8_t)((int)col.w + b > 255 ? 255 : col.w + b));
    };
    float eased = progress * progress * (3.0f - 2.0f * progress);
    int sweep_pos = seg_len - (int)(eased * (float)seg_len);
    uint8_t inv_prog_b = (uint8_t)((1.0f - progress) * 255.0f);

    // Clear the entire segment every frame to prevent ghosting
    for (int i = 0; i < seg_len; i++) {
      it[seg_start + i] = Color::BLACK;
    }

    // Stable color source
    Color base_c = Color::WHITE;
    if (instance)
      base_c = Color(instance->_segment.colors[0]);

    for (int i = 0; i < sweep_pos; i++) {
      int dist = sweep_pos - 1 - i;
      uint8_t dist_factor =
          (uint8_t)((dist * 255) / (seg_len > 0 ? seg_len : 1));
      uint8_t falloff = (uint8_t)(((uint16_t)dist_factor * inv_prog_b) >> 7);
      uint8_t ripple_raw = cfx::sin8((uint8_t)(dist * 25u));
      int ripple = (((int)ripple_raw - 128) * 2 * inv_prog_b) / 255;
      int bri = 255 - (int)falloff + ripple;
      if (bri < 0)
        bri = 0;
      if (bri > 255)
        bri = 255;
      it[seg_start + i] = dim(base_c, (uint8_t)bri);
    }
    if (sweep_pos > 0) {
      Color edge =
          it[seg_start + sweep_pos - 1 < seg_len ? seg_start + sweep_pos - 1
                                                 : seg_start]
              .get();
      if (sweep_pos - 1 >= 0 && sweep_pos - 1 < seg_len)
        it[seg_start + sweep_pos - 1] = boost(edge, 60);
      if (sweep_pos - 2 >= 0 && sweep_pos - 2 < seg_len)
        it[seg_start + sweep_pos - 2] =
            dim(it[seg_start + sweep_pos - 2].get(), 80);
    }
    break;
  }
  case INTRO_MODE_TELEMETRY: {
    // Outro: Telemetry Retract — dashes wipe from the far end backward
    auto dim = [](Color col, uint8_t f) -> Color {
      return Color((uint8_t)(((uint16_t)col.r * f) >> 8),
                   (uint8_t)(((uint16_t)col.g * f) >> 8),
                   (uint8_t)(((uint16_t)col.b * f) >> 8),
                   (uint8_t)(((uint16_t)col.w * f) >> 8));
    };
    auto boost = [](Color col, uint8_t b) -> Color {
      return Color((uint8_t)((int)col.r + b > 255 ? 255 : col.r + b),
                   (uint8_t)((int)col.g + b > 255 ? 255 : col.g + b),
                   (uint8_t)((int)col.b + b > 255 ? 255 : col.b + b),
                   (uint8_t)((int)col.w + b > 255 ? 255 : col.w + b));
    };
    const int DASH_LEN = 6;
    const int GAP_LEN = 2;

    // ── Two-Stage Outro (Reverse Telemetry) ──────────────────────
    // Stage 1 (0.0 - 0.5): Wipe out the gaps (turn them black)
    // Stage 2 (0.5 - 1.0): Wipe out the blocks/dashes
    float stage1_prog = progress / 0.5f;
    if (stage1_prog > 1.0f)
      stage1_prog = 1.0f;

    float stage2_prog = (progress - 0.5f) / 0.5f;
    if (stage2_prog < 0.0f)
      stage2_prog = 0.0f;

    // Sweep positions for retraction (retracting from end to start)
    int sweep1_pos = seg_len - (int)(stage1_prog * (float)seg_len);
    int sweep2_pos = seg_len - (int)(stage2_prog * (float)seg_len);

    for (int i = 0; i < seg_len; i++) {
      it[seg_start + i] = Color::BLACK;
    }

    Color dash_c = Color::WHITE;
    if (instance)
      dash_c = Color(instance->_segment.colors[0]);

    for (int i = 0; i < seg_len; i++) {
      int phase = i % DASH_LEN;
      bool is_dash = (phase < (DASH_LEN - GAP_LEN));

      if (is_dash) {
        if (i < sweep2_pos) {
          int dist_in_unit = (DASH_LEN - GAP_LEN - 1) - phase;
          uint8_t blade_b = (uint8_t)(255 - dist_in_unit * 18);
          it[seg_start + i] = dim(dash_c, blade_b);
        }
      } else {
        if (i < sweep1_pos) {
          it[seg_start + i] = dash_c;
        }
      }
    }

    // Edges
    if (sweep1_pos > 0 && sweep1_pos < seg_len && stage1_prog < 1.0f) {
      it[seg_start + sweep1_pos] = dim(dash_c, 100);
    }
    if (sweep2_pos > 0 && sweep2_pos < seg_len && stage2_prog < 1.0f) {
      it[seg_start + sweep2_pos] = dim(dash_c, 50);
    }
    break;
  }
  case INTRO_MODE_STELLAR_DUST: {
    // Outro: Stellar Fade — per-pixel breathing collapses envelope to black
    // Lambdas moved to function scope
    float inv = 1.0f - progress;
    float eased = inv * inv * inv; // cubic ease-out
    uint8_t env = (uint8_t)(eased * 255.0f);

    uint8_t t = (uint8_t)(elapsed >> 5);
    for (int i = 0; i < seg_len; i++) {
      Color orig = it[seg_start + i].get();
      uint8_t phase = (uint8_t)((uint32_t)(i) * 2654435761u >> 24);
      uint8_t osc = cfx::sin8(t + phase);
      uint8_t floor_b = (uint8_t)(((uint16_t)80u * env) >> 8);
      uint8_t scaled = (uint8_t)(((uint16_t)osc * 175u) >> 8);
      uint8_t star_b = floor_b + scaled;
      uint8_t final_b = (uint8_t)(((uint16_t)star_b * env) >> 8);
      it[seg_start + i] = dim(orig, final_b);
    }
    break;
  }

  case INTRO_MODE_INTERFERENCE: {
    // Outro: Interference Dissolve — reverse of intro
    float inv_p = 1.0f - progress;
    float eased_p = inv_p * inv_p; // Quadratic ease-in for outro
    uint8_t env = (uint8_t)(eased_p * 255.0f);

    uint8_t t1 = (uint8_t)(elapsed >> 4);
    uint8_t t2 = (uint8_t)((elapsed * 3u) >> 5);

    for (int i = 0; i < seg_len; i++) {
      Color orig = it[seg_start + i].get();
      uint8_t s = cfx::sin8((uint8_t)(i * 3u) + t1);
      uint8_t co = cfx::sin8((uint8_t)((uint8_t)(i * 5u) - t2 + 64u));
      uint8_t avg = (uint8_t)(((uint16_t)s + co) >> 1);
      uint8_t gam = (uint8_t)(((uint16_t)avg * avg) >> 8);

      // As progress increases, fade FROM solid (255) TO the interference
      // pattern
      uint8_t blended_gam =
          (uint8_t)(gam + ((255.0f - gam) * (1.0f - progress)));
      uint8_t final_b = (uint8_t)(((uint16_t)blended_gam * env) >> 8);
      it[seg_start + i] = dim(orig, final_b);
    }
    break;
  }

  case INTRO_MODE_ECLIPSE: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1500;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_outro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Color source (current effect's palette)
    // ─────────────────────────────
    Color c = Color::WHITE;
    if (instance)
      c = Color(instance->_segment.colors[0]);

    // ── 3. Global brightness envelope: falls from 255 → 0, cubic ease-out
    // ─────
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;
    uint8_t env = (uint8_t)((1.0f - cfx::ease_in_out(prog)) * 255.0f);

    // ── 3. Shadow geometry (identical to intro — shadow continues
    // uninterrupted) ─
    const uint8_t BASE_B = 180;
    int shadow_hw = seg_len * 18 / 100;
    if (shadow_hw < 4)
      shadow_hw = 4;
    int shadow_px = (int)((elapsed / 6000.0f) * (float)seg_len) % seg_len;

    // ── 4. Draw strip
    // ─────────────────────────────────────────────────────────
    for (int i = 0; i < seg_len; i++) {
      int dist = i - shadow_px;
      if (dist < 0)
        dist = -dist;
      if (dist > seg_len / 2)
        dist = seg_len - dist;
      if (dist > shadow_hw)
        dist = shadow_hw;

      int d_norm = shadow_hw - dist;
      uint8_t shadow_depth = (uint8_t)((uint32_t)d_norm * d_norm * 215u /
                                       (uint32_t)(shadow_hw * shadow_hw));

      int bri = (int)BASE_B - (int)shadow_depth;
      if (bri < 0)
        bri = 0;

      uint8_t final_b = (uint8_t)(((uint16_t)(uint8_t)bri * env) >> 8);
      it[seg_start + i] = dim(c, final_b);
    }
    break;
  }

  case INTRO_MODE_GAS_DISCHARGE: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1800;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_outro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Color source (current effect's palette)
    // ─────────────────────────────
    Color c = Color::WHITE;
    if (instance)
      c = Color(instance->_segment.colors[0]);

    // ── 3. Phase boundaries
    // ───────────────────────────────────────────────────
    uint32_t p1_end = duration * 12 / 100; // solid with faint ripple
    uint32_t p2_end = duration * 50 / 100; // growing buzz instability
    uint32_t p3_end = duration * 80 / 100; // collapse flashes
    // p4: final dark

    uint8_t brightness = 0;

    if (elapsed < p1_end) {
      // ── Phase 1: Solid with faint high-frequency ripple ───────────────────
      uint8_t t = (uint8_t)(elapsed * 255u / 60u); // period 60 ms
      brightness = 245u + (uint8_t)(((uint16_t)cfx::sin8(t) * 10u) >> 8);
    } else if (elapsed < p2_end) {
      // ── Phase 2: Growing instability — buzz amplitude expands ─────────────
      uint32_t phase_t = elapsed - p1_end;
      uint32_t phase_dur = p2_end - p1_end;
      float norm = (float)phase_t / (float)phase_dur;
      uint8_t amp = (uint8_t)(8.0f + norm * 60.0f);     // 8 → 68
      uint32_t period = 50u - (uint32_t)(norm * 22.0f); // 50 ms → 28 ms
      if (period == 0)
        period = 1;
      uint8_t t = (uint8_t)((phase_t % period) * 255u / period);
      uint8_t base_b = (uint8_t)(255u - (uint32_t)(norm * 50.0f));
      brightness = (uint8_t)((int)base_b - (int)amp +
                             (int)(((uint16_t)cfx::sin8(t) * amp * 2u) >> 8));
      if ((int)brightness > 255)
        brightness = 255;
      if ((int)brightness < 0)
        brightness = 0;
    } else if (elapsed < p3_end) {
      // ── Phase 3: Collapse — sparse final flares, mostly dark ──────────────
      const uint32_t SLOT_MS = 40;
      uint32_t phase_t = elapsed - p2_end;
      uint32_t slot = phase_t / SLOT_MS;
      uint32_t within_slot = phase_t % SLOT_MS;
      uint32_t h = cfx::knuth32(slot * 17u + 3u);
      bool is_flash = (h >> 29) > 6u; // ~25% are flares

      brightness =
          (is_flash && within_slot < 20u) ? (uint8_t)(160u + (h & 0x3Fu)) : 0u;
    } else {
      // ── Phase 4: Dark ─────────────────────────────────────────────────────
      brightness = 0;
    }

    // ── 3. Apply brightness to full strip ────────────────────────────────────
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = dim(c, brightness);
    break;
  }

  case INTRO_MODE_HARMONIC_SETTLE: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1600;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_outro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Color source (current effect's palette)
    // ─────────────────────────────
    Color c = Color::WHITE;
    if (instance)
      c = Color(instance->_segment.colors[0]);

    // ── 3. Spring release — symmetric retreat from seg_len → 0 ───────────────
    float t_norm = (float)elapsed / (float)duration;
    if (t_norm > 1.0f)
      t_norm = 1.0f;

    // Release formula: fill begins full and retreats with a damped dip
    // exp(-3*t) * (1 + cos(π*t)) / 2
    //   t=0:   exp(0)  * (1+1)/2   = 1.0  → full ✓
    //   t=0.5: exp(-1.5) * (1+0)/2 = 0.11 → ~11% (midpoint dip)
    //   t=1.0: exp(-3)  * (1-1)/2  = 0.0  → dark ✓
    float decay_term = expf(-3.0f * t_norm);
    float osc_term = cosf(3.14159265f * t_norm);
    float fill_frac = decay_term * (1.0f + osc_term) * 0.5f;

    int fill_px = (int)(fill_frac * (float)seg_len);
    if (fill_px > seg_len)
      fill_px = seg_len;
    if (fill_px < 0)
      fill_px = 0;

    // ── 3. Clear strip
    // ────────────────────────────────────────────────────────
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // ── 4. Draw remaining body
    // ────────────────────────────────────────────────
    for (int i = 0; i < fill_px; i++)
      it[seg_start + i] = c;

    // ── 5. Retreating tension edge (mirrors intro spring tension line)
    // ─────────
    if (fill_px > 0 && fill_px < seg_len) {
      int tension_px = fill_px < 4 ? fill_px : 4;
      for (int g = 0; g < tension_px; g++) {
        int px = fill_px - 1 - g;
        if (px >= 0)
          it[seg_start + px] = dim(c, (uint8_t)(80 - g * 18));
      }
    }
    break;
  }

  case INTRO_MODE_LITHOGRAPH: {
    // ── 1. Duration fetch
    // ─────────────────────────────────────────────────────
    uint32_t duration = 1100;
    number::Number *dur_num = this->local_inout_duration_();
    if (dur_num == nullptr && act_->controller != nullptr)
      dur_num = act_->controller->get_outro_duration();
    if (auto duration_override =
            this->resolve_inout_duration_override_ms_(dur_num);
        duration_override.has_value())
      duration = duration_override.value();
    if (duration == 0)
      duration = 1;

    // ── 2. Color source (current effect's palette)
    // ─────────────────────────────
    Color c = Color::WHITE;
    if (instance)
      c = Color(instance->_segment.colors[0]);

    // ── 3. Sweep retreats from seg_len → 0
    // ────────────────────────────────────
    float prog = (float)elapsed / (float)duration;
    if (prog > 1.0f)
      prog = 1.0f;
    int remaining = seg_len - (int)(cfx::ease_in_out(prog) * (float)seg_len);
    if (remaining < 0)
      remaining = 0;
    if (remaining > seg_len)
      remaining = seg_len;

    // ── 3. Scroll continues from intro (elapsed offset preserved by caller)
    // ────
    uint32_t scroll = elapsed >> 3;

    // ── 4. Rebuild pattern (same hash — same layout as intro)
    // ─────────────────
    const int PATTERN_SLOTS = 128;
    uint16_t seg_start_arr[PATTERN_SLOTS];
    bool seg_lit[PATTERN_SLOTS];
    int pattern_total = 0;
    int n_segs = 0;

    for (int s = 0; s < PATTERN_SLOTS; s++) {
      uint32_t h = cfx::knuth32((uint32_t)s * 31u + 7u);
      int width = (int)(h >> 29) + 1;
      bool lit = (h >> 28) & 1u;

      seg_start_arr[s] = (uint16_t)pattern_total;
      seg_lit[s] = lit;
      pattern_total += width;
      n_segs = s + 1;

      if (pattern_total >= seg_len * 3)
        break;
    }
    if (pattern_total == 0)
      pattern_total = 1;

    // ── 5. Clear strip
    // ────────────────────────────────────────────────────────
    for (int i = 0; i < seg_len; i++)
      it[seg_start + i] = Color::BLACK;

    // ── 6. Draw barcode in the remaining region
    // ────────────────────────────────
    for (int i = 0; i < remaining; i++) {
      uint32_t vpos = ((uint32_t)i + scroll) % (uint32_t)pattern_total;
      bool is_lit = false;

      for (int s = 0; s < n_segs - 1; s++) {
        if (vpos >= seg_start_arr[s] && vpos < seg_start_arr[s + 1]) {
          is_lit = seg_lit[s];
          break;
        }
      }
      if (vpos >= (uint32_t)seg_start_arr[n_segs - 1])
        is_lit = seg_lit[n_segs - 1];

      it[seg_start + i] = is_lit ? c : Color::BLACK;
    }

    // ── 7. Retreating scan cursor
    // ─────────────────────────────────────────────
    if (remaining > 0)
      it[seg_start + remaining - 1] = boost(c, 80);
    break;
  }

  case OUTRO_MODE_CENTER_SQUEEZE: {
    // Symmetrical wipe from edges to center (Compression)
    float threshold = progress * (seg_len / 2.0f);
    int mid = seg_len / 2;

    for (int i = 0; i < seg_len; i++) {
      float dist = std::abs((float)i - (float)mid);
      float remaining_half = (seg_len / 2.0f) - threshold;

      if (dist >= remaining_half) {
        it[seg_start + i] = Color::BLACK;
      } else {
        // Progressive White Compression Boost
        // We inject white proportional to gravity/pressure of the squeeze.
        uint8_t white_boost = 0;
        if (progress > 0.85f) {
          white_boost = 255; // Sharp white flash at the end point
        } else {
          // Gradual white injection (up to ~160)
          white_boost = (uint8_t)(progress * 160.0f / 0.85f);
        }

        Color cur = it[seg_start + i].get();
        it[seg_start + i] = Color(qadd8(cur.r, white_boost), qadd8(cur.g, white_boost),
                                 qadd8(cur.b, white_boost), qadd8(cur.w, white_boost));
      }
    }
    break;
  }

  case INTRO_MODE_FADE:
  case INTRO_MODE_NONE:
  default:
    // Manual fade scalar scaling due to hardware fade circumvention
    for (int i = 0; i < seg_len; i++) {
      int global_idx = seg_start + i;
      Color c = it[global_idx].get();
      it[global_idx] =
          Color((uint8_t)(c.r * fade_scaler), (uint8_t)(c.g * fade_scaler),
                (uint8_t)(c.b * fade_scaler), (uint8_t)(c.w * fade_scaler));
    }
    break;
  }

  return (progress >= 1.0f);
}

// --- Autotune Auto-Disable Implementation ---
void CFXAddressableLightEffect::apply_autotune_defaults_() {
  CFXControl *c = act_->controller;
  const bool transient_autotune_context =
#ifdef USE_CFX_SEQUENCE
      (act_->active_sequence != nullptr || act_->sequence_autotune.has_value());
#else
      false;
#endif

  // 1. Speed
  number::Number *speed_num =
      (c && c->get_speed()) ? c->get_speed() : this->local_speed_();
  if (speed_num != nullptr && !this->has_speed_preset_()) {
    float target = (float)this->get_default_speed_(this->effect_id_);
    if (!transient_autotune_context && speed_num->state != target) {
      auto call = speed_num->make_call();
      call.set_value(target);
      call.perform();
    }
    act_->autotune_expected_speed = target;
  } else if (speed_num != nullptr) {
    act_->autotune_expected_speed = speed_num->state;
  }

  // 2. Intensity
  number::Number *intensity_num =
      (c && c->get_intensity()) ? c->get_intensity() : this->local_intensity_();
  if (intensity_num != nullptr && !this->has_intensity_preset_()) {
    float target = (float)this->get_default_intensity_(this->effect_id_);
    if (!transient_autotune_context && intensity_num->state != target) {
      auto call = intensity_num->make_call();
      call.set_value(target);
      call.perform();
    }
    act_->autotune_expected_intensity = target;
  } else if (intensity_num != nullptr) {
    act_->autotune_expected_intensity = intensity_num->state;
  }

  // 3. Palette — set selector to "Default" so the runner resolves its own
  //    built-in palette for this effect. Don't resolve to a concrete name
  //    (e.g. "Aurora") — that would prevent the runner from using its native
  //    default and would confuse the auto-disable override detection.
  select::Select *palette_sel =
      (c && c->get_palette()) ? c->get_palette() : this->local_palette_();
  if (palette_sel != nullptr && !this->has_palette_preset_()) {
    std::string pal_name = "Default";
    if (this->is_monochromatic_(this->effect_id_)) {
      pal_name = "Solid";
    }

    if (!transient_autotune_context &&
        palette_sel->current_option() != pal_name) {
      auto call = palette_sel->make_call();
      call.set_option(pal_name);
      call.perform();
    }
    act_->autotune_expected_palette = pal_name;
  } else if (palette_sel != nullptr) {
    act_->autotune_expected_palette = palette_sel->current_option();
  }
}

void CFXAddressableLightEffect::trigger_on_start() {
  for (auto *t : (cfg_ ? cfg_->on_start_triggers : empty_start_triggers_)) {
    t->trigger();
  }
}

void CFXAddressableLightEffect::trigger_on_begin() {
  for (auto *t : (cfg_ ? cfg_->on_begin_triggers : empty_begin_triggers_)) {
    t->trigger();
  }
}

void CFXAddressableLightEffect::trigger_on_stop() {
  for (auto *t : (cfg_ ? cfg_->on_stop_triggers : empty_stop_triggers_)) {
    t->trigger();
  }
}

void CFXAddressableLightEffect::trigger_on_complete() {
  for (auto *t :
       (cfg_ ? cfg_->on_complete_triggers : empty_complete_triggers_)) {
    t->trigger();
  }
}

void CFXAddressableLightEffect::check_milestones_(float current_pct) {
  if (act_ == nullptr || act_->suppress_positional_events)
    return;
  if (act_->intro_active && act_->intro_suppresses_milestones)
    return;
  act_->milestone_fired_this_frame = false;
  static uint8_t spi_reach_suppress_logs = 0;
  uint8_t next = act_->last_fired_milestone + MILESTONE_STEP;
  while (current_pct >= next && next <= 100) {
    act_->last_fired_milestone = next;
    act_->milestone_fired_this_frame = true;
#ifdef USE_CFX_EVENTS
    char buf[48];
    snprintf(buf, sizeof(buf), "cfx_reach:%s:%u", act_->strip_tag.c_str(),
             (unsigned)act_->last_fired_milestone);
    if (!act_->suppress_reach_event) {
      bool suppress_ha_reach = false;
#ifdef USE_CFX_SEQUENCE
      if (act_->active_sequence != nullptr) {
        SPIDiagCensus diag_census = collect_spi_diag_census();
        suppress_ha_reach =
            diag_census.active_spi_effects > 0 && diag_census.active_effects >= 2;
        if (suppress_ha_reach && spi_reach_suppress_logs < 12) {
          ESP_LOGD("cfx_seq",
                   "SPI diag reach-suppress[%u]: effect=%s tag=%s milestone=%u "
                   "active(e=%u,se=%u,spi=%u) bound=%u runners=%u",
                   static_cast<unsigned>(spi_reach_suppress_logs),
                   act_->cached_runner_name.c_str(), act_->strip_tag.c_str(),
                   static_cast<unsigned>(act_->last_fired_milestone),
                   static_cast<unsigned>(diag_census.active_effects),
                   static_cast<unsigned>(diag_census.active_segment_effects),
                   static_cast<unsigned>(diag_census.active_spi_effects),
                   static_cast<unsigned>(diag_census.bound_sequences),
                   static_cast<unsigned>(diag_census.runner_count));
          spi_reach_suppress_logs++;
        }
      }
#endif
      if (!suppress_ha_reach) {
        chimera_fx::CFXEventManager::get().fire_event(buf);
      }
    }
#endif
    next = act_->last_fired_milestone + MILESTONE_STEP;
  }
  if (current_pct < act_->last_fired_milestone) {
    if (act_->last_fired_milestone >= 100 || current_pct >= 100.0f)
      act_->last_fired_milestone = 0;
  }
}

void CFXAddressableLightEffect::check_positional_triggers(
    int32_t current_pixel, int32_t total_pixels) {
  if (act_ == nullptr || act_->suppress_positional_events)
    return;
  // Separator is a UI divider — suppress all positional events.
  if (this->effect_id_ == 185)
    return;

  // CFX-035b: Lazily determine if the main effect is progressive
  // (pixel-marching type like Wipe/Sweep). During the intro the main runner is
  // serviced silently; if it advances current_leading_pixel the effect will
  // produce its own milestone data after the intro ends and we should NOT also
  // fire from the intro's wipe. Monochromatic effects (skip_service=true,
  // runner stalled) and non-progressive effects (Aurora, Ocean, etc.) never set
  // a leading pixel, so the flag stays false and their intro milestones flow
  // through normally.
  if (act_ && act_->intro_active && !act_->intro_suppresses_milestones) {
    int32_t main_lp = -1;
    if (!act_->segment_runners.empty())
      main_lp = act_->segment_runners[0]->current_leading_pixel;
    else if (act_->runner)
      main_lp = act_->runner->current_leading_pixel;
    if (main_lp >= 0 && act_->active_intro_mode != INTRO_MODE_IMPACT_FLARE &&
        act_->active_intro_mode != INTRO_MODE_TIDAL_SURGE) {
      act_->intro_suppresses_milestones = true;
    }
  }

  // Suppress intro positional tracking only for progressive main effects.
  // (CFX-035b)
  if (act_ && act_->intro_active && act_->intro_suppresses_milestones)
    return;
  // Defensive bounds check
  if (total_pixels <= 0 || current_pixel < 0 || current_pixel > total_pixels) {
    return;
  }

  // Prevent multiple identical triggers in sequence, debounce across frames
  if (current_pixel == act_->last_triggered_pixel) {
    return;
  }

#ifdef USE_CFX_EVENTS
  // CFX-026: milestones fire on BOTH fill and erase passes.
  // is_return_phase_ is still read to detect the pass boundary and fire
  // cfx_idle as a separator — but it no longer suppresses milestones.
  bool is_return_phase = act_->runner ? act_->runner->is_return_phase_ : false;

  // Detect forward→erase transition: reset milestones and fire cfx_idle
  // immediately so HA sees a clean boundary between passes. (CFX-025)
  if (is_return_phase && !act_->last_return_phase) {
    // Force-sweep to 100% before resetting so the final milestone is never
    // lost to a frame-boundary miss. Per-instance — no singleton.
    this->check_milestones_(100.0f);
    this->reset_milestones_();
  }
  act_->last_return_phase = is_return_phase;

  {
    // Always fire per-light milestones (cfx_reach HA events) using this
    // effect's own act_->strip_tag, regardless of whether a sequence is
    // active. Previously, when a sequence was bound, ALL milestone events were
    // delegated to seq->check_positional_triggers() which used seq->strip_tag_
    // ('led_strip1') for every light in the sequence — silencing Strip2, Strip3
    // and any other non-primary light entirely.
    //
    // The sequence's check_positional_triggers() serves TWO purposes:
    //   1. Fire cfx_reach HA milestone events (tagged with seq->strip_tag_)
    //   2. Evaluate on_cfx_reach YAML automation triggers
    //
    // Purpose 1 is now handled here by the effect using its own tag.
    // Purpose 2 still needs the sequence — call it separately so YAML
    // on_cfx_reach automations continue to work.
    float current_percentage =
        (total_pixels > 1) ? (float)current_pixel / (float)(total_pixels - 1)
                           : 1.0f;
    this->check_milestones_(current_percentage * 100.0f);

#ifdef USE_CFX_SEQUENCE
    if (act_->active_sequence != nullptr) {
      act_->active_sequence->check_positional_triggers(current_pixel,
                                                       total_pixels,
                                                       is_return_phase);
    }
#endif
  }
#endif

  // Effect internal triggers (from YAML)
  if (!(cfg_ ? cfg_->on_reach_triggers : empty_reach_triggers_).empty()) {
    float current_percentage = (float)current_pixel / (float)total_pixels;

    for (auto *t : (cfg_ ? cfg_->on_reach_triggers : empty_reach_triggers_)) {
      float target = t->get_target_position();
      bool crossed = false;

      if (act_->last_triggered_percentage == -1.0f) {
        if (current_percentage >= target)
          crossed = true;
      } else {
        // Forward crossing
        if (current_percentage >= target &&
            act_->last_triggered_percentage < target) {
          crossed = true;
        }
        // Backward crossing
        else if (current_percentage <= target &&
                 act_->last_triggered_percentage > target) {
          crossed = true;
        }
        // Wrap-around forward
        else if (act_->last_triggered_percentage > 0.8f &&
                 current_percentage < 0.2f) {
          if (target > act_->last_triggered_percentage ||
              target <= current_percentage) {
            crossed = true;
          }
        }
        // Wrap-around backward
        else if (act_->last_triggered_percentage < 0.2f &&
                 current_percentage > 0.8f) {
          if (target < act_->last_triggered_percentage ||
              target >= current_percentage) {
            crossed = true;
          }
        }
      }

      if (crossed) {
        ESP_LOGV(TAG, "Effect Instance '%s' (%p): on_reach %.0f%% triggered",
                 this->get_name(), this, target * 100.0f);
        t->trigger(current_percentage);
      }
      // Feed WDT in long positional loops (16+ strips)
      esphome::App.feed_wdt();
    }
  }

  act_->last_triggered_percentage = (float)current_pixel / (float)total_pixels;
  act_->last_triggered_pixel = current_pixel;
}

#ifdef USE_CFX_SEQUENCE
void CFXAddressableLightEffect::set_active_sequence(CFXSequence *seq,
                                                    std::optional<uint8_t> spd,
                                                    std::optional<uint8_t> iten,
                                                    std::optional<uint8_t> pal,
                                                    std::optional<bool> mir,
                                                    std::optional<bool> autotune,
                                                    uint32_t itr) {
  // CFX-030: act_ is null when the effect is not running (light off or
  // removed). Bail out silently rather than dereferencing null.
  if (this->act_ == nullptr) {
    ESP_LOGW(
        "cfx_seq",
        "set_active_sequence: act_ is null (effect not running), skipping");
    return;
  }

  cfx_light::CFXLightOutput *diag_out = nullptr;
  if (this->is_virtual_segment_) {
#ifdef USE_ESP32
    auto *vseg = static_cast<cfx_light::CFXVirtualSegmentLight *>(
        this->get_addressable_());
    if (vseg != nullptr)
      diag_out = vseg->get_parent();
#endif
  } else {
    diag_out = static_cast<cfx_light::CFXLightOutput *>(this->get_addressable_());
  }

  if (diag_out != nullptr && diag_out->is_spi_transport() &&
      this->act_->spi_diag_bind_logs < 10) {
    std::string seq_name_storage = (seq != nullptr) ? seq->get_name() : "-";
    std::string seq_id_storage = (seq != nullptr) ? seq->get_id() : "-";
    ESP_LOGD("cfx_seq",
             "SPI diag bind[%u]: effect=%s tag=%s seq=%s id=%s seq_ptr=%p "
             "act=%p runner=%p iter=%" PRIu32 " spd=%d int=%d pal=%d mir=%d auto=%d",
             static_cast<unsigned>(this->act_->spi_diag_bind_logs),
             this->act_->cached_runner_name.c_str(), this->act_->strip_tag.c_str(),
             seq_name_storage.c_str(), seq_id_storage.c_str(), seq, this->act_,
             this->act_->runner, itr, spd.has_value(), iten.has_value(),
             pal.has_value(), mir.has_value(), autotune.has_value());
    this->act_->spi_diag_bind_logs++;
  }

  act_->active_sequence = seq;
  act_->sequence_speed = spd;
  act_->sequence_intensity = iten;
  act_->sequence_palette = pal;
  act_->sequence_mirror = mir;
  act_->sequence_autotune = autotune;
  act_->sequence_iterations = itr;

  if (!act_->segment_runners.empty()) {
    for (auto *r : act_->segment_runners) {
      r->sequence_owns_speed_ = spd.has_value();
      r->sequence_owns_intensity_ = iten.has_value();
      r->sequence_owns_palette_ = pal.has_value();
      r->sequence_owns_mirror_ = mir.has_value();
    }
  } else if (act_->runner) {
    act_->runner->sequence_owns_speed_ = spd.has_value();
    act_->runner->sequence_owns_intensity_ = iten.has_value();
    act_->runner->sequence_owns_palette_ = pal.has_value();
    act_->runner->sequence_owns_mirror_ = mir.has_value();
  }

  // Reset trackers when a new sequence is bound
  if (seq != nullptr) {
    const bool suppress_ha_events = !seq->get_ha_events();
    act_->suppress_reach_event = suppress_ha_events;
    act_->suppress_stop_event = suppress_ha_events;
    act_->suppress_complete_event = suppress_ha_events;

    // Disable built-in intro/transitions to prevent blackout/conflict
    // EXCEPT for Monochromatic Presets, which functionally ARE intros.
    if (!this->get_monochromatic_preset_(this->effect_id_).is_active) {
      act_->intro_active = false;
    }
    act_->state = TRANSITION_NONE;
    act_->last_triggered_percentage = -1.0f;
    act_->last_leading_pixel = -1;
    act_->last_triggered_pixel = -1;

    if (!act_->segment_runners.empty()) {
      for (auto *r : act_->segment_runners) {
        r->reset();
        r->target_iterations_ = itr;
      }
    } else if (act_->runner) {
      act_->runner->reset();
      act_->runner->target_iterations_ = itr;
    }
  }
}
#endif

void CFXAddressableLightEffect::execute_completion() {
  if (this->act_ == nullptr || !this->act_->completion_pending) return;

#ifdef USE_CFX_SEQUENCE
  if (this->act_->active_sequence != nullptr) {
    auto *seq = this->act_->active_sequence;

    // CFX-044d: Transfer-race guard.
    // flush_pending_triggers() runs BEFORE execute_completion() in the same
    // worker cycle. A cfx_reach trigger at e.g. 75% may have called cfx_set,
    // which calls set_active_sequence(seq) on a NEW effect instance. The
    // sequence is therefore still running — this effect no longer owns it.
    // If we call complete_and_notify() here, clear_active_binding() would
    // iterate all effects, find the new owner, and NULL its active_sequence,
    // killing the next step mid-start.
    //
    // Detection: scan all effects for another instance that now owns `seq`.
    // If found, abort the completion — the sequence will complete naturally
    // when the new effect's runner signals effect_complete_.
    for (auto *other : CFXAddressableLightEffect::all_effects) {
      if (other != this && other->get_active_sequence() == seq) {
        // Sequence has been transferred — clear local state only.
        ESP_LOGD("cfx_seq", "execute_completion: seq '%s' transferred to %p, deferring",
                 seq->get_name().c_str(), other);
        this->act_->completion_pending = false;
        this->act_->active_sequence = nullptr;
        return;
      }
    }

    // Safe to complete: this effect is still the sole owner.
    this->act_->completion_pending = false;
    this->act_->active_sequence = nullptr; // prevent re-entry

    // CFX-044 FIX: Delegate to complete_and_notify() which performs teardown
    // in the correct order: stop + restore BEFORE on_complete fires, so any
    // chained cfx_set in on_complete is never overwritten by a late stop().
    seq->complete_and_notify();

  } else {
    // Standalone self-terminating effect (no sequence bound).
    this->act_->completion_pending = false;
    auto *ls = this->get_light_state();
    if (ls != nullptr) {
      auto call = ls->make_call();
      call.set_state(false);
      call.set_transition_length(0);
      call.perform();
    }
  }
#endif
}

} // namespace chimera_fx
} // namespace esphome
