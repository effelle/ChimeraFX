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



// ── CFXRunPool ────────────────────────────────────────────────────────────────

CFXRunPool &CFXRunPool::get() {
  static CFXRunPool inst;
  return inst;
}

void CFXRunPool::ensure_initialized_() {
  if (this->initialized_) return;
  this->initialized_ = true;
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
      seq->is_pool_owned_ = true;
      seq->completion_reported_ = false; // reset here, not in release() — avoids race with deferred callbacks
      // Register in the global instances list for the duration of this run.
      CFXSequence::instances.push_back(seq);
      ESP_LOGD("cfx_run", "Pool slot %u claimed (depth %u)", i, depth);
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
      seq->is_pool_owned_ = false;
      // Remove from global instances so it doesn't appear in stop_all() etc.
      auto &v = CFXSequence::instances;
      v.erase(std::remove(v.begin(), v.end(), seq), v.end());
      // Reset sequence state for next claim.
      seq->lights_.clear();
      seq->on_reach_triggers_.clear();
      seq->on_complete_triggers_.clear();
      seq->on_stop_triggers_.clear();
      seq->on_start_triggers_.clear();
      seq->saved_states_.clear();
      seq->pending_reach_triggers_.clear();
      seq->strip_tag_ = "";
      seq->effect_    = "";
      seq->speed_     = {};
      seq->intensity_ = {};
      seq->palette_   = {};
      seq->brightness_= {};
      seq->mirror_    = {};
      seq->intro_     = {};
      seq->outro_     = {};
      seq->inout_duration_ = {};
      seq->iterations_ = 0;
      seq->duration_ms_ = 0;
      seq->is_running_  = false;
      seq->is_starting_ = false;
      seq->is_stopping_ = false;
      seq->completion_reported_ = true; // keep true — deferred callbacks check this guard
      seq->last_fired_milestone_ = 0;
      seq->last_triggered_percentage_ = -1.0f;
      seq->last_triggered_pixel_ = -1;
      ESP_LOGD("cfx_run", "Pool slot %u released", i);
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

  // Claim a pool slot.
  CFXSequence *seq = CFXRunPool::get().claim(this->nesting_depth_);
  if (seq == nullptr)
    return; // pool exhausted or depth exceeded — already logged

  // Configure the claimed sequence.
  seq->effect_    = this->effect_;
  seq->strip_tag_ = this->strip_tag_;
  seq->iterations_= this->iterations_;
  if (this->speed_.has_value())        seq->set_speed(this->speed_.value());
  if (this->intensity_.has_value())    seq->set_intensity(this->intensity_.value());
  if (this->palette_.has_value())      seq->set_palette(this->palette_.value());
  if (this->brightness_.has_value())   seq->set_brightness(this->brightness_.value());
  if (this->mirror_.has_value())       seq->set_mirror(this->mirror_.value());
  if (this->intro_.has_value())        seq->set_intro(this->intro_.value());
  if (this->outro_.has_value())        seq->set_outro(this->outro_.value());
  if (this->inout_duration_.has_value()) seq->set_inout_duration(this->inout_duration_.value());

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
           this->nesting_depth_,
           this->iterations_);

  seq->start();
}

