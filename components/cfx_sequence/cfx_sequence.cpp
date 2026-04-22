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
#include "../cfx_light/cfx_virtual_segment_light.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/light/light_call.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/event/event.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include <cstring>

namespace esphome {
namespace cfx_sequence {

static const char *const TAG = "cfx_sequence";

std::vector<CFXSequence *> CFXSequence::instances;
CFXSequenceSelect *CFXSequenceSelect::instance = nullptr;
std::atomic<bool> CFXSequenceSelect::suppress_callback_{false};
CFXSequence *CFXSequence::current_trigger_sequence_ = nullptr;

namespace {
class SequenceTriggerContextGuard {
public:
  explicit SequenceTriggerContextGuard(CFXSequence *seq)
      : previous_(CFXSequence::get_current_trigger_sequence()) {
    CFXSequence::set_current_trigger_sequence(seq);
  }

  ~SequenceTriggerContextGuard() {
    CFXSequence::set_current_trigger_sequence(previous_);
  }

private:
  CFXSequence *previous_;
};

static cfx_light::CFXLightOutput *resolve_cfx_output_(light::LightState *state) {
  if (state == nullptr) {
    return nullptr;
  }

  auto *output = state->get_output();
  if (output == nullptr) {
    return nullptr;
  }

  for (auto *seg : cfx_light::CFXVirtualSegmentLight::all_segments) {
    if (seg == output) {
      return seg->get_parent();
    }
  }

  return static_cast<cfx_light::CFXLightOutput *>(output);
}

static bool has_active_spi_effect_() {
  auto has_active_spi_in_group =
      [](const std::vector<chimera_fx::CFXAddressableLightEffect *> &group) {
        for (auto *inst : group) {
          if (inst == nullptr || inst->get_act() == nullptr) {
            continue;
          }
          auto *diag_out = inst->get_diag_output();
          if (diag_out != nullptr && diag_out->is_spi_transport()) {
            return true;
          }
        }
        return false;
      };

  return has_active_spi_in_group(chimera_fx::CFXAddressableLightEffect::all_effects) ||
         has_active_spi_in_group(chimera_fx::CFXAddressableLightEffect::all_segment_effects);
}

static bool should_defer_pool_sequence_start_(light::LightState *target_light) {
  auto *target_out = resolve_cfx_output_(target_light);
  if (target_out != nullptr && target_out->is_spi_transport()) {
    return true;
  }
  return has_active_spi_effect_();
}

static void ensure_sequence_registry_capacity_(size_t extra_slots,
                                               const char *reason) {
  auto &registry = CFXSequence::instances;
  const size_t needed = registry.size() + extra_slots;
  if (registry.capacity() >= needed)
    return;

  size_t target = needed + 8;
  registry.reserve(target);
  ESP_LOGW("cfx_run",
           "Sequence registry reserve[%s]: size=%u capacity=%u target=%u",
           reason != nullptr ? reason : "?",
           static_cast<unsigned>(registry.size()),
           static_cast<unsigned>(registry.capacity()),
           static_cast<unsigned>(target));
}
}  // namespace

static light::ColorMode resolve_cfx_call_color_mode(light::LightState *light,
                                                    bool prefer_white) {
  if (prefer_white &&
      light->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE)) {
    return light::ColorMode::RGB_WHITE;
  }
  if (light->get_traits().supports_color_mode(light::ColorMode::RGB)) {
    return light::ColorMode::RGB;
  }
  if (light->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE)) {
    return light::ColorMode::RGB_WHITE;
  }
  if (prefer_white &&
      light->get_traits().supports_color_mode(light::ColorMode::WHITE)) {
    return light::ColorMode::WHITE;
  }

  auto valid_mode = light->remote_values.get_color_mode();
  if (valid_mode != light::ColorMode::UNKNOWN) {
    return valid_mode;
  }
  if (light->get_traits().supports_color_mode(light::ColorMode::WHITE)) {
    return light::ColorMode::WHITE;
  }
  if (light->get_traits().supports_color_mode(light::ColorMode::BRIGHTNESS)) {
    return light::ColorMode::BRIGHTNESS;
  }
  return light::ColorMode::ON_OFF;
}

static void apply_cfx_user_color(light::LightState *light, light::LightCall &call,
                                 bool has_color, uint8_t r, uint8_t g,
                                 uint8_t b, uint8_t w,
                                 bool color_has_white) {
  if (!has_color)
    return;

  auto mode = resolve_cfx_call_color_mode(light, color_has_white);
  call.set_color_mode(mode);

  if (mode == light::ColorMode::RGB || mode == light::ColorMode::RGB_WHITE) {
    call.set_rgb(r / 255.0f, g / 255.0f, b / 255.0f);
  }
  if (color_has_white &&
      (mode == light::ColorMode::RGB_WHITE ||
       mode == light::ColorMode::WHITE ||
       mode == light::ColorMode::RGB_COLD_WARM_WHITE ||
       mode == light::ColorMode::COLD_WARM_WHITE)) {
    call.set_white(w / 255.0f);
  }
}



// ── CFXRunPool ────────────────────────────────────────────────────────────────

CFXRunPool &CFXRunPool::get() {
  static CFXRunPool inst;
  return inst;
}

void CFXRunPool::ensure_initialized_() {
  if (this->initialized_) return;
  this->initialized_ = true;
  ensure_sequence_registry_capacity_(POOL_SIZE + 8, "pool-init");
  for (uint8_t i = 0; i < POOL_SIZE; i++) {
    // Heap-allocate each slot sequence once. They live for the firmware lifetime.
    // Using a placeholder id/name/effect — overwritten on each claim().
    std::string slot_id = "cfx_run_pool_" + std::to_string(i);
    this->sequences_[i] = new CFXSequence(slot_id, slot_id, "", false);
    // Remove from instances immediately — pool sequences self-manage registration.
    auto &v = CFXSequence::instances;
    v.erase(std::remove(v.begin(), v.end(), this->sequences_[i]), v.end());
    this->slots_[i].sequence = this->sequences_[i];
    this->slots_[i].in_use   = false;
  }
}

CFXSequence *CFXRunPool::claim(uint8_t depth) {
  ensure_initialized_();
  ensure_sequence_registry_capacity_(1, "claim");

  if (depth >= CFX_RUN_MAX_DEPTH) {
    ESP_LOGW("cfx_run", "Max nesting depth %u reached — cfx_run suppressed",
             CFX_RUN_MAX_DEPTH);
    return nullptr;
  }

  for (uint8_t i = 0; i < POOL_SIZE; i++) {
    if (!this->slots_[i].in_use) {
      this->slots_[i].in_use = true;
      this->slots_[i].depth  = depth;
      CFXSequence *seq = this->slots_[i].sequence;
      seq->runtime_depth_ = depth;
      seq->is_pool_owned_ = true;
      seq->completion_reported_ = false; // reset here, not in release() — avoids race with deferred callbacks
      // Register in the global instances list for the duration of this run.
      CFXSequence::instances.push_back(seq);
      ESP_LOGD("cfx_run",
               "Pool slot %u claimed (depth %u, registry size=%u cap=%u)",
               i, depth,
               static_cast<unsigned>(CFXSequence::instances.size()),
               static_cast<unsigned>(CFXSequence::instances.capacity()));
      return seq;
    }
  }

  ESP_LOGW("cfx_run", "Pool exhausted (%u slots in use) — cfx_run suppressed",
           POOL_SIZE);
  return nullptr;
}

