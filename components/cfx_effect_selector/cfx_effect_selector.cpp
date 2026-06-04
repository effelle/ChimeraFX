#include "cfx_effect_selector.h"

#include "esphome/core/helpers.h"

namespace esphome {
namespace cfx_effect_selector {

void CFXEffectSelector::press() {
  if (this->pressed_) {
    return;
  }
  const uint32_t now = millis();
  if ((int32_t) (now - this->ignore_press_until_ms_) < 0) {
    return;
  }
  this->pressed_ = true;
  this->selecting_ = false;
  this->suppress_toggle_ = false;
  this->press_started_ms_ = now;
  this->last_effect_update_ms_ = 0;
}

void CFXEffectSelector::release() {
  if (!this->pressed_) {
    return;
  }
  if (this->suppress_toggle_) {
    this->ignore_press_until_ms_ = millis() + POST_SELECT_GUARD_MS;
  }
  const bool should_toggle = !this->suppress_toggle_ && !this->selecting_;
  this->pressed_ = false;
  this->selecting_ = false;
  this->suppress_toggle_ = false;
  this->last_effect_update_ms_ = 0;

  if (should_toggle) {
    this->toggle_targets_();
  }
}

void CFXEffectSelector::loop() {
  const uint32_t now = millis();
  this->service_dispatch_(now);

  if (!this->pressed_ || this->effects_.empty()) {
    return;
  }
  if (!this->selecting_) {
    if ((now - this->press_started_ms_) < this->long_press_ms_) {
      return;
    }
    this->start_selection_(now);
    return;
  }
  if ((now - this->last_effect_update_ms_) >= this->effect_interval_ms_) {
    this->select_next_effect_(now);
  }
}

void CFXEffectSelector::start_selection_(uint32_t now) {
  const bool synced_from_active_target = this->sync_index_from_targets_();
  this->selecting_ = true;
  this->suppress_toggle_ = true;
  if (this->active_index_known_ && !synced_from_active_target) {
    this->last_effect_update_ms_ = now;
    this->apply_effect_(this->effects_[this->active_index_]);
    return;
  }
  this->select_next_effect_(now);
}

void CFXEffectSelector::select_next_effect_(uint32_t now) {
  if (this->effects_.empty()) {
    return;
  }
  if (!this->active_index_known_) {
    this->active_index_ = this->effects_.size() - 1;
  }
  this->active_index_ = (this->active_index_ + 1) % this->effects_.size();
  this->active_index_known_ = true;
  this->last_effect_update_ms_ = now;
  this->apply_effect_(this->effects_[this->active_index_]);
}

bool CFXEffectSelector::sync_index_from_targets_() {
  if (this->effects_.empty()) {
    return false;
  }
  for (auto *state : this->lights_) {
    if (state == nullptr || !state->remote_values.is_on()) {
      continue;
    }
    const std::string current = state->get_effect_name();
    for (size_t i = 0; i < this->effects_.size(); i++) {
      if (this->effects_[i] == current) {
        this->active_index_ = i;
        this->active_index_known_ = true;
        return true;
      }
    }
  }
  return false;
}

void CFXEffectSelector::apply_effect_(const std::string &effect) {
  this->begin_dispatch_(true, true, true, effect, false);
}

void CFXEffectSelector::toggle_targets_() {
  const bool turn_off = this->any_target_on_();
  const bool set_effect = !turn_off && !this->effects_.empty();
  const std::string effect =
      set_effect ? this->effects_[this->active_index_] : std::string();
  if (set_effect) {
    this->active_index_known_ = true;
  }
  this->begin_dispatch_(true, !turn_off, set_effect, effect, turn_off);
}

void CFXEffectSelector::begin_dispatch_(bool set_state, bool state_value,
                                        bool set_effect,
                                        const std::string &effect,
                                        bool transition_off) {
  this->dispatch_active_ = false;
  this->dispatch_set_state_ = set_state;
  this->dispatch_state_value_ = state_value;
  this->dispatch_set_effect_ = set_effect;
  this->dispatch_effect_ = effect;
  this->dispatch_transition_off_ = transition_off;
  this->dispatch_index_ = 0;
  this->dispatch_next_ms_ = millis();

  if (this->lights_.size() <= GROUP_DISPATCH_THRESHOLD) {
    for (auto *state : this->lights_) {
      this->perform_dispatch_call_(state);
    }
    return;
  }

  this->dispatch_active_ = true;
  this->service_dispatch_(this->dispatch_next_ms_);
}

bool CFXEffectSelector::service_dispatch_(uint32_t now) {
  if (!this->dispatch_active_) {
    return false;
  }
  if ((int32_t) (now - this->dispatch_next_ms_) < 0) {
    return false;
  }

  while (this->dispatch_index_ < this->lights_.size()) {
    auto *state = this->lights_[this->dispatch_index_++];
    if (state == nullptr) {
      continue;
    }
    this->perform_dispatch_call_(state);
    if (this->dispatch_index_ >= this->lights_.size()) {
      this->dispatch_active_ = false;
    } else {
      this->dispatch_next_ms_ = millis() + GROUP_DISPATCH_INTERVAL_MS;
    }
    return true;
  }

  this->dispatch_active_ = false;
  return true;
}

void CFXEffectSelector::perform_dispatch_call_(light::LightState *state) {
  if (state == nullptr) {
    return;
  }
  auto call = state->make_call();
  if (this->dispatch_set_state_) {
    call.set_state(this->dispatch_state_value_);
  }
  if (this->dispatch_transition_off_) {
    call.set_transition_length(0);
  }
  if (this->dispatch_set_effect_) {
    call.set_effect(this->dispatch_effect_);
  }
  call.perform();
}

bool CFXEffectSelector::any_target_on_() const {
  for (auto *state : this->lights_) {
    if (state != nullptr && state->remote_values.is_on()) {
      return true;
    }
  }
  return false;
}

}  // namespace cfx_effect_selector
}  // namespace esphome