void CfxSetActionBase::do_play_() {
  if (this->light_ == nullptr)
    return;

  // Apply params to all CFX effect instances registered for this light.
  // Also set runner ownership flags so CFXControl push callbacks don't
  // stomp the cfx_set values via UI slider on_state callbacks.
  for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
    if (inst->get_light_state() == this->light_) {
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
      if (this->mirror_.has_value())
        inst->set_mirror_preset(this->mirror_.value());
      if (this->intro_.has_value())
        inst->set_intro_preset(this->intro_.value());
      if (this->outro_.has_value())
        inst->set_outro_preset(this->outro_.value());
      if (this->inout_duration_.has_value())
        inst->set_inout_duration_preset(this->inout_duration_.value());
    }
  }

  // Optionally turn the light on with the specified effect.
  if (!this->effect_.empty()) {
    auto call = this->light_->make_call();
    call.set_state(true);
    call.set_effect(this->effect_);
    if (this->brightness_.has_value())
      call.set_brightness(this->brightness_.value());
    call.perform();
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
        seq->stop();
        seq->force_reset();
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
      this->stagger_tasks_pending_--;
      if (!this->is_running_) return;

      // Feed WDT immediately on entering the staggered task
      esphome::App.feed_wdt();

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
          target_inst->set_mirror_preset(this->mirror_.value());
        if (this->intro_.has_value())
          target_inst->set_intro_preset(this->intro_.value());
        if (this->outro_.has_value())
          target_inst->set_outro_preset(this->outro_.value());
        if (this->inout_duration_.has_value())
          target_inst->set_inout_duration_preset(this->inout_duration_.value());
      }

      auto call = l->make_call();
      call.set_state(true);
      if (!this->effect_.empty()) {
        call.set_effect(this->effect_);
      }
      auto valid_mode = l->remote_values.get_color_mode();
      if (valid_mode == light::ColorMode::UNKNOWN) {
        if (l->get_traits().supports_color_mode(light::ColorMode::RGB_WHITE)) {
          valid_mode = light::ColorMode::RGB_WHITE;
        } else if (l->get_traits().supports_color_mode(light::ColorMode::RGB)) {
          valid_mode = light::ColorMode::RGB;
        }
      }
      call.set_color_mode(valid_mode);
      if (this->effect_.empty()) {
        call.set_transition_length(0);
      }
      if (this->brightness_.has_value()) {
        call.set_brightness(this->brightness_.value());
      }
      call.perform();
    });
    stagger_delay += 50; // 50ms gap per strip start (CFX-052: longer for SPI compatibility)
  }

  // CFX-053: Deferred binding — runs AFTER all staggered starts have fired.
  // This ensures every effect's act_ is initialized before we try to bind.
  // Also handles segment-to-parent resolution for virtual segment lights.
  uint32_t bind_delay = stagger_delay + 100;
  uint32_t bind_hash = esphome::fnv1_hash(this->id_ + "_bind_all");
  esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance,
      bind_hash, bind_delay, [this]() {
    esphome::App.feed_wdt();
    for (auto *l : this->lights_) {
      light::LightEffect *active = chimera_fx::LightStateProxy::get_active_effect(l);
      bool bound = false;
      if (active != nullptr) {
        for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
          if (inst == active) {
            ESP_LOGD(TAG, "  Binding Sequence to Effect %p (active match)", inst);
            inst->set_active_sequence(this, this->speed_, this->intensity_,
                                      this->palette_, this->iterations_);
            inst->set_strip_tag(this->strip_tag_);
            if (this->mirror_.has_value())
              inst->set_mirror_preset(this->mirror_.value());
            if (this->intro_.has_value())
              inst->set_intro_preset(this->intro_.value());
            if (this->outro_.has_value())
              inst->set_outro_preset(this->outro_.value());
            if (this->inout_duration_.has_value())
              inst->set_inout_duration_preset(this->inout_duration_.value());
            bound = true;
            break;
          }
        }
        if (!bound) {
          // CFX-054: Virtual segments effects are tracked separately, check them too!
          for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_segment_effects) {
            if (inst == active) {
              ESP_LOGD(TAG, "  Binding Sequence to Effect %p (segment active match)", inst);
              inst->set_active_sequence(this, this->speed_, this->intensity_,
                                        this->palette_, this->iterations_);
              inst->set_strip_tag(this->strip_tag_);
              if (this->mirror_.has_value())
                inst->set_mirror_preset(this->mirror_.value());
              if (this->intro_.has_value())
                inst->set_intro_preset(this->intro_.value());
              if (this->outro_.has_value())
                inst->set_outro_preset(this->outro_.value());
              if (this->inout_duration_.has_value())
                inst->set_inout_duration_preset(this->inout_duration_.value());
              bound = true;
              break;
            }
          }
        }
      }
      // CFX-053: Segment-to-parent resolution.
      // Virtual segment lights are not in all_effects. Resolve the parent
      // physical light's effect instance via the segment's parent pointer.
      if (!bound) {
        auto *output = l->get_output();
        cfx_light::CFXVirtualSegmentLight *vseg = nullptr;
        for (auto *s : cfx_light::CFXVirtualSegmentLight::all_segments) {
          if (s == output) {
            vseg = s;
            break;
          }
        }
        if (vseg != nullptr) {
          auto *parent = vseg->get_parent();
          auto *master_ls = parent->get_master_light_state();
          if (master_ls != nullptr) {
            light::LightEffect *master_active =
                chimera_fx::LightStateProxy::get_active_effect(master_ls);
            if (master_active != nullptr) {
              for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
                if (inst == master_active) {
                  ESP_LOGD(TAG, "  Binding Sequence to parent Effect %p (segment '%s')",
                           inst, vseg->get_segment_id().c_str());
                  inst->set_active_sequence(this, this->speed_, this->intensity_,
                                            this->palette_, this->iterations_);
                  inst->set_strip_tag(this->strip_tag_);
                  if (this->mirror_.has_value())
                    inst->set_mirror_preset(this->mirror_.value());
                  if (this->intro_.has_value())
                    inst->set_intro_preset(this->intro_.value());
                  if (this->outro_.has_value())
                    inst->set_outro_preset(this->outro_.value());
                  if (this->inout_duration_.has_value())
                    inst->set_inout_duration_preset(this->inout_duration_.value());
                  bound = true;
                  break;
                }
              }
            }
          }
        }
      }
    }
    this->handle_fallback_binding_();
  });

  this->last_triggered_percentage_ = -1.0f;
  this->last_triggered_pixel_ = -1;
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