void CFXRunPool::release(CFXSequence *seq) {
  for (uint8_t i = 0; i < POOL_SIZE; i++) {
    if (this->slots_[i].sequence == seq && this->slots_[i].in_use) {
      this->slots_[i].in_use = false;
      seq->detach_runtime_parent_();
      std::vector<CFXSequence *> orphaned_children = seq->runtime_children_;
      for (auto *child : orphaned_children) {
        if (child != nullptr) {
          child->runtime_parent_ = nullptr;
        }
      }
      seq->runtime_children_.clear();
      seq->is_pool_owned_ = false;
      // Remove from global instances so it doesn't appear in stop_all() etc.
      auto &v = CFXSequence::instances;
      v.erase(std::remove(v.begin(), v.end(), seq), v.end());
      // Reset sequence state for next claim.
      seq->configured_light_count_ = 0;
      seq->lights_.clear();
      seq->on_reach_triggers_.clear();
      seq->on_complete_triggers_.clear();
      seq->on_stop_triggers_.clear();
      seq->on_start_triggers_.clear();
      seq->saved_states_.clear();
      seq->pending_reach_triggers_.clear();
      seq->fired_reach_triggers_.clear();
      seq->strip_tag_ = "";
      seq->effect_    = "";
      seq->speed_     = {};
      seq->intensity_ = {};
      seq->palette_   = {};
      seq->brightness_= {};
      seq->has_color_ = false;
      seq->color_r_   = 0;
      seq->color_g_   = 0;
      seq->color_b_   = 0;
      seq->color_w_   = 0;
      seq->color_has_white_ = false;
      seq->mirror_    = {};
      seq->intro_     = {};
      seq->outro_     = {};
      seq->inout_duration_ = {};
      seq->force_white_ = {};
      seq->autotune_ = {};
      seq->iterations_ = 0;
      seq->duration_ms_ = 0;
      seq->runtime_depth_ = 0;
      seq->runtime_parent_ = nullptr;
      seq->is_running_  = false;
      seq->is_starting_ = false;
      seq->is_stopping_ = false;
      seq->completion_reported_ = true; // keep true — deferred callbacks check this guard
      seq->last_fired_milestone_ = 0;
      seq->last_triggered_percentage_ = -1.0f;
      seq->last_triggered_pixel_ = -1;
      ESP_LOGD("cfx_run", "Pool slot %u released (registry size=%u cap=%u)", i,
               static_cast<unsigned>(CFXSequence::instances.size()),
               static_cast<unsigned>(CFXSequence::instances.capacity()));
      return;
    }
  }
}

bool CFXRunPool::is_pool_owned(CFXSequence *seq) const {
  for (uint8_t i = 0; i < POOL_SIZE; i++) {
    if (this->slots_[i].sequence == seq)
      return this->slots_[i].in_use;
  }
  return false;
}

// ── CfxRunActionBase::do_play_() ─────────────────────────────────────────────

void CfxRunActionBase::do_play_() {
  if (this->light_ == nullptr || this->effect_.empty()) {
    ESP_LOGW("cfx_run", "cfx_run: light or effect not set — skipped");
    return;
  }

  // Deduplication guard: if any active pool sequence is already running this
  // exact light+effect combination, skip. This prevents on_cfx_reach (which
  // fires every sweep cycle after the trigger position is crossed) from
  // spawning a new instance every frame, killing the running one, and looping.
  CFXRunPool &pool = CFXRunPool::get();
  for (auto *seq : CFXSequence::instances) {
    if (pool.is_pool_owned(seq) && seq->is_running() &&
        seq->owns_light(this->light_) &&
        seq->effect_ == this->effect_) {
      ESP_LOGV("cfx_run",
               "cfx_run: '%s' on '%s' already running — skipped duplicate.",
               this->effect_.c_str(), this->light_->get_name().c_str());
      return;
    }
  }
  CFXSequence *parent_seq = CFXSequence::get_current_trigger_sequence();
  uint8_t effective_depth = this->nesting_depth_;
  if (parent_seq != nullptr) {
    effective_depth = parent_seq->runtime_depth_ + 1;
  }
  CFXSequence *seq = CFXRunPool::get().claim(effective_depth);
  if (seq == nullptr)
    return; // pool exhausted or depth exceeded — already logged

  seq->detach_runtime_parent_();
  if (parent_seq != nullptr && parent_seq != seq) {
    parent_seq->attach_child_sequence_(seq);
  }

  // Configure the claimed sequence.
  seq->effect_    = this->effect_;
  seq->strip_tag_ = this->strip_tag_;
  seq->iterations_= this->iterations_;
  if (this->speed_.has_value())        seq->set_speed(this->speed_.value());
  if (this->intensity_.has_value())    seq->set_intensity(this->intensity_.value());
  if (this->palette_.has_value())      seq->set_palette(this->palette_.value());
  if (this->brightness_.has_value())   seq->set_brightness(this->brightness_.value());
  if (this->has_color_) {
    if (this->color_has_white_)
      seq->set_color_rgbw(this->color_r_, this->color_g_, this->color_b_,
                          this->color_w_);
    else
      seq->set_color_rgb(this->color_r_, this->color_g_, this->color_b_);
  }
  if (this->mirror_.has_value())       seq->set_mirror(this->mirror_.value());
  if (this->intro_.has_value())        seq->set_intro(this->intro_.value());
  if (this->outro_.has_value())        seq->set_outro(this->outro_.value());
  if (this->inout_duration_.has_value()) seq->set_inout_duration(this->inout_duration_.value());
  if (this->force_white_.has_value())  seq->set_force_white(this->force_white_.value());
  if (this->autotune_.has_value())     seq->set_autotune(this->autotune_.value());
  seq->set_ha_events(this->ha_events_);

  // Transfer triggers. These are YAML-codegen objects — they live for the
  // firmware lifetime and are safe to reference from the pooled sequence.
  // The pool's release() clears the vectors so they don't accumulate.
  for (auto *t : this->on_reach_triggers_)    seq->add_on_reach_trigger(t);
  for (auto *t : this->on_complete_triggers_) seq->add_on_complete_trigger(t);
  for (auto *t : this->on_stop_triggers_)     seq->add_on_stop_trigger(t);
  for (auto *t : this->on_start_triggers_)    seq->add_on_start_trigger(t);

  seq->add_light(this->light_);

  ESP_LOGD("cfx_run", "Spawning '%s' on '%s' (depth %u, iter %u)",
           this->effect_.c_str(),
           this->light_->get_name().c_str(),
           effective_depth,
           this->iterations_);

  if (CFXSequenceSelect::instance != nullptr &&
      should_defer_pool_sequence_start_(this->light_)) {
    uint32_t start_hash = esphome::fnv1_hash(seq->get_id() + "_deferred_start");
    ESP_LOGD("cfx_run", "Deferring start of '%s' on '%s' to next scheduler tick",
             this->effect_.c_str(), this->light_->get_name().c_str());
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance, start_hash, 0,
                                       [seq]() {
                                         if (seq != nullptr && !seq->is_running() &&
                                             !seq->is_starting()) {
                                           seq->start();
                                         }
                                       });
    return;
  }

  seq->start();
}

