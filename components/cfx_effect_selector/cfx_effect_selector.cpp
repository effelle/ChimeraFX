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
  if (!this->pressed_ || this->effects_.empty()) {
    return;
  }
  const uint32_t now = millis();
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
  this->sync_index_from_targets_();
  this->selecting_ = true;
  this->suppress_toggle_ = true;
  this->select_next_effect_(now);
}

void CFXEffectSelector::select_next_effect_(uint32_t now) {
  if (this->effects_.empty()) {
    return;
  }
  this->active_index_ = (this->active_index_ + 1) % this->effects_.size();
  this->last_effect_update_ms_ = now;
  this->apply_effect_(this->effects_[this->active_index_]);
}

void CFXEffectSelector::sync_index_from_targets_() {
  if (this->effects_.empty()) {
    return;
  }
  for (auto *state : this->lights_) {
    if (state == nullptr || !state->remote_values.is_on()) {
      continue;
    }
    const std::string current = state->get_effect_name();
    for (size_t i = 0; i < this->effects_.size(); i++) {
      if (this->effects_[i] == current) {
        this->active_index_ = i;
        return;
      }
    }
  }
  this->active_index_ = this->effects_.size() - 1;
}

void CFXEffectSelector::apply_effect_(const std::string &effect) {
  for (auto *state : this->lights_) {
    if (state == nullptr) {
      continue;
    }
    auto call = state->make_call();
    call.set_state(true);
    call.set_effect(effect);
    call.perform();
  }
}

void CFXEffectSelector::toggle_targets_() {
  const bool turn_off = this->any_target_on_();
  for (auto *state : this->lights_) {
    if (state == nullptr) {
      continue;
    }
    auto call = state->make_call();
    call.set_state(!turn_off);
    if (turn_off) {
      call.set_transition_length(0);
    }
    if (!turn_off && !this->effects_.empty()) {
      call.set_effect(this->effects_[this->active_index_]);
    }
    call.perform();
  }
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