void CFXSequence::handle_fallback_binding_() {
  // Check if ANY light is currently bound to this sequence
  bool any_bound = false;
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

  if (any_bound || chimera_fx::CFXAddressableLightEffect::all_effects.empty())
    return;

  // Fallback path: All staggered starts failed to bind local instances.
  // Bind to the first registered effect (Master FX) as a safety net.
  auto *master_fx = chimera_fx::CFXAddressableLightEffect::all_effects[0];
  if (master_fx != nullptr) {
    ESP_LOGW(TAG, "Sequence '%s': no active local CFX effects found. Falling back to %p.",
             this->name_.c_str(), master_fx);
    master_fx->set_active_sequence(this, this->speed_, this->intensity_,
                                   this->palette_, this->iterations_);
    master_fx->set_strip_tag(this->strip_tag_);
    if (this->mirror_.has_value())
      master_fx->set_mirror_preset(this->mirror_.value());
    if (this->intro_.has_value())
      master_fx->set_intro_preset(this->intro_.value());
    if (this->outro_.has_value())
      master_fx->set_outro_preset(this->outro_.value());
    if (this->inout_duration_.has_value())
      master_fx->set_inout_duration_preset(this->inout_duration_.value());
  }
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

  // Restore States: Only if explicitly requested
  if (this->restore_state_) {
    size_t light_idx = 0;
    for (auto *l : this->lights_) {
      if (light_idx < this->saved_states_.size()) {
        auto saved = this->saved_states_[light_idx];
        auto call = l->make_call();

        // Restore transition first
        call.set_transition_length(0);

        // Restore state
        bool turning_on = saved.values.is_on();
        call.set_state(turning_on);

        // Restore mode and brightness.
        // Guard: never pass UNKNOWN to set_color_mode — ESPHome logs a warning.
        // The save-time resolution above should prevent this, but be defensive.
        if (saved.color_mode != light::ColorMode::UNKNOWN)
          call.set_color_mode(saved.color_mode);
        call.set_brightness(saved.values.get_brightness());
        call.set_rgb(saved.values.get_red(), saved.values.get_green(),
                     saved.values.get_blue());

        // Only set white if the strip supports it in this mode
        if (l->get_traits().supports_color_mode(saved.color_mode) &&
            (saved.color_mode == light::ColorMode::RGB_WHITE ||
             saved.color_mode == light::ColorMode::RGB_COLD_WARM_WHITE ||
             saved.color_mode == light::ColorMode::COLD_WARM_WHITE ||
             saved.color_mode == light::ColorMode::WHITE)) {
          call.set_white(saved.values.get_white());
        }

        // Only restore effect if the light is on
        // Setting an effect (even "None") while turning off triggers warnings.
        if (turning_on) {
          call.set_effect(saved.effect);
        }

        call.perform();
      }
      light_idx++;
    }
  } else {
    // restore: false — clear the effect first, then turn lights off.
    // Two separate calls are required: ESPHome rejects set_effect("None")
    // when combined with set_state(false) in the same call, producing:
    //   [W] 'RGB Light': cannot start effect when turning off
    // If the effect is not cleared before turning off, ESPHome remembers it
    // and re-applies it the next time the light turns on.
    for (auto *l : this->lights_) {
      auto clear_call = l->make_call();
      clear_call.set_effect("None");
      clear_call.set_transition_length(0);
      clear_call.perform();

      auto off_call = l->make_call();
      off_call.set_state(false);
      off_call.set_transition_length(0);
      off_call.perform();
    }
  }

  this->is_stopping_ = false;

  // Update Select UI to reflect the stopped sequence.
  // Pool-owned sequences are not in the select option list — skip.
  if (!this->is_pool_owned_ &&
      CFXSequenceSelect::instance != nullptr &&
      CFXSequenceSelect::instance->has_state()) {
    const std::string &current = CFXSequenceSelect::instance->current_option();
    if (!current.empty() && this->name_ == current) {
      CFXSequenceSelect::instance->publish_state_silent("None");
    }
  }

  // Pool self-release: return slot so it's available for the next cfx_run.
  if (this->is_pool_owned_)
    CFXRunPool::get().release(this);
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

  if (this->restore_state_) {
    size_t idx = 0;
    for (auto *l : this->lights_) {
      if (idx < this->saved_states_.size()) {
        auto &saved = this->saved_states_[idx];
        auto call = l->make_call();
        call.set_transition_length(0);
        bool on = saved.values.is_on();
        call.set_state(on);
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
        if (on) call.set_effect(saved.effect);
        call.perform();
      }
      idx++;
    }
  } else {
    for (auto *l : this->lights_) {
      auto clr = l->make_call();
      clr.set_effect("None");
      clr.set_transition_length(0);
      clr.perform();
      auto off = l->make_call();
      off.set_state(false);
      off.set_transition_length(0);
      off.perform();
    }
  }

  this->is_stopping_ = false;

  if (!this->is_pool_owned_ &&
      CFXSequenceSelect::instance != nullptr &&
      CFXSequenceSelect::instance->has_state()) {
    const std::string &cur = CFXSequenceSelect::instance->current_option();
    if (!cur.empty() && this->name_ == cur)
      CFXSequenceSelect::instance->publish_state_silent("None");
  }

  // LAST: fire on_complete — any chained cfx_set now wins with no
  // subsequent stop() able to undo the next step.
  this->report_event_complete();

  // Pool self-release: return slot after completion so it's available
  // for the next cfx_run call. Must be last — report_event_complete()
  // may fire on_complete triggers that spawn new cfx_run sequences.
  if (this->is_pool_owned_)
    CFXRunPool::get().release(this);
}

