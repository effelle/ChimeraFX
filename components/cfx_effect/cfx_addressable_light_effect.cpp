/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * This file is part of the ChimeraFX for ESPHome.
 */

#include "cfx_addressable_light_effect.h"
#include "../cfx_light/cfx_light.h"
#include "cfx_compat.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h" // For millis()
#include "esphome/core/log.h"

namespace esphome {
namespace chimera_fx {

std::vector<CFXControl *> CFXControl::instances; // Define static vector

static const char *TAG = "chimera_fx";

CFXAddressableLightEffect::CFXAddressableLightEffect(const char *name)
    : light::AddressableLightEffect(name) {}

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
  default:
    return {false, INTRO_NONE, INTRO_NONE};
  }
}

bool CFXAddressableLightEffect::is_monochromatic_(uint8_t effect_id) {
  switch (effect_id) {
  case 161: // Horizon Sweep
  case 162: // Curtain Sweep
  case 163: // Stardust Sweep
  case 165: // Twin Pulse Sweep
  case 166: // Transmission
    return true;
  default:
    return false;
  }
}

void CFXAddressableLightEffect::start() {
  light::AddressableLightEffect::start();

  // Find controller early
  if (this->controller_ == nullptr) {
    this->controller_ = CFXControl::find(this->get_light_state());
  }

  // Allocate Runner(s) early so we can use them for metadata fallback
  if (this->runner_ == nullptr) {
    auto *it = (light::AddressableLight *)this->get_light_state()->get_output();
    if (it != nullptr) {
#ifdef USE_ESP32
      auto *cfx_out = static_cast<cfx_light::CFXLightOutput *>(it);
      const auto &seg_defs = cfx_out->get_segment_defs();

      if (!seg_defs.empty() && !this->segments_initialized_) {
        for (const auto &def : seg_defs) {
          auto *r = new CFXRunner(it);
          r->_segment.start = def.start;
          r->_segment.stop = def.stop;
          r->_segment.mirror = def.mirror;
          r->set_segment_id(def.id);
          r->setMode(this->effect_id_);
          r->diagnostics.set_target_interval_ms(this->update_interval_);
          this->segment_runners_.push_back(r);
        }
        this->runner_ = this->segment_runners_[0];
        this->segments_initialized_ = true;
        ESP_LOGI("chimera_fx", "Multi-segment mode: %u runners created for %s",
                 this->segment_runners_.size(), this->get_name());
      } else {
#endif
        this->runner_ = new CFXRunner(it);
        this->runner_->setMode(this->effect_id_);
        this->runner_->diagnostics.set_target_interval_ms(
            this->update_interval_);
        ESP_LOGI("chimera_fx", "Single-segment mode runner created for %s",
                 this->get_name());
#ifdef USE_ESP32
      }
#endif
    }
  }

  this->run_controls_();

  CFXControl *c = this->controller_;

  // 0. Autotune Resolution
  bool autotune_enabled = true; // Default to true if not found (Constraint)
  switch_::Switch *autotune_sw =
      (c && c->get_autotune()) ? c->get_autotune() : this->autotune_;

  if (this->autotune_preset_.has_value()) {
    autotune_enabled = this->autotune_preset_.value();
  } else if (autotune_sw != nullptr) {
    autotune_enabled = autotune_sw->state;
  }

  this->autotune_active_ = autotune_enabled;

  if (autotune_enabled) {
    this->apply_autotune_defaults_();
  }

  // 1. Speed (YAML Preset Override Only)
  number::Number *speed_num =
      (c && c->get_speed()) ? c->get_speed() : this->speed_;
  if (speed_num != nullptr && this->speed_preset_.has_value()) {
    float target = (float)this->speed_preset_.value();
    if (speed_num->state != target) {
      auto call = speed_num->make_call();
      call.set_value(target);
      call.perform();
    }
  }

  // 2. Intensity (YAML Preset Override Only)
  number::Number *intensity_num =
      (c && c->get_intensity()) ? c->get_intensity() : this->intensity_;
  if (intensity_num != nullptr && this->intensity_preset_.has_value()) {
    float target = (float)this->intensity_preset_.value();
    if (intensity_num->state != target) {
      auto call = intensity_num->make_call();
      call.set_value(target);
      call.perform();
    }
  }

  // 3. Palette (YAML Preset Override Only)
  select::Select *palette_sel =
      (c && c->get_palette()) ? c->get_palette() : this->palette_;
  if (palette_sel != nullptr && this->palette_preset_.has_value()) {
    auto call = palette_sel->make_call();
    call.set_index(this->palette_preset_.value());
    call.perform();
  }

  // 4. Mirror
  switch_::Switch *mirror_sw =
      (c && c->get_mirror()) ? c->get_mirror() : this->mirror_;
  if (mirror_sw != nullptr && this->mirror_preset_.has_value()) {
    bool target = this->mirror_preset_.value();
    if (mirror_sw->state != target) {
      if (target) {
        mirror_sw->turn_on();
      } else {
        mirror_sw->turn_off();
      }
    }
  }

  // 5. Intro Effect
  select::Select *intro_sel = (c && c->get_intro_effect())
                                  ? c->get_intro_effect()
                                  : this->intro_effect_;
  if (!this->initial_preset_applied_ && intro_sel != nullptr &&
      this->intro_preset_.has_value()) {
    // Only apply if still at "None" factory default — preserve user choices
    const char *cur = intro_sel->current_option();
    if (cur == nullptr || strcmp(cur, "None") == 0) {
      auto call = intro_sel->make_call();
      call.set_index(this->intro_preset_.value());
      call.perform();
    }
  }

  // 6. Intro Duration
  number::Number *intro_dur_num = (c && c->get_intro_duration())
                                      ? c->get_intro_duration()
                                      : this->intro_duration_;
  if (!this->initial_preset_applied_ && intro_dur_num != nullptr &&
      this->intro_duration_preset_.has_value()) {
    float target = this->intro_duration_preset_.value();
    if (intro_dur_num->state != target) {
      auto call = intro_dur_num->make_call();
      call.set_value(target);
      call.perform();
    }
  }

  // 7. Intro Use Palette
  switch_::Switch *intro_pal_sw = (c && c->get_intro_use_palette())
                                      ? c->get_intro_use_palette()
                                      : this->intro_use_palette_;
  if (!this->initial_preset_applied_ && intro_pal_sw != nullptr &&
      this->intro_use_palette_preset_.has_value()) {
    bool target = this->intro_use_palette_preset_.value();
    if (intro_pal_sw->state != target) {
      if (target) {
        intro_pal_sw->turn_on();
      } else {
        intro_pal_sw->turn_off();
      }
    }
  }

  // 7.5. Force White
  switch_::Switch *force_white_sw =
      (c && c->get_force_white()) ? c->get_force_white() : this->force_white_;
  if (!this->initial_preset_applied_ && force_white_sw != nullptr &&
      this->force_white_preset_.has_value()) {
    bool target = this->force_white_preset_.value();
    if (force_white_sw->state != target) {
      if (target) {
        force_white_sw->turn_on();
      } else {
        force_white_sw->turn_off();
      }
    }
  }

  // 8. Timer
  number::Number *timer_num = (c) ? c->get_timer() : nullptr;
  if (!this->initial_preset_applied_ && timer_num != nullptr &&
      this->timer_preset_.has_value()) {
    float target = (float)this->timer_preset_.value();
    if (timer_num->state != target) {
      auto call = timer_num->make_call();
      call.set_value(target);
      call.perform();
    }
  }

  // 9. Outro Effect
  select::Select *outro_sel = (c && c->get_outro_effect())
                                  ? c->get_outro_effect()
                                  : this->outro_effect_;
  if (!this->initial_preset_applied_ && outro_sel != nullptr &&
      this->outro_preset_.has_value()) {
    // Only apply if still at "None" factory default — preserve user choices
    const char *cur = outro_sel->current_option();
    if (cur == nullptr || strcmp(cur, "None") == 0) {
      auto call = outro_sel->make_call();
      call.set_index(this->outro_preset_.value());
      call.perform();
    }
  }

  // 10. Outro Duration
  number::Number *outro_dur_num = (c && c->get_outro_duration())
                                      ? c->get_outro_duration()
                                      : this->outro_duration_;
  if (!this->initial_preset_applied_ && outro_dur_num != nullptr &&
      this->outro_duration_preset_.has_value()) {
    float target = this->outro_duration_preset_.value();
    if (outro_dur_num->state != target) {
      auto call = outro_dur_num->make_call();
      call.set_value(target);
      call.perform();
    }
  }

  this->initial_preset_applied_ = true;

  // Visualizer: Notify metadata
  auto *out = static_cast<cfx_light::CFXLightOutput *>(
      this->get_light_state()->get_output());
  if (out != nullptr) {
    std::string pal_name = "";
    if (palette_sel && palette_sel->has_state()) {
      const char *opt = palette_sel->current_option();
      if (opt != nullptr)
        pal_name = opt;
    }

    // Resolve "Default" to actual palette name if possible
    if ((pal_name.empty() || pal_name == "Default") && this->runner_) {
      pal_name = this->get_palette_name_(this->runner_->getPalette());
    }

    out->send_visualizer_metadata(this->get_name(), pal_name);
    this->last_sent_palette_ = pal_name;
  }

  // State Machine Init: Check if we are turning ON from OFF
  auto *state = this->get_light_state();
  if (state != nullptr) {
    bool is_fresh_turn_on = !state->current_values.is_on();

    // Bypass intro for effects that have their own embedded startup animations
    if (this->effect_id_ == 158 || this->effect_id_ == 159) {
      is_fresh_turn_on = false;
    }

    this->intro_active_ = is_fresh_turn_on;

    if (this->intro_active_ && this->controller_ == nullptr) {
      // Try linking again if missed
      this->controller_ = CFXControl::find(this->get_light_state());
      this->run_controls_(); // Re-run to pull pointers
    }

    if (this->intro_active_) {
      this->intro_start_time_ = millis();
    } else {
    }
  } else {
    this->intro_active_ = false;
  }

  // Resolve Intro Mode (Now reflecting Presets!)
  this->active_intro_mode_ = INTRO_NONE;

  if (this->intro_active_) {
    // Re-resolve in case preset changed it
    if (intro_sel == nullptr && c != nullptr) {
      intro_sel = c->get_intro_effect();
    }
    // Also check local member if controller failed
    if (intro_sel == nullptr)
      intro_sel = this->intro_effect_;

    // 1. Highest Priority: Embedded Monochromatic Presets
    MonochromaticPreset preset =
        this->get_monochromatic_preset_(this->effect_id_);
    if (preset.is_active) {
      this->intro_active_ = true;
      this->active_intro_mode_ = preset.intro_mode;
    } else {
      // 2. Fallback to UI Selectors / YAML Presets
      if (intro_sel != nullptr && intro_sel->has_state()) {
        const char *opt = intro_sel->current_option();
        std::string s = opt ? opt : "";
        if (s == "Wipe")
          this->active_intro_mode_ = INTRO_WIPE;
        else if (s == "Fade")
          this->active_intro_mode_ = INTRO_FADE;
        else if (s == "Center")
          this->active_intro_mode_ = INTRO_CENTER;
        else if (s == "Glitter")
          this->active_intro_mode_ = INTRO_GLITTER;
        else if (s == "Twin Pulse")
          this->active_intro_mode_ = INTRO_TWIN_PULSE;
        else if (s == "Morse Code")
          this->active_intro_mode_ = INTRO_MODE_MORSE;
      }
    }

    // Cache Speed for Intro (since Morse needs it for unit_ms)
    this->active_intro_speed_ = 128; // fallback
    number::Number *speed_num =
        (c && c->get_speed()) ? c->get_speed() : this->speed_;
    if (speed_num != nullptr && speed_num->has_state()) {
      this->active_intro_speed_ = (uint8_t)speed_num->state;
    } else if (this->speed_preset_.has_value()) {
      this->active_intro_speed_ = this->speed_preset_.value();
    } else {
      this->active_intro_speed_ = this->get_default_speed_(this->effect_id_);
    }

    // Cache for this run
    this->intro_effect_ = intro_sel;

    if (this->active_intro_mode_ == INTRO_NONE && !preset.is_active) {
      this->intro_active_ = false;

      // Use the light's default_transition_length for a smooth fade-in
      auto *ls = this->get_light_state();
      if (ls != nullptr) {
        uint32_t trans_ms = ls->get_default_transition_length();
        if (trans_ms > 0) {
          this->fade_in_active_ = true;
          this->fade_in_start_ms_ = millis();
          this->fade_in_duration_ms_ = trans_ms;
        }
      }
    } else {
    }
  }

  // Initialize Outro state tracking
  if (state != nullptr) {
    // last_target_on_ removed, state transitions handled natively
  }
}