void CfxSetActionBase::do_play_() {
  if (this->light_ == nullptr)
    return;

  auto apply_ha_event_policy_to_inst =
      [this](chimera_fx::CFXAddressableLightEffect *inst) {
        const bool suppress = !this->ha_events_;
        inst->set_suppress_reach_event(suppress);
        inst->set_suppress_stop_event(suppress);
        inst->set_suppress_complete_event(suppress);
      };

  auto apply_overrides_to_inst =
      [this](chimera_fx::CFXAddressableLightEffect *inst) {
        if (this->speed_.has_value()) {
          inst->set_sequence_speed(this->speed_.value());
          inst->set_runner_owns_speed(true);
        }
        if (this->intensity_.has_value()) {
          inst->set_sequence_intensity(this->intensity_.value());
          inst->set_runner_owns_intensity(true);
        }
        if (this->palette_.has_value()) {
          inst->set_sequence_palette(this->palette_.value());
          inst->set_runner_owns_palette(true);
        }
        if (this->mirror_.has_value()) {
          inst->set_sequence_mirror(this->mirror_.value());
          inst->set_runner_owns_mirror(true);
        }
        if (this->autotune_.has_value())
          inst->set_sequence_autotune(this->autotune_.value());
        if (this->intro_.has_value())
          inst->set_intro_preset(this->intro_.value());
        if (this->outro_.has_value())
          inst->set_outro_preset(this->outro_.value());
        if (this->inout_duration_.has_value())
          inst->set_inout_duration_preset(this->inout_duration_.value());
        if (this->force_white_.has_value())
          inst->set_force_white_preset(this->force_white_.value());
      };

  auto resolve_target_inst =
      [this]() -> chimera_fx::CFXAddressableLightEffect * {
        auto find_inst =
            [this](const std::vector<chimera_fx::CFXAddressableLightEffect *> &effects,
                   bool require_active) -> chimera_fx::CFXAddressableLightEffect * {
              for (auto *inst : effects) {
                if (inst->get_light_state() != this->light_)
                  continue;
                if (!this->effect_.empty() && inst->get_name() != this->effect_)
                  continue;
                if (require_active && inst->get_act() == nullptr)
                  continue;
                return inst;
              }
              return nullptr;
            };

        // When an explicit effect is requested, seed only that effect instance.
        if (!this->effect_.empty()) {
          if (auto *inst = find_inst(chimera_fx::CFXAddressableLightEffect::all_effects, false))
            return inst;
          if (auto *inst = find_inst(chimera_fx::CFXAddressableLightEffect::all_segment_effects, false))
            return inst;
        }

        // Otherwise target only the currently active effect on this light.
        if (auto *inst = find_inst(chimera_fx::CFXAddressableLightEffect::all_effects, true))
          return inst;
        if (auto *inst = find_inst(chimera_fx::CFXAddressableLightEffect::all_segment_effects, true))
          return inst;

        return nullptr;
      };

  const bool needs_light_call =
      !this->effect_.empty() || this->brightness_.has_value() || this->has_color_;
  chimera_fx::CFXAddressableLightEffect *target_inst = resolve_target_inst();

  // Seed only the intended effect instance. Broadcasting pending overrides to
  // every effect object on a light makes future unrelated effects inherit
  // stale speed/intensity/palette/autotune settings.
  if (target_inst != nullptr) {
    apply_overrides_to_inst(target_inst);
    apply_ha_event_policy_to_inst(target_inst);
  }

  // Optionally turn the light on and/or update its live visible state.
  if (needs_light_call) {
    // Fix 3 — Re-entrancy: snapshot act_ BEFORE perform() so we can detect
    // whether ESPHome treated the call as a no-op (same effect already active).
    chimera_fx::CFXAddressableLightEffect::CFXActivation *act_before = nullptr;
    if (target_inst != nullptr)
      act_before = target_inst->get_act();

    auto call = this->light_->make_call();
    call.set_state(true);
    if (!this->effect_.empty()) {
      call.set_effect(this->effect_);
    }
    if (this->has_color_) {
      apply_cfx_user_color(this->light_, call, this->has_color_, this->color_r_,
                           this->color_g_, this->color_b_, this->color_w_,
                           this->color_has_white_);
    } else {
      // CFX-057: Set color mode to prevent virtual segments defaulting to White.
      auto valid_mode = this->light_->remote_values.get_color_mode();
      if (valid_mode == light::ColorMode::UNKNOWN) {
        if (this->light_->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE)) {
          valid_mode = light::ColorMode::RGB_WHITE;
        } else if (this->light_->get_traits().supports_color_mode(light::ColorMode::RGB)) {
          valid_mode = light::ColorMode::RGB;
        }
      }
      call.set_color_mode(valid_mode);
    }
    if (this->brightness_.has_value())
      call.set_brightness(this->brightness_.value());
    call.perform();

    if (target_inst == nullptr) {
      target_inst = resolve_target_inst();
    }

    // Fix 3 — If act_ is unchanged after perform(), ESPHome was a no-op
    // (light already ON with same effect). Force a clean restart so the intro
    // replays and mono_idle is cleared.
    if (!this->effect_.empty() &&
        target_inst != nullptr &&
        act_before  != nullptr &&
        target_inst->get_act() == act_before) {
      ESP_LOGD("chimera_fx",
               "cfx_set re-entry: forcing start() on '%s' (perform() was no-op).",
               this->effect_.c_str());
      target_inst->start();
      apply_overrides_to_inst(target_inst);
      apply_ha_event_policy_to_inst(target_inst);
    } else if (target_inst != nullptr) {
      apply_ha_event_policy_to_inst(target_inst);
    }
  }

  // Bug C fix — adopt_light(): register this light with the currently running
  // parent sequence so that sequence stop() cleans it up properly.
  //
  // When cfx_set activates a light that is NOT in the sequence's lights[] list
  // (e.g. Strip2 triggered via on_cfx_reach), that light is an orphan: the
  // sequence never calls turn_off on it, so its outro never plays and HA keeps
  // it marked as ON even after the sequence ends. The only way to fix it was to
  // manually toggle the switch.
  //
  // Fix: find the currently running non-pool sequence and call adopt_light() on
  // it. adopt_light() adds the light to lights_[], saves its state as OFF
  // (pre-sequence baseline), and registers a listener — making stop() treat it
  // identically to any other sequence light.
  //
  // We identify the parent sequence as: running, not pool-owned, and either
  // already owning this->light_ (cfx_set on a sequence light — no-op inside
  // adopt_light) or owning the light that fired the on_cfx_reach trigger. We
  // simply adopt into ALL currently running non-pool sequences; adopt_light()
  // is idempotent so double-adoption is harmless.
  for (auto *seq : CFXSequence::instances) {
    if (seq->is_running() && !CFXRunPool::get().is_pool_owned(seq)) {
      seq->adopt_light(this->light_);
    }
  }
}