void CFXSequence::force_reset() {
  // Hard reset: turn off all lights managed by this sequence.
  // Called by "None" handler to ensure lights are off even when
  // the sequence has already completed and on_complete overrode state.
  // Guard: skip sequences that were never started — firing light calls on
  // non-running sequences produces ESPHome warnings on unrelated lights.
  if (!this->is_running_ && !this->completion_reported_)
    return;

  for (auto *l : this->lights_) {
    // Two-call pattern required: ESPHome rejects set_effect("None") combined
    // with set_state(false) in a single call, producing:
    //   [W] 'X': effect cannot be used with transition/flash
    //   [W] 'X': cannot start effect when turning off
    auto clear_call = l->make_call();
    clear_call.set_effect("None");
    clear_call.set_transition_length(0);
    clear_call.perform();

    auto off_call = l->make_call();
    off_call.set_state(false);
    off_call.set_transition_length(0);
    off_call.perform();
  }
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
    clear_call.set_transition_length(0);
    clear_call.perform();

    auto off_call = l->make_call();
    off_call.set_state(false);
    off_call.set_transition_length(0);
    off_call.perform();
  }

  this->is_stopping_ = false;
}

void CFXSequence::stop_all() {
  std::set<light::LightState *> unique_lights;
  for (auto *seq : CFXSequence::instances) {
    for (auto *l : seq->lights_) {
      unique_lights.insert(l);
    }
    if (seq->is_running_) {
      seq->is_running_ = false;
      seq->clear_active_binding();
      seq->duration_complete_fired_ = false;
    }
  }

  // Two calls required: see stop() else branch for explanation.
  for (auto *l : unique_lights) {
    auto clear_call = l->make_call();
    clear_call.set_effect("None");
    clear_call.set_transition_length(0);
    clear_call.perform();

    auto off_call = l->make_call();
    off_call.set_state(false);
    off_call.set_transition_length(0);
    off_call.perform();
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
  for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
    if (inst->get_active_sequence() == this) {
      inst->set_is_sequence_outro(this->completion_reported_);
      inst->set_active_sequence(nullptr, {}, {}, {}, 0);
    }
  }
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
                                      (uint32_t)t.trigger, 0, [t]() {
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
                                      (uint32_t)t, 0, [t]() { t->trigger(); });
  }
  // NOTE: cfx_start HA event is fired by CFXAddressableLightEffect::start()
  // unconditionally for all effects and all paths. Do NOT fire it here again.
}