void CFXAddressableLightEffect::stop() {
  light::AddressableLightEffect::stop();

  // Clear intro snapshot vector to reclaim RAM
  intro_snapshot_.clear();
  intro_snapshot_.shrink_to_fit();

  CFXControl *c = this->controller_;
  auto *state = this->get_light_state();

  if (state != nullptr && this->runner_ != nullptr) {
    auto *out =
        static_cast<cfx_light::CFXLightOutput *>(this->get_addressable_());
    if (out != nullptr) {

      // Resolve Outro Mode synchronously before dropping controller mapping
      this->active_outro_mode_ = INTRO_NONE;
      select::Select *out_eff = this->outro_effect_;
      if (out_eff == nullptr && c != nullptr)
        out_eff = c->get_outro_effect();

      // 1. Highest Priority: Embedded Monochromatic Presets
      MonochromaticPreset preset =
          this->get_monochromatic_preset_(this->effect_id_);

      if (preset.is_active) {
        this->active_outro_mode_ = preset.outro_mode;
      } else {
        // 2. Fallback to UI Selectors / YAML Presets
        if (out_eff != nullptr && out_eff->has_state()) {
          std::string opt = out_eff->current_option();
          if (opt == "Wipe")
            this->active_outro_mode_ = INTRO_WIPE;
          else if (opt == "Center")
            this->active_outro_mode_ = INTRO_CENTER;
          else if (opt == "Glitter")
            this->active_outro_mode_ = INTRO_GLITTER;
          else if (opt == "Fade")
            this->active_outro_mode_ = INTRO_FADE;
          else if (opt == "Twin Pulse")
            this->active_outro_mode_ = INTRO_TWIN_PULSE;
          else if (opt == "Morse Code")
            this->active_outro_mode_ = INTRO_MODE_MORSE;
        } else if (this->outro_preset_.has_value()) {
          this->active_outro_mode_ = *this->outro_preset_;
        } else {
          this->active_outro_mode_ = this->active_intro_mode_;
        }
      }

      // 10. Outro Duration Priority Chain
      uint32_t duration_ms = 1000; // Hard Default

      number::Number *dur_num = this->outro_duration_;
      if (dur_num == nullptr && c != nullptr)
        dur_num = c->get_outro_duration();

      if (this->active_outro_mode_ == INTRO_MODE_MORSE) {
        // A. Highest: Morse Code Timing (Message length based)
        uint8_t current_speed = 128;
        number::Number *intensity_num =
            (c && c->get_intensity()) ? c->get_intensity() : this->intensity_;
        if (intensity_num != nullptr && intensity_num->has_state()) {
          current_speed = (uint8_t)intensity_num->state;
        }
        uint32_t unit_ms = 80 + ((255 - current_speed) * 100 / 255);
        duration_ms = 35 * unit_ms;
      } else if (dur_num != nullptr && dur_num->has_state()) {
        // B. High: UI Slider
        duration_ms = (uint32_t)(dur_num->state * 1000.0f);
      } else if (this->outro_duration_preset_.has_value()) {
        // C. Medium: YAML Preset
        duration_ms =
            (uint32_t)(this->outro_duration_preset_.value() * 1000.0f);
      } else if (preset.is_active) {
        // D. Fallback: Monochromatic Mode Speed Slider
        number::Number *speed_num = this->speed_;
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
      this->active_outro_duration_ms_ = duration_ms;

      // Cache Intensity for Outro (since controller is detached during Outro)
      this->active_outro_intensity_ = 128; // fallback
      number::Number *intensity_num = this->intensity_;
      if (intensity_num == nullptr && c != nullptr)
        intensity_num = c->get_intensity();

      if (intensity_num != nullptr && intensity_num->has_state()) {
        this->active_outro_intensity_ = (uint8_t)intensity_num->state;
      } else {
        this->active_outro_intensity_ =
            this->get_default_intensity_(this->effect_id_);
      }

      // Capture ALL segment runners for the outro (or just the primary for
      // single-runner)
      CFXRunner *captured_runner = this->runner_;
      this->runner_ = nullptr; // Null here so start() creates a fresh one later
      std::vector<CFXRunner *> captured_runners;

      if (!this->segment_runners_.empty()) {
        for (auto *r : this->segment_runners_) {
          if (c)
            c->unregister_runner(r);
          captured_runners.push_back(r);
        }
        this->segment_runners_.clear();
        this->segments_initialized_ = false;
      } else {
        if (c)
          c->unregister_runner(captured_runner);
        captured_runners.push_back(captured_runner);
      }

      // Safely detach from effect runner system
      this->controller_ = nullptr;
      this->intro_active_ = false;
      this->outro_active_ = false;

      // Register the callback synchronously to prevent ESPHome from rendering
      // a rogue frame of a solid color transition during the gap.
      this->outro_start_time_ = 0; // Signify uninitialized start time

      out->set_outro_callback([this, out, captured_runners]() -> bool {
        auto *current_state = this->get_light_state();
        if (current_state != nullptr && current_state->remote_values.is_on()) {
          // Effect was completely changed or light remained ON.
          // Abort the outro and delete all captured runners cleanly.
          for (auto *r : captured_runners)
            delete r;
          return true;
        }

        // Initialize outro start time on the very first allowed frame
        if (this->outro_start_time_ == 0) {
          this->outro_start_time_ = millis();
        }

        // Run outro frame on ALL captured segment runners
        bool done = false;
        for (auto *r : captured_runners) {
          ::instance = r;
          done = this->run_outro_frame(*out, r);
        }

        if (done) {
          for (auto *r : captured_runners)
            delete r;
        }
        return done;
      });

      return;
    }
  }

  // Normal Stop / Cleanup (Failsafe)
  if (!this->segment_runners_.empty()) {
    for (auto *r : this->segment_runners_) {
      if (r != this->runner_ && this->controller_) {
        this->controller_->unregister_runner(r);
      }
      if (r != this->runner_) {
        delete r;
      }
    }
    this->segment_runners_.clear();
    this->segments_initialized_ = false;
  }
  if (this->runner_ != nullptr) {
    if (this->controller_) {
      this->controller_->unregister_runner(this->runner_);
    }
    delete this->runner_;
    this->runner_ = nullptr;
  }
  this->controller_ = nullptr;
  this->intro_active_ = false;
  this->outro_active_ = false;
}

void CFXAddressableLightEffect::apply(light::AddressableLight &it,
                                      const Color &current_color) {
  // Use update_interval_ (default 24ms = 42 FPS, set via YAML or __init__.py)
  // This provides CPU headroom while maintaining smooth animation

  const uint32_t now = cfx_millis();
  if (now - this->last_run_ < this->update_interval_) {
    return; // Not time for update yet
  }
  this->last_run_ = now;

  // --- Ensure Runner(s) ---
  if (this->runner_ == nullptr) {
#ifdef USE_ESP32
    // Check if cfx_light has segment definitions
    auto *cfx_out = static_cast<cfx_light::CFXLightOutput *>(&it);
    const auto &seg_defs = cfx_out->get_segment_defs();

    if (!seg_defs.empty() && !this->segments_initialized_) {
      // Multi-segment mode: create one runner per segment def
      for (const auto &def : seg_defs) {
        auto *r = new CFXRunner(&it);
        r->_segment.start = def.start;
        r->_segment.stop = def.stop;
        r->_segment.mirror = def.mirror;
        r->set_segment_id(def.id);
        r->setMode(this->effect_id_);
        r->diagnostics.set_target_interval_ms(this->update_interval_);
        this->segment_runners_.push_back(r);
      }
      // Primary runner = first segment (used by intro/outro/control logic)
      this->runner_ = this->segment_runners_[0];
      this->segments_initialized_ = true;
      ESP_LOGI("chimera_fx", "Multi-segment mode: %u runners created for %s",
               this->segment_runners_.size(), this->get_name());
    } else {
#endif
      // Single-runner mode (backward compatible)
      this->runner_ = new CFXRunner(&it);
      this->runner_->setMode(this->effect_id_);
      this->runner_->diagnostics.set_target_interval_ms(this->update_interval_);
#ifdef USE_ESP32
    }
#endif
  }

  // Sync Debug State (must be AFTER runner creation to avoid null deref)
  if (this->debug_switch_ && this->runner_) {
    this->runner_->setDebug(this->debug_switch_->state);
    if (this->get_light_state()) {
      this->runner_->setName(this->get_light_state()->get_name().c_str());
    }
  }

  // Update speed from Number component
  // Update controls via Controller or Local entities
  this->run_controls_();

  // Retrieve Force White Switch State
  switch_::Switch *force_white_sw =
      (this->controller_ && this->controller_->get_force_white())
          ? this->controller_->get_force_white()
          : this->force_white_;

  bool force_white_active = force_white_sw != nullptr && force_white_sw->state;
  // Only truly monochromatic effects (161-163) can route purely to W channel.
  // Other Solid-palette effects (Collider, Dissolve, BPM, etc.) use RGB from
  // colors[0] and would render black if RGB is zeroed.
  bool eligible_monochrome = this->is_monochromatic_(this->effect_id_);

  uint32_t color;
  Color adjusted_color = current_color;

  if (force_white_active && eligible_monochrome) {
    // Intelligent monochrome routing: steal the max brightness intent from RGB
    // and map purely to W hardware
    uint8_t max_rgb = std::max(std::max(current_color.red, current_color.green),
                               current_color.blue);
    uint8_t target_w = std::max(current_color.white, max_rgb);
    color = (uint32_t(target_w) << 24); // R, G, B are 0
    adjusted_color = Color(0, 0, 0, target_w);
  } else {
    // Normal Mode: Pass current light color to segment natively
    color = (uint32_t(current_color.white) << 24) |
            (uint32_t(current_color.red) << 16) |
            (uint32_t(current_color.green) << 8) | uint32_t(current_color.blue);
  }
  // Sync color to all runners
  if (!this->segment_runners_.empty()) {
    for (auto *r : this->segment_runners_) {
      r->setColor(color);
    }
  } else {
    this->runner_->setColor(color);
  }

  // === Dynamic Gamma Update ===
  // Sync the Runner's gamma LUT with the light's current gamma setting
  float current_gamma = this->get_light_state()->get_gamma_correct();
  if (!this->segment_runners_.empty()) {
    for (auto *r : this->segment_runners_) {
      if (abs(r->_gamma - current_gamma) > 0.01f) {
        r->setGamma(current_gamma);
      }
    }
  } else {
    if (abs(this->runner_->_gamma - current_gamma) > 0.01f) {
      this->runner_->setGamma(current_gamma);
    }
  }

  // === State Machine: Intro vs Main Effect ===
  if (this->intro_active_) {
    // Run intro on ALL segments (swap-on-service pattern)
    if (!this->segment_runners_.empty()) {
      for (auto *r : this->segment_runners_) {
        ::instance = r;
        this->run_intro(it, adjusted_color);
      }
    } else {
      this->run_intro(it, adjusted_color);
    }

    // 2. Resolve Intro Completion Duration (Priority Hierarchy)
    uint32_t duration_ms = 1000; // Final Default: 1.0s
    number::Number *dur_num = this->intro_duration_;
    if (dur_num == nullptr && this->controller_ != nullptr)
      dur_num = this->controller_->get_intro_duration();

    if (dur_num != nullptr && dur_num->has_state()) {
      // High Priority: UI Slider
      duration_ms = (uint32_t)(dur_num->state * 1000.0f);
    } else if (this->intro_duration_preset_.has_value()) {
      // Medium Priority: YAML Preset
      duration_ms = (uint32_t)(this->intro_duration_preset_.value() * 1000.0f);
    } else {
      // Monochromatic Preset Fallback: Speed Slider
      MonochromaticPreset preset =
          this->get_monochromatic_preset_(this->effect_id_);
      if (preset.is_active) {
        number::Number *speed_num = this->speed_;
        if (speed_num == nullptr && this->controller_ != nullptr)
          speed_num = this->controller_->get_speed();

        if (speed_num != nullptr && speed_num->has_state()) {
          // Map Speed (0-255) to Duration (500ms up to 10000ms)
          float speed_val = speed_num->state;
          duration_ms = (uint32_t)(500.0f + (speed_val / 255.0f * 9500.0f));
        } else {
          duration_ms = 1000; // Standard 1s default
        }
      }
    }

    if (millis() - this->intro_start_time_ > duration_ms) {
      this->intro_active_ = false;

      // Check if Transition is enabled via config
      float trans_dur = (this->transition_duration_ != nullptr &&
                         this->transition_duration_->has_state())
                            ? this->transition_duration_->state
                            : 1.5f;

      MonochromaticPreset preset =
          this->get_monochromatic_preset_(this->effect_id_);
      if (preset.is_active) {
        trans_dur = 0.0f; // Instant finish for Monochromatic Presets
      }

      if (trans_dur > 0.0f) {
        // Snapshot Intro End State
        this->intro_snapshot_.resize(it.size());
        for (int i = 0; i < it.size(); i++) {
          this->intro_snapshot_[i] = it[i].get();
        }
        this->state_ =
            TRANSITION_RUNNING; // Use RUNNING to signify Active Blend
        this->transition_start_ms_ = millis();
      } else {
        this->state_ = TRANSITION_NONE;
      }

      // Ensure Main Runner is reset/started
      this->runner_->start();
    }
  } else {
    // Main CFX effect Running — Multi-Segment Swap-on-Service
    if (!this->segment_runners_.empty()) {
      // Multi-segment: iterate all runners, swapping the global instance
      for (auto *r : this->segment_runners_) {
        ::instance = r;
        r->service();
      }
    } else {
      // Single runner (backward compatible)
      this->runner_->service();
    }

    // Apply user brightness to all rendered pixels
    // cfx_light::write_state() sends raw bytes to DMA without brightness
    // scaling, so we must apply it here after all runners render.
    auto *bri_state = this->get_light_state();
    if (bri_state != nullptr) {
      float bri = bri_state->remote_values.get_brightness();
      if (bri < 0.99f) { // Skip if already at full brightness
        for (int i = 0; i < it.size(); i++) {
          Color pc = it[i].get();
          it[i] = Color((uint8_t)(pc.r * bri), (uint8_t)(pc.g * bri),
                        (uint8_t)(pc.b * bri), (uint8_t)(pc.w * bri));
        }
      }
    }

    // Handle INTRO_NONE fade-in (brightness ramp 0→1)
    if (this->fade_in_active_) {
      uint32_t elapsed = millis() - this->fade_in_start_ms_;
      if (elapsed >= this->fade_in_duration_ms_) {
        this->fade_in_active_ = false; // Fade complete
      } else {
        float progress = (float)elapsed / (float)this->fade_in_duration_ms_;
        // Scale all pixel brightness by progress
        for (int i = 0; i < it.size(); i++) {
          Color c = it[i].get();
          it[i] = Color((uint8_t)(c.r * progress), (uint8_t)(c.g * progress),
                        (uint8_t)(c.b * progress), (uint8_t)(c.w * progress));
        }
      }
    }

    // Handle Intro→Main Blending
    if (this->state_ == TRANSITION_RUNNING) {
      uint32_t trans_elapsed = millis() - this->transition_start_ms_;
      float trans_dur_ms = (this->transition_duration_
                                ? this->transition_duration_->state * 1000.0f
                                : 1500.0f);

      // Soft Dissolve (Fairy Dust with Crossfade) Logic
      const float softness = 0.2f; // Configurable softness
      // Scale progress to ensure all pixels complete transition even with
      // softness delay
      float progress =
          ((float)trans_elapsed / trans_dur_ms) * (1.0f + softness);

      // Seed for deterministic random mask
      uint32_t seed = this->transition_start_ms_;

      for (int i = 0; i < it.size(); i++) {
        if (i >= this->intro_snapshot_.size())
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
            it[i] = this->intro_snapshot_[i];
          } else {
            // Blend Intro -> Main
            Color buf = this->intro_snapshot_[i];
            Color main = it[i].get();
            uint8_t r = (uint8_t)(buf.r * (1.0f - mix) + main.r * mix);
            uint8_t g = (uint8_t)(buf.g * (1.0f - mix) + main.g * mix);
            uint8_t b = (uint8_t)(buf.b * (1.0f - mix) + main.b * mix);
            uint8_t w = (uint8_t)(buf.w * (1.0f - mix) + main.w * mix);
            it[i] = Color(r, g, b, w);
          }
        }
      }

      // End transition when fully complete
      if (progress >= (1.0f + softness)) {
        this->state_ = TRANSITION_NONE;
      }
    }
  }

  it.schedule_show();
}