void CFXSequenceSelect::setup() {
  CFXSequenceSelect::instance = this;
  // Phase E: Remind integrators to set api: batch_delay: 0ms for best event delivery.
  // This is a one-time boot advisory — it does not block operation.
  ESP_LOGW(TAG,
           "ChimeraFX: For sub-10ms cfx_start/cfx_complete event delivery to "
           "Home Assistant, set 'api: batch_delay: 0ms' in your ESPHome config.");
  this->add_on_state_callback([this](size_t index) {
    std::string value(this->current_option());
    if (CFXSequenceSelect::suppress_callback_)
      return;

    if (value == "None") {
      ESP_LOGD(TAG, "Active Sequence Select: 'None'");
      for (auto *seq : CFXSequence::instances) {
        if (seq->is_running() || seq->is_starting()) {
          seq->stop();
        } else {
          seq->force_reset();
        }
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

void CFXSequenceSelect::control(const std::string &value) {
  this->publish_state(value);
}

void CFXSequenceSelect::loop() {
  CFXEventManager::get().flush_pending();

  for (auto *seq : CFXSequence::instances) {
    if (seq->has_pending_teardown()) {
      esphome::App.feed_wdt();
      seq->process_pending_teardown();
      return;
    }
  }

  for (auto *seq : CFXSequence::instances)
    seq->check_duration();

  for (auto *seq : CFXSequence::instances) {
    if (seq->has_pending_duration_completion()) {
      esphome::App.feed_wdt();
      seq->complete_and_notify();
      return;
    }
  }

  for (auto *seq : CFXSequence::instances) {
    if (seq->has_pending_triggers()) {
      esphome::App.feed_wdt();
      seq->flush_pending_triggers();
      return;
    }
  }

  for (auto *eff : chimera_fx::CFXAddressableLightEffect::all_effects) {
    if (eff->has_pending_completion()) {
      esphome::App.feed_wdt();
      eff->execute_completion();
      return;
    }
  }
  for (auto *eff : chimera_fx::CFXAddressableLightEffect::all_segment_effects) {
    if (eff->has_pending_completion()) {
      esphome::App.feed_wdt();
      eff->execute_completion();
      return;
    }
  }
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
  this->detach_runtime_parent_();
  std::vector<CFXSequence *> orphaned_children = this->runtime_children_;
  for (auto *child : orphaned_children) {
    if (child != nullptr) {
      child->runtime_parent_ = nullptr;
    }
  }
  this->runtime_children_.clear();

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

  // CFX-052: SPI strips are UNSUPPORTED in multi-light sequences.
  // SPI driver (APA102/SK9822) causes crashes when used with more than
  // 2 lights or when >2 triggers fire per sequence (25%/50%/75%).
  // Root cause: SPI initialization/timing conflicts with ESPHome API.
  // Limitation: Use SPI strips alone or with max 2 RMT lights only.

  // CFX-044b: Conflict-aware handover — only stop sequences that share a
  // target light with this one. Sequences on independent strips can run in
  // parallel. Previously all sequences were stopped globally, destroying
  // unrelated strips when a new sequence started on any strip.
  for (auto *seq : CFXSequence::instances) {
    if (seq == this || !seq->is_running_)
      continue;
    // Check for light overlap
    bool conflict = false;
    for (auto *l : this->lights_) {
      if (seq->owns_light(l)) {
        conflict = true;
        break;
      }
    }
    if (conflict)
      seq->stop();
  }

  ESP_LOGD(TAG, "Starting CFX Sequence '%s' (%s)...", this->name_.c_str(),
           this->id_.c_str());

  this->report_event_begin();

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

  // Rebuild per-instance milestone event strings now that strip_tag_ is known.
  // Each CFXSequence instance maintains its own strings — no singleton shared state.
  if (!this->strip_tag_.empty()) {
    ESP_LOGD(TAG, "  Strip tag '%s': building per-instance milestone strings", this->strip_tag_.c_str());
    this->rebuild_milestone_strings_();
    this->reset_milestones_();
  }

  // Activate effect on all target lights
  // CFX-049: Staggered start to eliminate 68ms API lag.
  // Each strip perform() cost ~15ms. We spread them across loop cycles.
  uint32_t stagger_delay = 0;
  this->stagger_tasks_pending_ = 0;
  size_t total_lights = this->lights_.size();
  
  for (size_t i = 0; i < total_lights; i++) {
    light::LightState *l = this->lights_[i];
    auto task_name = this->id_ + "_start_" + std::to_string(i);
    uint32_t task_hash = esphome::fnv1_hash(task_name);
    this->stagger_tasks_pending_++;
             
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance,
                                      task_hash,
                                      stagger_delay, [this, l, task_name]() {
      // CFX-055: Do NOT decrement stagger_tasks_pending_ until AFTER
      // call.perform() completes. Decrementing early opens the listener
      // gate (is_stagger_complete() == true) during the effect stop→start
      // transition inside perform(), which can momentarily detect
      // is_on()==false and trigger a premature sequence stop().
      if (!this->is_running_) {
        this->stagger_tasks_pending_--;
        return;
      }

      // Feed WDT immediately on entering the staggered task
      esphome::App.feed_wdt();

      ESP_LOGD(TAG, "  Executing stagger task '%s' for light '%s'",
               task_name.c_str(), l->get_name().c_str());

      // CFX-053: Apply presets to the effect instance BEFORE call.perform()
      // Resolve target effect instance (handle segments resolving to parents)
      chimera_fx::CFXAddressableLightEffect *target_inst = nullptr;
      for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
        if (inst->get_light_state() == l && (this->effect_.empty() || inst->get_name() == this->effect_)) {
          target_inst = inst;
          break;
        }
      }
      if (target_inst == nullptr) {
        for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_segment_effects) {
          if (inst->get_light_state() == l && (this->effect_.empty() || inst->get_name() == this->effect_)) {
            target_inst = inst;
            break;
          }
        }
      }

      if (target_inst == nullptr) {
        auto *output = l->get_output();
        for (auto *s : cfx_light::CFXVirtualSegmentLight::all_segments) {
          if (s == output) {
            auto *parent = s->get_parent();
            auto *master_ls = parent->get_master_light_state();
            if (master_ls != nullptr) {
              light::LightEffect *master_active =
                  chimera_fx::LightStateProxy::get_active_effect(master_ls);
              for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
                if (inst == master_active) {
                  target_inst = inst;
                  break;
                }
              }
            }
            break;
          }
        }
      }

      if (target_inst != nullptr) {
        if (this->mirror_.has_value())
          target_inst->set_sequence_mirror(this->mirror_.value());
        if (this->autotune_.has_value())
          target_inst->set_sequence_autotune(this->autotune_.value());
        if (this->intro_.has_value())
          target_inst->set_intro_preset(this->intro_.value());
        if (this->outro_.has_value())
          target_inst->set_outro_preset(this->outro_.value());
        if (this->inout_duration_.has_value())
          target_inst->set_inout_duration_preset(this->inout_duration_.value());
        if (this->force_white_.has_value())
          target_inst->set_force_white_preset(this->force_white_.value());
      }

      auto call = l->make_call();
      call.set_state(true);
      if (!this->effect_.empty()) {
        call.set_effect(this->effect_);
      }
      if (this->has_color_) {
        apply_cfx_user_color(l, call, this->has_color_, this->color_r_,
                             this->color_g_, this->color_b_, this->color_w_,
                             this->color_has_white_);
      } else {
        auto valid_mode = l->remote_values.get_color_mode();
        if (valid_mode == light::ColorMode::UNKNOWN) {
          if (l->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE)) {
            valid_mode = light::ColorMode::RGB_WHITE;
          } else if (l->get_traits().supports_color_mode(light::ColorMode::RGB)) {
            valid_mode = light::ColorMode::RGB;
          }
        }
        call.set_color_mode(valid_mode);
      }
      if (this->effect_.empty()) {
        call.set_transition_length(0);
      }
      if (this->brightness_.has_value()) {
        call.set_brightness(this->brightness_.value());
      }
      call.perform();

      // CFX-055: NOW it's safe to decrement — perform() has completed,
      // the effect is started and bound, light is ON.
      this->stagger_tasks_pending_--;
    });
    stagger_delay += 50; // 50ms gap per strip start (CFX-052: longer for SPI compatibility)
  }

  // CFX-055: Deferred binding — verification & retry.
  // The primary binding now happens via CFX-036 auto-bind inside the effect's
  // start() method (synchronous, reliable). This deferred callback serves as
  // a VERIFICATION pass: if auto-bind succeeded, it's a no-op. If it missed
  // (e.g. effect not yet initialized), we retry the binding here.
  uint32_t bind_delay = stagger_delay + 100;
  uint32_t bind_hash = esphome::fnv1_hash(this->id_ + "_bind_all");
  esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance,
      bind_hash, bind_delay, [this]() {
    if (!this->is_running_) return;
    esphome::App.feed_wdt();
    this->try_bind_effects_();
  });

  this->last_triggered_percentage_ = -1.0f;
  this->last_triggered_pixel_ = -1;
  this->fired_reach_triggers_.clear();
  this->completion_reported_ = false;
  this->duration_completion_pending_ = false; 
  this->is_running_ = true;
  this->is_starting_ = false;

  // Update Select UI to reflect the active sequence.
  // Pool-owned sequences (cfx_run) are not in the select option list — skip.
  if (CFXSequenceSelect::instance != nullptr && !this->is_pool_owned_) {
    CFXSequenceSelect::instance->publish_state_silent(this->name_);
  }

  // Reset duration timer
  this->duration_start_ms_ = millis();
  this->duration_complete_fired_ = false;

  this->report_event_start();
}

