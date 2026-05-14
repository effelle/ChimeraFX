/*
 * ChimeraFX - CFXEffectStub
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Licensed under the EUPL-1.2
 *
 * Lightweight proxy that populates the HA effect dropdown for virtual segments
 * without carrying the full CFXAddressableLightEffect overhead (~60 B → ~36 B).
 * All rendering is delegated to a per-segment singleton effect instance.
 *
 * Architecture:
 *   81 stubs (36 B each) ──delegate──▶ 1 singleton (60 B)
 *   HA sees 81 effect names.  Only the singleton allocates CFXActivation/CFXRunner.
 *
 * Memory savings: ~2.2 KB per segment (37% reduction).
 */

#pragma once

#include "cfx_addressable_light_effect.h"
#include "esphome/components/light/addressable_light_effect.h"
#include "esphome/components/light/light_effect.h"
#include <vector>

namespace esphome {
namespace chimera_fx {

class CFXEffectStub : public light::AddressableLightEffect {
public:
  static std::vector<CFXEffectStub *> &all_stubs() {
    static std::vector<CFXEffectStub *> stubs;
    return stubs;
  }

  CFXEffectStub(const char *name, uint8_t effect_id,
                CFXAddressableLightEffect *singleton)
      : AddressableLightEffect(name),
        effect_id_(effect_id),
        singleton_(singleton) {
    all_stubs().push_back(this);
  }

  /// Called by ESPHome when this stub is selected from the HA effect dropdown.
  /// ESPHome guarantees: old_effect.stop() completes before new_effect.start().
  /// Flow: ESPHome calls stub->start_internal() which handles
  /// set_effect_active(true) + clear_effect_data(), then calls this start().
  void start() override {
    // Inject the segment's LightState so the singleton can access the
    // CFXVirtualSegmentLight output, find its CFXControl, and derive
    // its strip_tag for events.
    singleton_->init_internal(this->get_light_state());
    singleton_->set_effect_id(effect_id_);
    singleton_->start();
  }

  /// Called by ESPHome when the effect is deactivated or a different effect
  /// is selected. Tears down the singleton's CFXActivation + CFXRunner.
  void stop() override {
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