uint8_t CFXAddressableLightEffect::get_palette_index_() {
  select::Select *s = this->palette_;
  if (s == nullptr && this->controller_ != nullptr)
    s = this->controller_->get_palette();

  if (s == nullptr)
    return 0;

  const char *option = s->current_option();
  if (option == nullptr)
    return 0;

  // Optimization: Use strcmp to avoid std::string allocation on every frame
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
    return 23;

  if (strcmp(option, "Default") == 0) {
    // Resolve the natural default for this effect
    if (this->runner_) {
      uint8_t m = this->runner_->getMode();
      return this->get_default_palette_id_(m);
    }
    return 1; // Fallback to Aurora if no runner
  }

  return 0; // Unknown palette name
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
    return 11; // Defaults to Ocean

  case 38:    // Aurora
    return 1; // Defaults to Aurora Palette

  case 104:    // Sunrise
    return 12; // Defaults to HeatColors

  case 52:     // Running Dual
    return 13; // Defaults to Sakura

  default:
    return 1; // General fallback to Aurora
  }
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
  case 9:
    return "Lava";
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
  case 23:
    return "Twilight";
  case 254:
    return "Smart Random";
  case 255:
    return "Solid";
  default:
    return "Default";
  }
}

uint8_t CFXAddressableLightEffect::get_default_speed_(uint8_t effect_id) {
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
  case 161:
  case 162:
  case 163:
    return 1; // Monochromatic series (fastest speed)
  case 164:
    return 100; // Collider (Default Speed)
  default:
    return 128; // WLED default
  }
}

