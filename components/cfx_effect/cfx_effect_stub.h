/*
 * ChimeraFX - CFXEffectStub
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Licensed under the EUPL-1.2
 *
 * Slim proxy that populates the HA effect dropdown for virtual segments
 * without carrying full CFXAddressableLightEffect runner/config overhead.
 * All rendering is delegated to a per-segment singleton effect instance.
 *
 * Architecture:
 *   Many stubs delegate to 1 singleton.
 *   HA sees all effect names. Only the singleton allocates CFXActivation/CFXRunner.
 *
 * Memory savings: keeps each selectable segment effect to a tiny proxy.
 */

#pragma once

#include "cfx_addressable_light_effect.h"
#include "esphome/components/light/addressable_light_effect.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/core/log.h"

namespace esphome {
namespace chimera_fx {

class CFXEffectStub : public light::AddressableLightEffect {
public:
  CFXEffectStub(const char *name, uint8_t effect_id,
                CFXAddressableLightEffect *singleton)
      : AddressableLightEffect(name),
        effect_id_(effect_id),
        singleton_(singleton) {}

  /// Called by ESPHome when this stub is selected from the HA effect dropdown.
  /// ESPHome guarantees: old_effect.stop() completes before new_effect.start().
  /// Flow: ESPHome calls stub->start_internal() which handles
  /// set_effect_active(true) + clear_effect_data(), then calls this start().
  void start() override {
    // Inject the segment's LightState so the singleton can access the
    // CFXVirtualSegmentLight output, find its CFXControl, and derive
    // its strip_tag for events.
    static uint8_t start_diag_logs = 0;
    if (start_diag_logs < 32) {
      auto *state = this->get_light_state();
      ESP_LOGI("cfx_stub",
               "Segment effect start[%u]: light='%s' "
               "state=%p output=%p stub=%p singleton=%p effect_id=%u "
               "effect='%s'",
               static_cast<unsigned>(start_diag_logs),
               state != nullptr ? state->get_name().c_str() : "<null>",
               state, state != nullptr ? state->get_output() : nullptr, this,
               singleton_, static_cast<unsigned>(effect_id_),
               this->get_name().c_str());
      start_diag_logs++;
    }
    singleton_->init_internal(this->get_light_state());
    singleton_->set_effect_id(effect_id_);
    singleton_->start();
  }

  /// Called by ESPHome when the effect is deactivated or a different effect
  /// is selected. Tears down the singleton's CFXActivation + CFXRunner.
  void stop() override {
    static uint8_t stop_diag_logs = 0;
    auto *state = this->get_light_state();
    if (stop_diag_logs < 32) {
      ESP_LOGI("cfx_stub",
               "Segment effect stop[%u]: light='%s' "
               "state=%p output=%p stub=%p singleton=%p effect_id=%u "
               "effect='%s'",
               static_cast<unsigned>(stop_diag_logs),
               state != nullptr ? state->get_name().c_str() : "<null>",
               state, state != nullptr ? state->get_output() : nullptr, this,
               singleton_, static_cast<unsigned>(effect_id_),
               this->get_name().c_str());
      stop_diag_logs++;
    }
    singleton_->init_internal(state);
    singleton_->set_effect_id(effect_id_);
    singleton_->stop();
  }

  /// Called every frame by AddressableLightEffect::apply() after its
  /// rate-gate passes. Delegates rendering to the singleton.
  void apply(light::AddressableLight &it, const Color &current_color) override {
    singleton_->apply(it, current_color);
  }

  /// Expose the effect_id for diagnostics and cfx_set routing.
  uint8_t get_effect_id() const { return effect_id_; }

  /// Expose the underlying singleton effect so the coordinator can resolve it.
  CFXAddressableLightEffect *get_singleton() const { return singleton_; }

private:
  uint8_t effect_id_;
  CFXAddressableLightEffect *singleton_;
};

}  // namespace chimera_fx
}  // namespace esphome