void CFXSequence::report_event_begin() {
  ESP_LOGV(TAG, "Sequence '%s': on_begin triggers firing", this->id_.c_str());
  // on_cfx_begin YAML trigger always fires (on-device automation).
  for (auto *t : this->on_begin_triggers_) {
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance, 
                                      (uint32_t)t, 0, [t]() { t->trigger(); });
  }
  // CFX-029: HA cfx_begin event only fires when the sequence has a real
  // intro configured (intro_ != 0). Without an intro, cfx_begin and
  // cfx_start fire at the same millisecond and are redundant.
  if (!this->strip_tag_.empty()
      && this->intro_.has_value() && this->intro_.value() != 0) {
    std::string evt = std::string("cfx_begin:") + this->strip_tag_;
    CFXEventManager::get().fire_event(evt.c_str());
  }
}

void CFXSequence::report_event_stop() {
  ESP_LOGV(TAG, "Sequence '%s': on_stop triggers firing", this->id_.c_str());
  for (auto *t : this->on_stop_triggers_) {
    esphome::App.scheduler.set_timeout(CFXSequenceSelect::instance, 
                                      (uint32_t)t, 0, [t]() { t->trigger(); });
  }
  // Fire cfx_stop HA event — outro is beginning (or sequence is stopping).
  if (!this->strip_tag_.empty()) {
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
      // Small verification: only fire if the sequence didn't restart mid-deferral
      if (s->completion_reported_)
        t->trigger();
    });
  }
  // Fire tagged cfx_complete using this sequence's own strip_tag_.
  // No global singleton state — each sequence instance fires for its own strip.
  if (!this->strip_tag_.empty()) {
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
        ESP_LOGV(TAG, "Sequence '%s': on_reach %.2f%% queued",
                 this->id_.c_str(), target * 100.0f);
        this->pending_reach_triggers_.push_back({t, current_percentage});
      }
    }

    this->last_triggered_percentage_ = current_percentage;
  }

  this->last_triggered_pixel_ = current_pixel;

  // CFX-053: Milestones (cfx_reach HA events) fire ONLY on the forward pass.
  // The erase/return pass of bidirectional effects (e.g. Wipe) sweeps 0→100%
  // again, which would double-fire every milestone. The forward pass is
  // "the only one that counts" — erase pass milestones are suppressed.
  if (!is_return_phase) {
    this->check_milestones_(current_percentage * 100.0f);
  }
}

void CFXSequence::CFXSequenceListener::on_light_remote_values_update() {
  if (this->parent_ != nullptr && this->parent_->is_running() && 
      this->parent_->is_stagger_complete() && !this->light_->remote_values.is_on()) {
    ESP_LOGV("cfx_sequence",
             "Sequence '%s' stopping because light '%s' turned off",
             this->parent_->get_name().c_str(), this->light_->get_name().c_str());
    this->parent_->stop();
  }
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