uint8_t CFXAddressableLightEffect::get_default_intensity_(uint8_t effect_id) {
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
  case 161:
  case 162:
  case 163:
    return 1; // Monochromatic series (No blur)
  case 164:
    return 170; // Collider (Default Intensity)
  default:
    return 128; // WLED default
  }
}

void CFXAddressableLightEffect::run_controls_() {
  // 1. Find controller if not linked
  if (this->controller_ == nullptr) {
    this->controller_ = CFXControl::find(this->get_light_state());
  }

  // 2. Register ALL runners with the controller (segment runners or single
  // runner)
  if (this->controller_) {
    if (!this->segment_runners_.empty()) {
      for (auto *r : this->segment_runners_) {
        this->controller_->register_runner(r);
      }
    } else if (this->runner_) {
      this->controller_->register_runner(this->runner_);
    }
  }

  CFXControl *c = this->controller_;

  select::Select *palette_sel =
      (c && c->get_palette()) ? c->get_palette() : this->palette_;

  // --- Autotune Auto-Disable State Machine ---
  bool current_autotune_state =
      true; // Constraint: No switch = always respect defaults
  switch_::Switch *autotune_sw =
      (c && c->get_autotune()) ? c->get_autotune() : this->autotune_;
  if (autotune_sw != nullptr) {
    current_autotune_state = autotune_sw->state;
  }

  // Handle manual OFF -> ON transition
  if (current_autotune_state && !this->autotune_active_) {
    this->apply_autotune_defaults_();
    this->autotune_active_ = true;
  }
  // Handle manual ON -> OFF transition
  else if (!current_autotune_state && this->autotune_active_) {
    this->autotune_active_ = false;
  }
  // Handle expected ON state, but detecting manual UI overrides
  else if (current_autotune_state && this->autotune_active_ &&
           autotune_sw != nullptr) {
    bool manual_override = false;

    number::Number *speed_num =
        (c && c->get_speed()) ? c->get_speed() : this->speed_;
    if (speed_num && speed_num->state != this->autotune_expected_speed_)
      manual_override = true;

    number::Number *intensity_num =
        (c && c->get_intensity()) ? c->get_intensity() : this->intensity_;
    if (intensity_num &&
        intensity_num->state != this->autotune_expected_intensity_)
      manual_override = true;

    if (palette_sel && palette_sel->has_state() &&
        palette_sel->current_option() != this->autotune_expected_palette_)
      manual_override = true;

    if (manual_override) {
      autotune_sw->turn_off();
      this->autotune_active_ = false;
    }
  }

  // --- Visualizer: Dynamic Palette Sync ---
  if (palette_sel && palette_sel->has_state()) {
    const char *opt = palette_sel->current_option();
    std::string current_pal = opt ? opt : "";
    if (!current_pal.empty() && current_pal != this->last_sent_palette_) {
      auto *out = static_cast<cfx_light::CFXLightOutput *>(
          this->get_light_state()->get_output());
      if (out != nullptr) {
        out->send_visualizer_metadata(this->get_name(), current_pal);
      }
      this->last_sent_palette_ = current_pal;
    }
  }

  // --- Visualizer: Periodic Metadata Refresh (Every 5s) ---
  uint32_t now = millis();
  if (now - this->last_metadata_refresh_ > 5000) {
    auto *out = static_cast<cfx_light::CFXLightOutput *>(
        this->get_light_state()->get_output());
    if (out != nullptr) {
      std::string pal_name = "";
      if (palette_sel && palette_sel->has_state()) {
        const char *opt = palette_sel->current_option();
        if (opt != nullptr)
          pal_name = opt;
      }

      // Deep Palette Resolution: If UI says "Default", ask the runner what's
      // actually rendering
      if ((pal_name.empty() || pal_name == "Default") && this->runner_) {
        pal_name = this->get_palette_name_(this->runner_->getPalette());
      }

      out->send_visualizer_metadata(this->get_name(), pal_name);
    }
    this->last_metadata_refresh_ = now;
  }

  if (this->runner_) {
    // Helper lambda for Palette Index Lookup
    // New indices: 0=Default, 1=Aurora, 2=Forest, 3=Ocean, 4=Rainbow, etc.
    auto get_pal_idx = [this](select::Select *sel) -> uint8_t {
      if (!sel || !sel->has_state())
        return 0;
      const char *opt = sel->current_option();
      if (!opt)
        return 0;

      // Use strcmp instead of std::string to avoid heap allocation every frame
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
      if (strcmp(opt, "Lava") == 0)
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
      if (strcmp(opt, "Twilight") == 0)
        return 23;
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
      // 2. Speed (standalone mode)
      if (this->speed_)
        this->runner_->setSpeed((uint8_t)this->speed_->state);
      else
        this->runner_->setSpeed(this->get_default_speed_(this->effect_id_));

      // 3. Intensity (standalone mode)
      if (this->intensity_)
        this->runner_->setIntensity((uint8_t)this->intensity_->state);
      else
        this->runner_->setIntensity(
            this->get_default_intensity_(this->effect_id_));

      // 4. Palette (standalone mode)
      if (this->is_monochromatic_(this->effect_id_)) {
        this->runner_->setPalette(255);
      } else if (this->palette_) {
        uint8_t pal_idx = get_pal_idx(this->palette_);
        this->runner_->setPalette(pal_idx);
      } else {
        this->runner_->setPalette(
            this->get_default_palette_id_(this->effect_id_));
      }

      // 5. Mirror (standalone mode)
      if (this->mirror_)
        this->runner_->setMirror(this->mirror_->state);
    } else {
      // Controller present: only enforce monochromatic palette override
      // (PUSH callbacks don't know about monochromatic constraints)
      if (this->is_monochromatic_(this->effect_id_)) {
        if (!this->segment_runners_.empty()) {
          for (auto *r : this->segment_runners_)
            r->setPalette(255);
        } else {
          this->runner_->setPalette(255);
        }
      }
    }

    // 6. Intro Use Palette
    if (c && c->get_intro_use_palette())
      this->intro_use_palette_ = c->get_intro_use_palette();

    // 7. Debug
    if (c && c->get_debug())
      this->debug_switch_ = c->get_debug();
  }
}

