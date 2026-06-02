#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include <string>
#include <vector>

namespace esphome {
namespace cfx_effect_selector {

class CFXEffectSelector : public Component {
 public:
  explicit CFXEffectSelector(const std::string &id) : id_(id) {}
  void loop() override;

  void add_light(light::LightState *state) { this->lights_.push_back(state); }
  void add_effect(const std::string &effect) { this->effects_.push_back(effect); }
  void set_long_press_ms(uint32_t value) { this->long_press_ms_ = value; }
  void set_effect_interval_ms(uint32_t value) {
    this->effect_interval_ms_ = value;
  }

  void press();
  void release();

 protected:
  static constexpr uint32_t POST_SELECT_GUARD_MS = 350;

  void start_selection_(uint32_t now);
  void select_next_effect_(uint32_t now);
  void sync_index_from_targets_();
  void apply_effect_(const std::string &effect);
  void toggle_targets_();
  bool any_target_on_() const;

  std::vector<light::LightState *> lights_;
  std::vector<std::string> effects_;
  std::string id_;
  uint32_t long_press_ms_{500};
  uint32_t effect_interval_ms_{900};
  uint32_t press_started_ms_{0};
  uint32_t last_effect_update_ms_{0};
  uint32_t ignore_press_until_ms_{0};
  size_t active_index_{0};
  bool pressed_{false};
  bool selecting_{false};
  bool suppress_toggle_{false};
};

template<typename... Ts> class PressAction : public ::esphome::Action<Ts...> {
 public:
  explicit PressAction(CFXEffectSelector *selector) : selector_(selector) {}
  void play(const Ts &...x) override {
    if (this->selector_ != nullptr) {
      this->selector_->press();
    }
  }

 protected:
  CFXEffectSelector *selector_;
};

template<typename... Ts> class ReleaseAction : public ::esphome::Action<Ts...> {
 public:
  explicit ReleaseAction(CFXEffectSelector *selector) : selector_(selector) {}
  void play(const Ts &...x) override {
    if (this->selector_ != nullptr) {
      this->selector_->release();
    }
  }

 protected:
  CFXEffectSelector *selector_;
};

}  // namespace cfx_effect_selector
}  // namespace esphome