// CFX-055: Unified bind helper — used by both the deferred bind_all and retry.
// Iterates the sequence's own lights, finds the active effect on each, and
// binds the sequence to it. Returns true if at least one binding was made.
bool CFXSequence::try_bind_effects_() {
  bool any_bound = false;

  // First, check if the auto-bind (CFX-036) already succeeded.
  for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
    if (inst->get_active_sequence() == this) {
      any_bound = true;
      break;
    }
  }
  if (!any_bound) {
    for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_segment_effects) {
      if (inst->get_active_sequence() == this) {
        any_bound = true;
        break;
      }
    }
  }

  if (any_bound) {
    ESP_LOGD(TAG, "Sequence '%s': binding verified (auto-bind succeeded)", this->name_.c_str());
    return true;
  }

  // Auto-bind missed — try manual binding via active effect on each light.
  ESP_LOGD(TAG, "Sequence '%s': auto-bind missed, attempting manual bind...", this->name_.c_str());
  for (auto *l : this->lights_) {
    light::LightEffect *active = chimera_fx::LightStateProxy::get_active_effect(l);
    if (active == nullptr) {
      ESP_LOGD(TAG, "  Light '%s': no active effect", l->get_name().c_str());
      continue;
    }

    // Search all_effects
    for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
      if (inst == active) {
        ESP_LOGD(TAG, "  Binding to Effect %p on '%s' (manual)", inst, l->get_name().c_str());
        this->apply_binding_to_effect_(inst);
        any_bound = true;
        break;
      }
    }
    if (any_bound) continue;

    // Search all_segment_effects
    for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_segment_effects) {
      if (inst == active) {
        ESP_LOGD(TAG, "  Binding to segment Effect %p on '%s' (manual)", inst, l->get_name().c_str());
        this->apply_binding_to_effect_(inst);
        any_bound = true;
        break;
      }
    }
    if (any_bound) continue;

    // Segment-to-parent resolution
    auto *output = l->get_output();
    for (auto *s : cfx_light::CFXVirtualSegmentLight::all_segments) {
      if (s == output) {
        auto *parent = s->get_parent();
        auto *master_ls = parent->get_master_light_state();
        if (master_ls != nullptr) {
          light::LightEffect *master_active =
              chimera_fx::LightStateProxy::get_active_effect(master_ls);
          for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
            if (inst == master_active) {
              ESP_LOGD(TAG, "  Binding to parent Effect %p (segment '%s', manual)",
                       inst, s->get_segment_id().c_str());
              this->apply_binding_to_effect_(inst);
              any_bound = true;
              break;
            }
          }
        }
        break;
      }
    }
  }

  if (!any_bound) {
    // CFX-055: Schedule ONE retry 150ms later instead of blind fallback.
    // The effect's start() may not have fired yet (deferred by ESPHome).
    uint32_t retry_hash = esphome::fnv1_hash(this->id_ + "_bind_retry");
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance,
        retry_hash, 150, [this]() {
      if (!this->is_running_) return;

      // Check once more
      bool found = false;
      for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
        if (inst->get_active_sequence() == this) { found = true; break; }
      }
      if (!found) {
        for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_segment_effects) {
          if (inst->get_active_sequence() == this) { found = true; break; }
        }
      }
      if (found) {
        ESP_LOGD(TAG, "Sequence '%s': binding verified on retry", this->name_.c_str());
        return;
      }

      // Last resort: find any effect on the sequence's own lights
      for (auto *l : this->lights_) {
        for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
          if (inst->get_light_state() == l && inst->get_act() != nullptr) {
            ESP_LOGW(TAG, "Sequence '%s': fallback binding to '%s' on '%s'",
                     this->name_.c_str(), inst->get_name().c_str(), l->get_name().c_str());
            this->apply_binding_to_effect_(inst);
            return;
          }
        }
      }
      ESP_LOGW(TAG, "Sequence '%s': binding FAILED after retry — no active effects on target lights",
               this->name_.c_str());
    });
  }

  return any_bound;
}

void CFXSequence::apply_binding_to_effect_(chimera_fx::CFXAddressableLightEffect *inst) {
  inst->set_active_sequence(this, this->speed_, this->intensity_,
                            this->palette_, this->mirror_, this->autotune_,
                            this->iterations_);
  inst->set_suppress_reach_event(!this->ha_events_);
  inst->set_suppress_stop_event(!this->ha_events_);
  inst->set_suppress_complete_event(!this->ha_events_);

  // strip_tag is intentionally NOT stamped from the sequence here.
  // Every effect's act_->strip_tag is already set correctly in start() via
  // get_object_id_to() for its own light. Overwriting it with the sequence's
  // primary tag ('led_strip1') would cause ALL effects in the sequence —
  // including Strip3, Strip2, and any adopted light — to fire cfx_reach /
  // cfx_stop / cfx_complete events tagged as 'led_strip1', making them
  // invisible or misrouted in HA. Each light must report under its own tag.
  // The sequence fires its own cfx_stop/cfx_complete using strip_tag_ directly
  // (see report_event_stop / report_event_complete) — it does not need to
  // propagate its tag into individual effect instances.

  if (this->intro_.has_value())
    inst->set_intro_preset(this->intro_.value());
  if (this->outro_.has_value())
    inst->set_outro_preset(this->outro_.value());
  if (this->inout_duration_.has_value())
    inst->set_inout_duration_preset(this->inout_duration_.value());
  if (this->force_white_.has_value())
    inst->set_force_white_preset(this->force_white_.value());
}

