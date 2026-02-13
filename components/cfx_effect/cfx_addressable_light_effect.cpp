/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * This file is part of the ChimeraFX for ESPHome.
 */

#include "cfx_addressable_light_effect.h"
#include "cfx_compat.h"
#include "esphome/core/hal.h" // For millis()
#include "esphome/core/log.h"


namespace esphome {
namespace chimera_fx {

std::vector<CFXControl *> CFXControl::instances; // Define static vector

static const char *TAG = "chimera_fx";

CFXAddressableLightEffect::CFXAddressableLightEffect(const char *name)
    : light::AddressableLightEffect(name) {}

void CFXAddressableLightEffect::start() {
  light::AddressableLightEffect::start();

  this->run_controls_();

  CFXControl *c = this->controller_;

  number::Number *speed_num =

      (c && c->get_speed()) ? c->get_speed() : this->speed_;
  if (speed_num != nullptr && this->speed_preset_.has_value()) {
    float target = (float)this->speed_preset_.value();
    if (speed_num->state != target) {
      auto call = speed_num->make_call();
      call.set_value(target);
      call.perform();
    }
  } else if (speed_num != nullptr) {
    // No YAML preset: apply per-effect code default and sync slider
    float target = (float)this->get_default_speed_(this->effect_id_);
    if (speed_num->state != target) {
      auto call = speed_num->make_call();
      call.set_value(target);
      call.perform();
    }
  }

  // 2. Intensity
  number::Number *intensity_num =
      (c && c->get_intensity()) ? c->get_intensity() : this->intensity_;
  if (intensity_num != nullptr && this->intensity_preset_.has_value()) {
    float target = (float)this->intensity_preset_.value();
    if (intensity_num->state != target) {
      auto call = intensity_num->make_call();
      call.set_value(target);
      call.perform();
    }
  } else if (intensity_num != nullptr) {
    // No YAML preset: apply per-effect code default and sync slider
    float target = (float)this->get_default_intensity_(this->effect_id_);
    if (intensity_num->state != target) {
      auto call = intensity_num->make_call();
      call.set_value(target);
      call.perform();
    }
  }

  // 3. Palette (Fixed: Resolve from controller)
  select::Select *palette_sel =
      (c && c->get_palette()) ? c->get_palette() : this->palette_;
  if (palette_sel != nullptr && this->palette_preset_.has_value()) {
    // NOTE: Select uses strings, but we have an index preset.
    // Optimally we should check index, but Select component typically stores
    // state as String. We can't easily check 'state != value' without mapping
    // index to string. For Safety, we SKIP optimization for Dropdowns to ensure
    // index is enforced.
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
  if (intro_sel != nullptr && this->intro_preset_.has_value()) {
    // Same as Palette: Force update to be safe with Index->String mapping
    auto call = intro_sel->make_call();
    call.set_index(this->intro_preset_.value());
    call.perform();
  }

  // 6. Intro Duration
  number::Number *intro_dur_num = (c && c->get_intro_duration())
                                      ? c->get_intro_duration()
                                      : this->intro_duration_;
  if (intro_dur_num != nullptr && this->intro_duration_preset_.has_value()) {
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
  if (intro_pal_sw != nullptr && this->intro_use_palette_preset_.has_value()) {
    bool target = this->intro_use_palette_preset_.value();
    if (intro_pal_sw->state != target) {
      if (target) {
        intro_pal_sw->turn_on();
      } else {
        intro_pal_sw->turn_off();
      }
    }
  }

  // 8. Timer
  number::Number *timer_num = (c) ? c->get_timer() : nullptr;
  if (timer_num != nullptr && this->timer_preset_.has_value()) {
    float target = (float)this->timer_preset_.value();
    if (timer_num->state != target) {
      auto call = timer_num->make_call();
      call.set_value(target);
      call.perform();
    }
  }

  // === END PRESETS ===

  // State Machine Init: Check if we are turning ON from OFF
  auto *state = this->get_light_state();
  if (state != nullptr) {
    bool is_fresh_turn_on = !state->current_values.is_on();
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
      else
        this->active_intro_mode_ = INTRO_NONE;
    }

    // Cache for this run
    this->intro_effect_ = intro_sel;

    if (this->active_intro_mode_ == INTRO_NONE) {
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
}

void CFXAddressableLightEffect::stop() {
  light::AddressableLightEffect::stop();

  // Clear intro snapshot vector to reclaim RAM
  intro_snapshot_.clear();
  intro_snapshot_.shrink_to_fit();

  if (this->runner_ != nullptr) {
    if (this->controller_) {
      this->controller_->unregister_runner(this->runner_);
    }
    delete this->runner_; // Destructor calls deallocateData()
    this->runner_ = nullptr;
  }
  this->controller_ = nullptr;
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

  // --- Start Runner --- on first apply
  if (this->runner_ == nullptr) {
    this->runner_ = new CFXRunner(&it);
    this->runner_->setMode(this->effect_id_);
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

  // Pass current light color to segment for solid color mode (palette 255/0)
  uint32_t color = (uint32_t(current_color.red) << 16) |
                   (uint32_t(current_color.green) << 8) |
                   uint32_t(current_color.blue);
  this->runner_->setColor(color);

  // === Dynamic Gamma Update ===
  // Sync the Runner's gamma LUT with the light's current gamma setting
  float current_gamma = this->get_light_state()->get_gamma_correct();
  // setGamma checks for change internally (float comparison), so simple call is
  // safe-ish but to avoid function call overhead we can check locally if we
  // wanted. We'll trust setGamma or just check here. setGamma does a check but
  // re-calcs LUT. Let's rely on setGamma's internal check (which I implemented:
  // if (g < 0.1) ... _gamma = g ...). Wait, I didn't verify if I added a
  // 'change check' efficiently in setGamma. Let's add a check here to be safe
  // and efficient.
  if (abs(this->runner_->_gamma - current_gamma) > 0.01f) {
    this->runner_->setGamma(current_gamma);
  }

  // === State Machine: Intro vs Main Effect ===
  if (this->intro_active_) {
    this->run_intro(it, current_color);

    // Check for Intro Completion
    uint32_t duration_ms = 3000; // Default 3s

    // Resolve duration from local component OR linked controller
    number::Number *dur_num = this->intro_duration_;
    if (dur_num == nullptr && this->controller_ != nullptr)
      dur_num = this->controller_->get_intro_duration();

    if (dur_num != nullptr && dur_num->has_state()) {
      duration_ms = (uint32_t)(dur_num->state * 1000.0f);
    }

    if (millis() - this->intro_start_time_ > duration_ms) {
      this->intro_active_ = false;

      // Check if Transition is enabled via config
      float trans_dur = (this->transition_duration_ != nullptr &&
                         this->transition_duration_->has_state())
                            ? this->transition_duration_->state
                            : 1.5f;

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
    // Main CFX effect Running
    this->runner_->service();

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
  switch (effect_id) {
  // Solid Defaults (255)
  case 0:
    return 255; // Static
  case 1:
    return 255; // Blink
  case 2:
    return 255; // Breath
  case 3:
    return 255; // Wipe
  case 4:
    return 255; // Wipe Random
  case 6:
    return 255; // Wipe Sweep (ID=6)
  case 91:
    return 255; // Bouncing Balls

  // Rainbow Defaults (4)
  case 7:
    return 4; // Dynamic
  case 8:
    return 4; // Rainbow (ID=8)
  case 9:
    return 4; // Rainbow Cycle (Color Loop) (ID=9)
  case 64:
    return 4; // Juggle
  case 74:
    return 4; // ColorTwinkle
  case 79:
    return 4; // Ripple
  case 105:
    return 4; // Phased
  case 107:
    return 4; // Noise Pal
  case 110:
    return 4; // Flow

  // Party Defaults (8)
  case 97:
    return 8; // Plasma (ID=97)
  case 63:
    return 8; // Pride 2015

  // Specific Defaults
  case 18:
    return 255; // Dissolve -> Solid (User override)
  case 38:
    return 1; // Aurora -> Aurora palette
  case 53:
    return 5; // Fire Dual -> Fire palette (same as Fire 2012)
  case 66:
    return 5; // Fire 2012 -> Fire
  case 76:
    return 255; // Meteor -> Solid (WLED default)
  case 40:
    return 255; // Scanner -> Solid
  case 60:
    return 255; // Scanner Dual -> Solid
  case 101:
    return 11; // Pacifica -> Pacifica
  case 104:
    return 12; // Sunrise -> HeatColors

  // Default Aurora (1) or specific handling
  default:
    return 1; // Aurora is the general default
  }
}

uint8_t CFXAddressableLightEffect::get_default_speed_(uint8_t effect_id) {
  // Per-effect speed defaults from effects_preset.md
  switch (effect_id) {
  case 38:
    return 24; // Aurora
  case 53:
    return 64; // Fire Dual (same as Fire 2012)
  case 64:
    return 64; // Juggle
  case 66:
    return 64; // Fire 2012
  case 104:
    return 60; // Sunrise
  default:
    return 128; // WLED default
  }
}

uint8_t CFXAddressableLightEffect::get_default_intensity_(uint8_t effect_id) {
  // Per-effect intensity defaults from effects_preset.md
  switch (effect_id) {
  case 53:
    return 160; // Fire Dual (same as Fire 2012)
  case 66:
    return 160; // Fire 2012
  default:
    return 128; // WLED default
  }
}

void CFXAddressableLightEffect::run_controls_() {
  // 1. Find controller if not linked
  if (this->controller_ == nullptr) {
    this->controller_ = CFXControl::find(this->get_light_state());
    if (this->controller_ && this->runner_) {
      this->controller_->register_runner(this->runner_);
    }
  }

  if (this->controller_ && this->runner_) {
    // Ensure registration happens even if controller was found previously
    this->controller_->register_runner(this->runner_);
  }

  CFXControl *c = this->controller_;

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
      if (strcmp(opt, "Fairy") == 0)
        return 22;
      if (strcmp(opt, "Twilight") == 0)
        return 23;
      if (strcmp(opt, "Default") == 0) {
        // Resolve the natural default for this effect
        if (this->runner_) {
          uint8_t m = this->runner_->getMode();
          return this->get_default_palette_id_(m);
        }
        return 1; // Fallback to Aurora if no runner
      }
      return 0; // Unknown palette name
    };
    // 2. Speed
    if (c && c->get_speed())
      this->runner_->setSpeed((uint8_t)c->get_speed()->state);
    else if (this->speed_)
      this->runner_->setSpeed((uint8_t)this->speed_->state);
    else {
      // FALLBACK: No control component - use per-effect default
      this->runner_->setSpeed(this->get_default_speed_(this->effect_id_));
    }

    // 3. Intensity
    if (c && c->get_intensity())
      this->runner_->setIntensity((uint8_t)c->get_intensity()->state);
    else if (this->intensity_)
      this->runner_->setIntensity((uint8_t)this->intensity_->state);
    else {
      // FALLBACK: No control component - use per-effect default
      this->runner_->setIntensity(
          this->get_default_intensity_(this->effect_id_));
    }

    // 4. Palette
    if (c && c->get_palette()) {
      uint8_t pal_idx = get_pal_idx(c->get_palette());
      this->runner_->setPalette(pal_idx);
    } else if (this->palette_) {
      uint8_t pal_idx = get_pal_idx(this->palette_);
      this->runner_->setPalette(pal_idx);
    } else {
      // FALLBACK: No control component - use per-effect default
      uint8_t default_pal = this->get_default_palette_id_(this->effect_id_);
      this->runner_->setPalette(default_pal);
    }

    // 5. Mirror
    if (c && c->get_mirror())
      this->runner_->setMirror(c->get_mirror()->state);
    else if (this->mirror_)
      this->runner_->setMirror(this->mirror_->state);

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

  uint32_t duration = 3000;
  number::Number *dur_num = this->intro_duration_;
  if (dur_num == nullptr && this->controller_ != nullptr)
    dur_num = this->controller_->get_intro_duration();

  if (dur_num != nullptr && dur_num->has_state()) {
    duration = (uint32_t)(dur_num->state * 1000.0f);
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

  if (use_palette && this->runner_ != nullptr) {
    // Force update the runner's palette immediately
    this->runner_->_segment.palette = pal;
  }

  int num_leds = it.size();

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
    int logical_len = symmetry ? (num_leds / 2) : num_leds;
    int lead = (int)(progress * logical_len);

    // Safety: ensure lead covers full range at 100%
    if (progress >= 1.0f)
      lead = logical_len + 1;

    for (int i = 0; i < logical_len; i++) {
      bool active = false;
      if (!reverse) {
        // Standard: Fill from start (0) to lead
        if (i <= lead)
          active = true;
      } else {
        // Reverse: Fill from end (logical_len-1) back to 0
        // For Wipe: End -> Start
        // For Center: Center -> Edges
        if (i >= (logical_len - 1 - lead))
          active = true;
      }

      Color pixel_c = Color::BLACK;
      if (active) {
        if (use_palette) {
          // Map i to 0-255 for palette (Gradient Wipe)
          uint8_t map_idx =
              (uint8_t)((i * 255) / (logical_len > 0 ? logical_len : 1));
          // Get color from runner
          uint32_t cp = this->runner_->_segment.color_from_palette(
              map_idx, false, true, 255, 255);
          pixel_c = Color((cp >> 16) & 0xFF, (cp >> 8) & 0xFF, cp & 0xFF, 0);
        } else {
          pixel_c = c;
        }
      }

      // Apply
      it[i] = pixel_c;
      if (symmetry) {
        it[num_leds - 1 - i] = pixel_c;
      }
    }
    if (symmetry && (num_leds % 2 != 0)) {
      int mid = num_leds / 2;
      bool fill_center = (progress >= 1.0f) || (reverse && lead > 0);
      if (fill_center) {
        if (use_palette && this->runner_) {
          uint8_t map_idx = 128; // Center of palette gradient
          uint32_t cp = this->runner_->_segment.color_from_palette(
              map_idx, false, true, 255, 255);
          it[mid] = Color((cp >> 16) & 0xFF, (cp >> 8) & 0xFF, cp & 0xFF, 0);
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

    for (int i = 0; i < num_leds; i++) {
      Color base_c = c;
      if (use_palette) {
        // Map whole strip to spectrum for Fade
        uint8_t map_idx = (uint8_t)((i * 255) / (num_leds > 0 ? num_leds : 1));
        uint32_t cp = this->runner_->_segment.color_from_palette(
            map_idx, false, true, 255, 255);
        base_c = Color((cp >> 16) & 0xFF, (cp >> 8) & 0xFF, cp & 0xFF, 0);
      }

      // Explicit Scaling avoiding operator ambiguity
      it[i] =
          Color((base_c.r * brightness) / 255, (base_c.g * brightness) / 255,
                (base_c.b * brightness) / 255, (base_c.w * brightness) / 255);
    }
    break;
  }
  case INTRO_MODE_GLITTER: {
    // Dissolve / Glitter Effect
    // Random pixels turn on based on progress
    uint8_t threshold = (uint8_t)(progress * 255.0f);

    for (int i = 0; i < num_leds; i++) {
      // Deterministic pseudo-random value for this pixel position
      // so it stays ON once it turns ON (Dissolve behavior)
      uint16_t hash = (i * 33) + (i * i);
      uint8_t val = hash % 256;

      bool active = (threshold >= val);

      Color pixel_c = Color::BLACK;
      if (active) {
        if (use_palette) {
          uint8_t map_idx =
              (uint8_t)((i * 255) / (num_leds > 0 ? num_leds : 1));
          uint32_t cp = this->runner_->_segment.color_from_palette(
              map_idx, false, true, 255, 255);
          pixel_c = Color((cp >> 16) & 0xFF, (cp >> 8) & 0xFF, cp & 0xFF, 0);
        } else {
          pixel_c = c;
        }
      }
      it[i] = pixel_c;
    }
    break;
  }
  case INTRO_MODE_NONE:
  default:
    for (int i = 0; i < num_leds; i++) {
      it[i] = Color::BLACK;
    }
    break;
  }
}
} // namespace chimera_fx
} // namespace esphome