// Intro Routine Implementation
void CFXAddressableLightEffect::run_intro(light::AddressableLight &it,
                                          const Color &target_color) {
  uint32_t elapsed = millis() - this->intro_start_time_;

  // Safety: If mode is NONE, abort immediately and release control
  // Ensure we clear the flag so next frame service() runs.
  if (this->active_intro_mode_ == INTRO_NONE) {
    this->intro_active_ = false;
    return;
  }

  uint32_t duration = 1000; // Initial Default: 1.0s
  number::Number *dur_num = this->intro_duration_;
  if (dur_num == nullptr && this->controller_ != nullptr)
    dur_num = this->controller_->get_intro_duration();

  MonochromaticPreset preset =
      this->get_monochromatic_preset_(this->effect_id_);

  if (dur_num != nullptr && dur_num->has_state()) {
    // High Priority: UI Slider
    duration = (uint32_t)(dur_num->state * 1000.0f);
  } else if (this->intro_duration_preset_.has_value()) {
    // Medium Priority: YAML Preset
    duration = (uint32_t)(this->intro_duration_preset_.value() * 1000.0f);
  } else if (preset.is_active) {
    // Monochromatic Preset Fallback: Speed Slider
    number::Number *speed_num = this->speed_;
    if (speed_num == nullptr && this->controller_ != nullptr)
      speed_num = this->controller_->get_speed();

    if (speed_num != nullptr && speed_num->has_state()) {
      // Map Speed (0-255) to Duration (500ms up to 10000ms)
      float speed_val = speed_num->state;
      duration = (uint32_t)(500.0f + (speed_val / 255.0f * 9500.0f));
    }
  }

  // Morse Code Timing Override
  if (this->active_intro_mode_ == INTRO_MODE_MORSE) {
    uint32_t speed_val = this->active_intro_speed_;
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
  uint8_t mode = this->active_intro_mode_;

  // 3. Setup Color/Palette
  // Use Target Color (remote_values) to ensure visibility during fade-in
  Color c = target_color;
  auto *state = this->get_light_state();
  if (state) {
    auto v = state->remote_values;
    c = Color((uint8_t)(v.get_red() * 255), (uint8_t)(v.get_green() * 255),
              (uint8_t)(v.get_blue() * 255), (uint8_t)(v.get_white() * 255));
  }
  if (c.r == 0 && c.g == 0 && c.b == 0 && c.w == 0) {
    c = Color::WHITE;
  }

  // Apply Force White logic to the Target Color
  switch_::Switch *force_white_sw =
      (this->controller_ && this->controller_->get_force_white())
          ? this->controller_->get_force_white()
          : this->force_white_;

  if (force_white_sw != nullptr && force_white_sw->state &&
      (this->get_default_palette_id_(this->effect_id_) == 255)) {
    uint8_t max_rgb = std::max(std::max(c.r, c.g), c.b);
    uint8_t target_w = std::max(c.w, max_rgb);
    c = Color(0, 0, 0, target_w);
  }

  // Apply user brightness scaling to target color
  float user_brightness = 1.0f;
  if (state) {
    user_brightness = state->remote_values.get_brightness();
    if (user_brightness < 0.01f)
      user_brightness = 0.01f;
  }
  c = Color((uint8_t)(c.r * user_brightness), (uint8_t)(c.g * user_brightness),
            (uint8_t)(c.b * user_brightness), (uint8_t)(c.w * user_brightness));

  // Check for Palette usage
  bool use_palette = false;
  uint8_t pal = 0;

  // New Feature: "Intro Use Palette" - Inherit from Runner's active effect
  // Explicitly handle switch state to avoid fall-through to Legacy auto-mode
  if (this->intro_use_palette_) {
    if (this->intro_use_palette_->state && this->runner_) {
      pal = this->runner_->_segment.palette;
      if (pal == 0) {
        // If Effect is using Default (0), resolve its Natural Palette ID
        pal = this->get_default_palette_id_(this->runner_->getMode());
      }

      // Fix: If resolved palette is Solid (255), ignore the switch and use
      // direct color This ensures we use the Target Color (c) instead of the
      // runner's Fading Color
      if (pal == 255) {
        use_palette = false;
      } else {
        use_palette = true;
      }
    } else {
      // Switch is OFF or Missing Runner -> Force Solid
      use_palette = false;
    }
  } else {
    // Legacy Behavior: Use explicit Intro Palette setting (if any)
    pal = this->get_palette_index_();
    if (pal > 0 && this->runner_ != nullptr) {
      use_palette = true;
    }
  }

  // Monochromatic Presets always force palette matching Active Effect Palette
  if (preset.is_active && this->runner_ != nullptr) {
    pal = this->runner_->_segment.palette;
    if (pal == 0)
      pal = this->get_default_palette_id_(this->effect_id_);
    if (pal == 255)
      use_palette = false;
    else
      use_palette = true;
  }

  if (use_palette && ::instance != nullptr) {
    // Force update the runner's palette immediately
    ::instance->_segment.palette = pal;
  }

  // Segment Aware Bounds
  int seg_start = 0;
  int seg_stop = it.size();
  int seg_len = it.size();
  if (::instance != nullptr) {
    seg_start = ::instance->_segment.start;
    seg_stop = ::instance->_segment.stop;
    seg_len = ::instance->_segment.length();
  }

  // Control State
  switch_::Switch *mirror_sw = this->mirror_;
  if (mirror_sw == nullptr && this->controller_ != nullptr)
    mirror_sw = this->controller_->get_mirror();

  bool reverse = false;
  if (mirror_sw != nullptr && mirror_sw->state)
    reverse = true;

  // Symmetry determined by Mode
  bool symmetry = false;
  if (mode == INTRO_MODE_CENTER) {
    symmetry = true;
    mode = INTRO_MODE_WIPE; // Use Wipe logic with symmetry
  }

  switch (mode) {
  case INTRO_MODE_WIPE: {
    int logical_len = symmetry ? (seg_len / 2) : seg_len;

    // Intensity defines blur radius (up to 50% of the segment)
    float blur_percent = 0.0f;
    number::Number *intensity_num = this->intensity_;
    if (intensity_num == nullptr && this->controller_ != nullptr) {
      intensity_num = this->controller_->get_intensity();
    }
    if (intensity_num != nullptr && intensity_num->has_state()) {
      blur_percent = (intensity_num->state / 255.0f) * 0.5f;
    }

    int blur_radius = (int)(logical_len * blur_percent);
    float exact_lead = progress * (logical_len + blur_radius);
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

      Color pixel_c = Color::BLACK;
      if (alpha > 0.0f) {
        if (use_palette) {
          uint8_t map_idx =
              (uint8_t)((i * 255) / (logical_len > 0 ? logical_len : 1));
          uint32_t cp = this->runner_->_segment.color_from_palette(
              map_idx, false, true, 255, 255);
          pixel_c = Color((uint8_t)(((cp >> 16) & 0xFF) * user_brightness),
                          (uint8_t)(((cp >> 8) & 0xFF) * user_brightness),
                          (uint8_t)((cp & 0xFF) * user_brightness), 0);
        } else {
          pixel_c = c; // Solid Color
        }

        // Apply Alpha Blending to Background (Black)
        if (alpha < 1.0f) {
          pixel_c =
              Color((uint8_t)(pixel_c.r * alpha), (uint8_t)(pixel_c.g * alpha),
                    (uint8_t)(pixel_c.b * alpha), (uint8_t)(pixel_c.w * alpha));
        }
      }

      // Apply
      // Respect segment bounds and mirror
      int global_idx1 = seg_start + i;
      int global_idx2 = seg_stop - 1 - i;

      it[global_idx1] = pixel_c;
      if (symmetry && global_idx2 >= 0) {
        it[global_idx2] = pixel_c;
      }
    }
    if (symmetry && (seg_len % 2 != 0)) {
      int mid = seg_start + (seg_len / 2);
      bool fill_center = (progress >= 1.0f) || (reverse && lead > 0);
      if (fill_center) {
        if (use_palette && this->runner_) {
          uint8_t map_idx = 128; // Center of palette gradient
          uint32_t cp = this->runner_->_segment.color_from_palette(
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
      if (use_palette && ::instance) {
        // Map whole segment to spectrum for Fade
        uint8_t map_idx = (uint8_t)((i * 255) / (seg_len > 0 ? seg_len : 1));
        uint32_t cp = ::instance->_segment.color_from_palette(map_idx, false,
                                                              true, 255, 255);
        base_c = Color((uint8_t)(((cp >> 16) & 0xFF) * user_brightness),
                       (uint8_t)(((cp >> 8) & 0xFF) * user_brightness),
                       (uint8_t)((cp & 0xFF) * user_brightness),
                       (uint8_t)(((cp >> 24) & 0xFF) * user_brightness));
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
        if (use_palette && ::instance) {
          uint8_t map_idx = (uint8_t)((i * 255) / (seg_len > 0 ? seg_len : 1));
          uint32_t cp = ::instance->_segment.color_from_palette(map_idx, false,
                                                                true, 255, 255);
          pixel_c = Color((uint8_t)(((cp >> 16) & 0xFF) * user_brightness),
                          (uint8_t)(((cp >> 8) & 0xFF) * user_brightness),
                          (uint8_t)((cp & 0xFF) * user_brightness),
                          (uint8_t)(((cp >> 24) & 0xFF) * user_brightness));
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
        if (use_palette && ::instance) {
          uint8_t map_idx =
              (uint8_t)((idx * 255) / (seg_len > 0 ? seg_len : 1));
          uint32_t cp = ::instance->_segment.color_from_palette(map_idx, false,
                                                                true, 255, 255);
          base_c = Color((uint8_t)(((cp >> 16) & 0xFF) * user_brightness),
                         (uint8_t)(((cp >> 8) & 0xFF) * user_brightness),
                         (uint8_t)((cp & 0xFF) * user_brightness),
                         (uint8_t)(((cp >> 24) & 0xFF) * user_brightness));
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
  case INTRO_MODE_MORSE: {
    uint32_t speed = this->active_intro_speed_;
    uint32_t unit_ms = 80 + ((255 - speed) * 100 / 255);
    uint32_t elapsed_m = millis() - this->intro_start_time_;
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
        if (::instance) {
          uint32_t cp = ::instance->_segment.color_from_palette(map_idx, false,
                                                                true, 255, 255);
          it[global_idx] =
              Color((uint8_t)(((cp >> 16) & 0xFF) * user_brightness),
                    (uint8_t)(((cp >> 8) & 0xFF) * user_brightness),
                    (uint8_t)((cp & 0xFF) * user_brightness),
                    (uint8_t)(((cp >> 24) & 0xFF) * user_brightness));
        } else {
          it[global_idx] = Color((uint8_t)(target_color.r * user_brightness),
                                 (uint8_t)(target_color.g * user_brightness),
                                 (uint8_t)(target_color.b * user_brightness),
                                 (uint8_t)(target_color.w * user_brightness));
        }
      } else {
        it[global_idx] = Color::BLACK;
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

  uint32_t duration_ms = this->active_outro_duration_ms_;

  uint32_t elapsed = millis() - this->outro_start_time_;
  float progress = (float)elapsed / (duration_ms > 0 ? duration_ms : 1);
  if (progress > 1.0f)
    progress = 1.0f;

  float fade_scaler = 1.0f - progress;

  // 1. Advance the underlying effect in the background
  runner->service();

  // 1b. CRITICAL: Stop ESPHome's internal transition from dimming our pixels!
  // ESPHome is actively scaling the brightness down to 0.0f over the
  // default_transition_length. We must temporarily force current_values
  // brightness to 1.0f so the effect renders at full brightness. Our custom
  // Outro logic (Wipe/Fade) handles the dimming.
  float original_brightness = 0.0f;
  float user_brightness = 1.0f;
  auto *ls = this->get_light_state();
  if (ls != nullptr) {
    original_brightness = ls->current_values.get_brightness();
    // Capture user's intended brightness BEFORE forcing to 1.0
    user_brightness = ls->remote_values.get_brightness();
    if (user_brightness < 0.01f)
      user_brightness = 0.01f;
    ls->current_values.set_brightness(1.0f);
  }

  // 2. Render background frame onto the output buffer (scaled by user
  // brightness)
  int seg_start = runner->_segment.start;
  int seg_stop = runner->_segment.stop;
  int seg_len = runner->_segment.length();

  for (int i = 0; i < seg_len; i++) {
    int global_idx = seg_start + i;
    uint32_t c = runner->_segment.getPixelColor(i);
    uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * user_brightness);
    uint8_t g = (uint8_t)(((c >> 8) & 0xFF) * user_brightness);
    uint8_t b = (uint8_t)((c & 0xFF) * user_brightness);
    uint8_t w = (uint8_t)(((c >> 24) & 0xFF) * user_brightness);
    it[global_idx] = Color(r, g, b, w);
  }

  // Restore the scaling factor so we don't permanently corrupt the LightState
  if (ls != nullptr) {
    ls->current_values.set_brightness(original_brightness);
  }

  uint8_t mode = this->active_outro_mode_;

  // Control State for Mirror (affects wipe direction)
  switch_::Switch *mirror_sw = this->mirror_;
  if (mirror_sw == nullptr && this->controller_ != nullptr)
    mirror_sw = this->controller_->get_mirror();

  bool reverse = false;
  if (mirror_sw != nullptr && mirror_sw->state)
    reverse = true;

  bool symmetry = false;
  if (mode == INTRO_MODE_CENTER) {
    symmetry = true;
    mode = INTRO_MODE_WIPE;
  }

  switch (mode) {
  case INTRO_MODE_WIPE: {
    int logical_len = symmetry ? (seg_len / 2) : seg_len;

    // Intensity defines blur radius (up to 50% of the strip)
    // Use the cached active_outro_intensity_ because controller_ is null during
    // Outro
    float blur_percent = (this->active_outro_intensity_ / 255.0f) * 0.5f;

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

      if (alpha == 0.0f) {
        it[seg_start + i] = Color::BLACK;
        if (symmetry)
          it[seg_stop - 1 - i] = Color::BLACK;
      } else if (alpha < 1.0f) {
        // Dim the background frame by alpha
        Color c1 = it[seg_start + i].get();
        it[seg_start + i] =
            Color((uint8_t)(c1.r * alpha), (uint8_t)(c1.g * alpha),
                  (uint8_t)(c1.b * alpha), (uint8_t)(c1.w * alpha));
        if (symmetry) {
          Color c2 = it[seg_stop - 1 - i].get();
          it[seg_stop - 1 - i] =
              Color((uint8_t)(c2.r * alpha), (uint8_t)(c2.g * alpha),
                    (uint8_t)(c2.b * alpha), (uint8_t)(c2.w * alpha));
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
  case INTRO_MODE_MORSE: {
    uint32_t unit_ms = 80 + ((255 - this->active_outro_intensity_) * 100 / 255);
    uint32_t elapsed_morse = millis() - this->outro_start_time_;
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
  CFXControl *c = this->controller_;

  // 1. Speed
  number::Number *speed_num =
      (c && c->get_speed()) ? c->get_speed() : this->speed_;
  if (speed_num != nullptr && !this->speed_preset_.has_value()) {
    float target = (float)this->get_default_speed_(this->effect_id_);
    if (speed_num->state != target) {
      auto call = speed_num->make_call();
      call.set_value(target);
      call.perform();
    }
    this->autotune_expected_speed_ = target;
  } else if (speed_num != nullptr) {
    this->autotune_expected_speed_ = speed_num->state;
  }

  // 2. Intensity
  number::Number *intensity_num =
      (c && c->get_intensity()) ? c->get_intensity() : this->intensity_;
  if (intensity_num != nullptr && !this->intensity_preset_.has_value()) {
    float target = (float)this->get_default_intensity_(this->effect_id_);
    if (intensity_num->state != target) {
      auto call = intensity_num->make_call();
      call.set_value(target);
      call.perform();
    }
    this->autotune_expected_intensity_ = target;
  } else if (intensity_num != nullptr) {
    this->autotune_expected_intensity_ = intensity_num->state;
  }

  // 3. Palette
  select::Select *palette_sel =
      (c && c->get_palette()) ? c->get_palette() : this->palette_;
  if (palette_sel != nullptr && !this->palette_preset_.has_value()) {
    uint8_t default_pal_id = this->get_default_palette_id_(this->effect_id_);
    std::string pal_name = this->get_palette_name_(default_pal_id);
    if (palette_sel->current_option() != pal_name) {
      auto call = palette_sel->make_call();
      call.set_option(pal_name);
      call.perform();
    }
    this->autotune_expected_palette_ = pal_name;
  } else if (palette_sel != nullptr) {
    this->autotune_expected_palette_ = palette_sel->current_option();
  }
}

} // namespace chimera_fx
} // namespace esphome