void CFXSequence::begin_teardown_(TeardownMode mode) {
  this->teardown_mode_ = mode;
  this->teardown_light_index_ = 0;
}

void CFXSequence::finalize_teardown_() {
  TeardownMode completed_mode = this->teardown_mode_;
  this->teardown_mode_ = TeardownMode::NONE;
  this->teardown_light_index_ = 0;
  this->is_stopping_ = false;

  // cfx_set adoptions are runtime-only: once the sequence has finished
  // cleaning up, drop back to the original configured light set so a later
  // restart does not replay the sequence on previously adopted lights.
  if (!this->is_pool_owned_) {
    if (this->lights_.size() > this->configured_light_count_) {
      this->lights_.resize(this->configured_light_count_);
    }

    // Keep listener allocations, but disarm runtime-only adopted lights until
    // they are explicitly adopted again on a future run.
    for (auto &m : this->monitored_lights_) {
      if (std::find(this->lights_.begin(), this->lights_.end(), m.light) ==
          this->lights_.end()) {
        m.listener->nullify();
      }
    }
  }

  if (!this->is_pool_owned_ &&
      CFXSequenceSelect::instance != nullptr &&
      CFXSequenceSelect::instance->has_state()) {
    const std::string &current = CFXSequenceSelect::instance->current_option();
    if (!current.empty() && this->name_ == current) {
      CFXSequenceSelect::instance->publish_state_silent("None");
    }
  }

  if (completed_mode == TeardownMode::COMPLETE_RESTORE) {
    // LAST: fire on_complete — any chained cfx_set now wins with no
    // subsequent stop() able to undo the next step.
    this->report_event_complete();
  }

  if (this->is_pool_owned_ &&
      (completed_mode == TeardownMode::STOP_RESTORE ||
       completed_mode == TeardownMode::STOP_FORCE_OFF ||
       completed_mode == TeardownMode::COMPLETE_RESTORE)) {
    CFXRunPool::get().release(this);
  }
}

void CFXSequence::attach_child_sequence_(CFXSequence *child) {
  if (child == nullptr || child == this)
    return;

  child->detach_runtime_parent_();
  child->runtime_parent_ = this;

  if (std::find(this->runtime_children_.begin(), this->runtime_children_.end(),
                child) == this->runtime_children_.end()) {
    this->runtime_children_.push_back(child);
  }
}

void CFXSequence::detach_child_sequence_(CFXSequence *child) {
  if (child == nullptr)
    return;

  auto it = std::remove(this->runtime_children_.begin(),
                        this->runtime_children_.end(), child);
  if (it != this->runtime_children_.end()) {
    this->runtime_children_.erase(it, this->runtime_children_.end());
  }
}

void CFXSequence::detach_runtime_parent_() {
  if (this->runtime_parent_ != nullptr) {
    this->runtime_parent_->detach_child_sequence_(this);
    this->runtime_parent_ = nullptr;
  }
}

void CFXSequence::stop_tree_(std::set<CFXSequence *> &visited) {
  if (!visited.insert(this).second)
    return;

  std::vector<CFXSequence *> children = this->runtime_children_;
  for (auto *child : children) {
    if (child != nullptr) {
      child->stop_tree_(visited);
    }
  }

  if (this->is_running_) {
    this->stop();
  }
}

void CFXSequence::stop_tree() {
  std::set<CFXSequence *> visited;
  this->stop_tree_(visited);
}

void CFXSequence::process_pending_teardown() {
  if (this->teardown_mode_ == TeardownMode::NONE)
    return;

  const bool restore_saved_state =
      (this->teardown_mode_ != TeardownMode::FORCE_OFF) &&
      (this->teardown_mode_ != TeardownMode::STOP_FORCE_OFF) &&
      this->restore_state_;

  while (this->teardown_light_index_ < this->lights_.size()) {
    auto *l = this->lights_[this->teardown_light_index_];

    if (restore_saved_state) {
      if (this->teardown_light_index_ >= this->saved_states_.size()) {
        this->teardown_light_index_++;
        continue;
      }

      auto saved = this->saved_states_[this->teardown_light_index_];
      auto call = l->make_call();
      call.set_transition_length(0);

      bool turning_on = saved.values.is_on();
      call.set_state(turning_on);

      if (saved.color_mode != light::ColorMode::UNKNOWN)
        call.set_color_mode(saved.color_mode);
      call.set_brightness(saved.values.get_brightness());
      call.set_rgb(saved.values.get_red(), saved.values.get_green(),
                   saved.values.get_blue());

      if (l->get_traits().supports_color_mode(saved.color_mode) &&
          (saved.color_mode == light::ColorMode::RGB_WHITE ||
           saved.color_mode == light::ColorMode::RGB_COLD_WARM_WHITE ||
           saved.color_mode == light::ColorMode::COLD_WARM_WHITE ||
           saved.color_mode == light::ColorMode::WHITE)) {
        call.set_white(saved.values.get_white());
      }

      if (turning_on)
        call.set_effect(saved.effect);

      call.perform();
      this->teardown_light_index_++;
      return;
    }

    auto off_call = l->make_call();
    off_call.set_state(false);
    off_call.set_transition_length(0);
    off_call.perform();
    this->teardown_light_index_++;
    return;
  }

  this->finalize_teardown_();
}

void CFXSequence::stop() {
  if (this->is_stopping_ || !this->is_running_)
    return;
  this->is_stopping_ = true;
  this->is_running_ = false;
  this->duration_complete_fired_ = false;

  ESP_LOGD(TAG, "Stopping CFX Sequence '%s'...", this->name_.c_str());

  this->report_event_stop();
  this->clear_active_binding();
  this->begin_teardown_(TeardownMode::STOP_RESTORE);

  if (this->lights_.empty())
    this->finalize_teardown_();
}

// CFX-044: Correctly-ordered completion path invoked by execute_completion().
// stop() has this ordering problem:
//   report_event_complete() → on_complete fires cfx_set → step 2 starts
//   stop() → clear_active_binding() + call.perform() restore → KILLS step 2
// This method fixes it:
//   1. Stop teardown (binding clear + restore) FIRST
//   2. report_event_complete() LAST — on_complete automations start on clean slate
void CFXSequence::complete_and_notify() {
  if (this->is_stopping_ || !this->is_running_)
    return;
  this->is_stopping_ = true;
  this->is_running_  = false;
  this->duration_complete_fired_ = false;
  this->duration_completion_pending_ = false; // CFX-044c: Reset after handling

  ESP_LOGD(TAG, "Sequence '%s': completing (effect done)...", this->name_.c_str());

  // CFX-053: Drain any last-frame on_cfx_reach triggers BEFORE teardown.
  // Without this, triggers queued in the same frame as effect_complete_ are
  // lost because clear_active_binding() nulls act_->active_sequence before
  // flush_pending_triggers() has a chance to fire them.
  this->flush_pending_triggers();

  this->report_event_stop();
  this->clear_active_binding();
  this->begin_teardown_(TeardownMode::COMPLETE_RESTORE);

  if (this->lights_.empty())
    this->finalize_teardown_();
}

void CFXSequence::force_reset() {
  // Hard reset: turn off all lights managed by this sequence.
  // Called by "None" handler to ensure lights are off even when
  // the sequence has already completed and on_complete overrode state.
  // Guard: skip sequences that were never started — firing light calls on
  // non-running sequences produces ESPHome warnings on unrelated lights.
  if (!this->is_running_ && !this->completion_reported_ &&
      !this->is_starting_ &&
      this->teardown_mode_ == TeardownMode::NONE)
    return;

  if (this->is_stopping_) {
    if (this->teardown_mode_ == TeardownMode::STOP_RESTORE) {
      this->begin_teardown_(TeardownMode::STOP_FORCE_OFF);
    } else if (this->teardown_mode_ == TeardownMode::NONE) {
      this->begin_teardown_(TeardownMode::FORCE_OFF);
    }

    if (this->lights_.empty())
      this->finalize_teardown_();
    return;
  }

  this->is_starting_ = false;
  this->is_stopping_ = true;
  this->begin_teardown_(TeardownMode::FORCE_OFF);

  if (this->lights_.empty())
    this->finalize_teardown_();
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
    clear_call.perform();

    auto off_call = l->make_call();
    off_call.set_state(false);
    off_call.set_transition_length(0);
    off_call.perform();
  }

  if (this->is_pool_owned_) {
    CFXRunPool::get().release(this);
    return;
  }

  this->is_stopping_ = false;
}

void CFXSequence::stop_all() {
  std::set<light::LightState *> unique_lights;
  std::vector<CFXSequence *> pooled_to_release;
  for (auto *seq : CFXSequence::instances) {
    for (auto *l : seq->lights_) {
      unique_lights.insert(l);
    }
    if (seq->is_running_) {
      seq->is_running_ = false;
      seq->clear_active_binding();
      seq->duration_complete_fired_ = false;
    }
    if (seq->is_pool_owned_) {
      pooled_to_release.push_back(seq);
    }
  }

  // Two calls required: see stop() else branch for explanation.
  for (auto *l : unique_lights) {
    auto clear_call = l->make_call();
    clear_call.set_effect("None");
    clear_call.perform();

    auto off_call = l->make_call();
    off_call.set_state(false);
    off_call.set_transition_length(0);
    off_call.perform();
  }

  for (auto *seq : pooled_to_release) {
    if (seq != nullptr && seq->is_pool_owned_) {
      CFXRunPool::get().release(seq);
    }
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
  //
  // Both all_effects (master-light effects) AND all_segment_effects (virtual
  // segment effects) must be cleared. Previously only all_effects was
  // iterated, leaving segment effects (Strip2, Strip3 etc.) with a stale
  // non-null active_sequence. The cfx_stop gate in effect stop() requires
  // active_sequence == nullptr — so those segments silently dropped their
  // cfx_stop and cfx_complete events on sequence stop.
  for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
    const bool is_bound_to_sequence = inst->get_active_sequence() == this;
    const bool is_active_owned_light =
        inst->get_act() != nullptr && this->owns_light(inst->get_light_state());
    if (is_bound_to_sequence || is_active_owned_light) {
      inst->set_is_sequence_outro(this->completion_reported_);
      inst->set_suppress_positional_events(true);
      inst->set_suppress_stop_event(true);
      inst->set_suppress_complete_event(true);
      inst->set_active_sequence(nullptr, {}, {}, {}, {}, {}, 0);
    }
  }
  for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_segment_effects) {
    const bool is_bound_to_sequence = inst->get_active_sequence() == this;
    const bool is_active_owned_light =
        inst->get_act() != nullptr && this->owns_light(inst->get_light_state());
    if (is_bound_to_sequence || is_active_owned_light) {
      inst->set_is_sequence_outro(this->completion_reported_);
      inst->set_suppress_positional_events(true);
      inst->set_suppress_stop_event(true);
      inst->set_suppress_complete_event(true);
      inst->set_active_sequence(nullptr, {}, {}, {}, {}, {}, 0);
    }
  }
}

void CFXSequence::adopt_light(light::LightState *state) {
  if (state == nullptr || !this->is_running_)
    return;

  // Idempotent: if the light is already owned, do nothing.
  if (this->owns_light(state))
    return;

  // 1. Add to lights_ so stop() iterates it.
  this->lights_.push_back(state);

  // 2. Save current state so the restore path in stop() has a valid baseline.
  //    The light was just turned ON by cfx_set, so we save it as OFF — that is
  //    the pre-sequence state we want to restore to when the sequence ends.
  SavedState s;
  s.values = state->remote_values;
  s.values.set_state(false);            // restore to OFF when sequence stops
  light::ColorMode cm = state->remote_values.get_color_mode();
  if (cm == light::ColorMode::UNKNOWN) {
    if (state->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE))
      cm = light::ColorMode::RGB_WHITE;
    else if (state->get_traits().supports_color_mode(light::ColorMode::RGB))
      cm = light::ColorMode::RGB;
    else if (state->get_traits().supports_color_mode(light::ColorMode::WHITE))
      cm = light::ColorMode::WHITE;
    else
      cm = light::ColorMode::ON_OFF;
  }
  s.color_mode = cm;
  s.effect = "None";
  this->saved_states_.push_back(s);

  // 3. Register a remote-values listener so the sequence detects when this
  //    light is turned off manually (same pattern as start()-time registration).
  auto it = std::find_if(
      this->monitored_lights_.begin(), this->monitored_lights_.end(),
      [state](const MonitoredLight &m) { return m.light == state; });

  if (it != this->monitored_lights_.end()) {
    it->listener->reinstate(this);
  } else {
    auto *listener = new CFXSequenceListener(this, state);
    state->add_remote_values_listener(listener);
    this->monitored_lights_.push_back({state, listener});
  }

  ESP_LOGD("cfx_sequence",
           "Sequence '%s': adopted cfx_set light '%s' — will stop with sequence.",
           this->name_.c_str(), state->get_name().c_str());
}

void CFXSequence::flush_pending_triggers() {
  if (this->pending_reach_triggers_.empty())
    return;

  // Make a local copy then clear the queue BEFORE firing.
  std::vector<PendingTrigger> to_fire = this->pending_reach_triggers_;
  this->pending_reach_triggers_.clear();

  // Adaptive batch size mirrors the event manager's queue-depth logic so
  // trigger flushing and event delivery share the loop budget proportionally
  // rather than competing blindly.
  size_t queue_depth = to_fire.size();
  size_t max_fire;
  if (queue_depth >= 8)
    max_fire = 3;
  else if (queue_depth >= 4)
    max_fire = 2;
  else
    max_fire = 1;

  for (size_t idx = 0; idx < max_fire && idx < to_fire.size(); idx++) {
    const auto &t = to_fire[idx];
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance,
                                      (uint32_t)t.trigger, 0, [s = this, t]() {
      SequenceTriggerContextGuard guard(s);
      esphome::yield();
      esphome::App.feed_wdt();
      t.trigger->trigger(t.value);
    });
    esphome::yield();
    esphome::App.feed_wdt();
  }

  // Re-queue remaining triggers for next tick
  for (size_t idx = max_fire; idx < to_fire.size(); idx++) {
    this->pending_reach_triggers_.push_back(to_fire[idx]);
  }
}

void CFXSequence::check_duration() {
  if (!this->is_running_)
    return;
  if (this->duration_ms_ == 0)
    return;
  if (this->duration_complete_fired_ || this->duration_completion_pending_)
    return;
  if ((millis() - this->duration_start_ms_) >= this->duration_ms_) {
    ESP_LOGD(TAG, "Sequence '%s': duration %u ms elapsed — marking for worker completion",
             this->name_.c_str(), this->duration_ms_);
    this->duration_complete_fired_ = true;
    this->duration_completion_pending_ = true; // CFX-044c: Defer to worker
  }
}

void CFXSequence::report_event_start() {
  ESP_LOGV(TAG, "Sequence '%s': on_start triggers firing", this->id_.c_str());
  for (auto *t : this->on_start_triggers_) {
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance, 
                                      (uint32_t)t, 0, [s = this, t]() {
      SequenceTriggerContextGuard guard(s);
      t->trigger();
    });
  }
  // NOTE: cfx_start HA event is fired by CFXAddressableLightEffect::start()
  // unconditionally for all effects and all paths. Do NOT fire it here again.
}

void CFXSequence::report_event_begin() {
  ESP_LOGV(TAG, "Sequence '%s': on_begin triggers firing", this->id_.c_str());
  // on_cfx_begin YAML trigger always fires (on-device automation).
  for (auto *t : this->on_begin_triggers_) {
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance, 
                                      (uint32_t)t, 0, [s = this, t]() {
      SequenceTriggerContextGuard guard(s);
      t->trigger();
    });
  }
  // CFX-029: HA cfx_begin event only fires when the sequence has a real
  // intro configured (intro_ != 0). Without an intro, cfx_begin and
  // cfx_start fire at the same millisecond and are redundant.
  if (!this->strip_tag_.empty()
      && this->ha_events_
      && this->intro_.has_value() && this->intro_.value() != 0) {
    std::string evt = std::string("cfx_begin:") + this->strip_tag_;
    CFXEventManager::get().fire_event(evt.c_str());
  }
}

void CFXSequence::report_event_stop() {
  ESP_LOGV(TAG, "Sequence '%s': on_stop triggers firing", this->id_.c_str());
  for (auto *t : this->on_stop_triggers_) {
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance, 
                                      (uint32_t)t, 0, [s = this, t]() {
      SequenceTriggerContextGuard guard(s);
      t->trigger();
    });
  }
  // Fire cfx_stop HA event — outro is beginning (or sequence is stopping).
  if (this->ha_events_ && !this->strip_tag_.empty()) {
    std::string evt = std::string("cfx_stop:") + this->strip_tag_;
    CFXEventManager::get().fire_event(evt.c_str());
  }
}

void CFXSequence::report_event_complete() {
  ESP_LOGV(TAG, "Sequence '%s': on_complete triggers firing", this->id_.c_str());
  this->completion_reported_ = true;
  for (auto *t : this->on_complete_triggers_) {
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance, 
                                      (uint32_t)t, 0, [s = this, t]() {
      SequenceTriggerContextGuard guard(s);
      // Small verification: only fire if the sequence didn't restart mid-deferral
      if (s->completion_reported_)
        t->trigger();
    });
  }
  // Fire tagged cfx_complete using this sequence's own strip_tag_.
  // No global singleton state — each sequence instance fires for its own strip.
  if (this->ha_events_ && !this->strip_tag_.empty()) {
    std::string evt = std::string("cfx_complete:") + this->strip_tag_;
    CFXEventManager::get().fire_event(evt.c_str());
  }
}

void CFXSequence::check_positional_triggers(int32_t current_pixel,
                                            int32_t total_pixels,
                                            bool is_return_phase) {
  if (total_pixels <= 0)
    return;

  // Debounce per pixel
  if (current_pixel == this->last_triggered_pixel_) {
    return;
  }

  // Adjust percentage calculation to be boundary-inclusive (0.0 to 1.0)
  // total_pixels - 1 ensures that the last pixel maps to 100%
  float current_percentage = (total_pixels > 1) ? (float)current_pixel / (float)(total_pixels - 1) : 1.0f;

  // During the return/erase phase (e.g. Wipe erasing back), suppress
  // on_cfx_reach positional triggers entirely. The leading pixel sweeps
  // 0→100% again during the erase, which would double-fire every trigger.
  // Milestones (cfx_reach HA events) are still allowed — the effect layer
  // controls those separately via check_milestones_() and the milestone
  // reset at the phase boundary.
  if (!is_return_phase) {
    // Reset stale tracking state at the start of a new forward pass.
    if (this->last_triggered_percentage_ > 0.8f && current_percentage < 0.2f) {
      this->last_triggered_percentage_ = -1.0f;
      this->fired_reach_triggers_.clear();
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
        if (std::find(this->fired_reach_triggers_.begin(),
                      this->fired_reach_triggers_.end(),
                      t) != this->fired_reach_triggers_.end()) {
          continue;
        }
        ESP_LOGV(TAG, "Sequence '%s': on_reach %.2f%% queued",
                 this->id_.c_str(), target * 100.0f);
        this->pending_reach_triggers_.push_back({t, current_percentage});
        this->fired_reach_triggers_.push_back(t);
      }
    }

    this->last_triggered_percentage_ = current_percentage;
  }

  this->last_triggered_pixel_ = current_pixel;

  // Milestone HA events (cfx_reach) are no longer fired here.
  // Each CFXAddressableLightEffect fires its own check_milestones_() using
  // act_->strip_tag — the per-light tag set in start() from get_object_id_to().
  // This function is now responsible only for on_cfx_reach YAML automation
  // triggers (pending_reach_triggers_), which require sequence-level state
  // (last_triggered_percentage_, on_reach_triggers_) and are correctly owned
  // by the sequence object.
}

void CFXSequence::CFXSequenceListener::on_light_remote_values_update() {
  if (this->parent_ == nullptr || !this->parent_->is_running() ||
      !this->parent_->is_stagger_complete())
    return;

  // Only stop the sequence when ALL of its lights are off, not just this one.
  // Previous behaviour (any single light off -> stop()) caused a cascade: in a
  // 2-light sequence, turning off Strip1 immediately restored Strip3 to its
  // pre-sequence state, even though the user only intended to control Strip1.
  // Each sequence light must be independently controllable; the sequence stops
  // only when every light it owns has turned off.
  if (this->light_->remote_values.is_on())
    return; // this light is still on — nothing to do

  for (auto *l : this->parent_->lights_) {
    if (l->remote_values.is_on())
      return; // at least one other sequence light still on — keep running
  }

  ESP_LOGV("cfx_sequence",
           "Sequence '%s' stopping: all lights are now off",
           this->parent_->get_name().c_str());
  this->parent_->stop();
}

// ----------------------------------------------------
// Runtime Configurable Entities (Number Inputs)
// ----------------------------------------------------


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
