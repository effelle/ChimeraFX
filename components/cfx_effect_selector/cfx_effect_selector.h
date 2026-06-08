#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <string>
#include <vector>

namespace esphome {
namespace cfx_effect_selector {

class CFXEffectSelector : public Component {
 public:
  explicit CFXEffectSelector(const std::string &id) : id_(id) {}
  void setup() override;
  void loop() override;

  void add_light(light::LightState *state) { this->lights_.push_back(state); }
  void add_effect(const std::string &effect) { this->effects_.push_back(effect); }
  void set_long_press_ms(uint32_t value) { this->long_press_ms_ = value; }
  void set_effect_interval_ms(uint32_t value) {
    this->effect_interval_ms_ = value;
  }
  void set_restore(bool value) { this->restore_ = value; }

  void press();
  void release();

 protected:
  static constexpr uint32_t POST_SELECT_GUARD_MS = 350;
  static constexpr size_t GROUP_DISPATCH_THRESHOLD = 2;
  static constexpr uint32_t GROUP_DISPATCH_INTERVAL_MS = 8;

  void start_selection_(uint32_t now);
  void select_next_effect_(uint32_t now);
  bool sync_index_from_targets_();
  void save_active_index_();
  void apply_effect_(const std::string &effect);
  void toggle_targets_();
  void begin_dispatch_(bool set_state, bool state_value, bool set_effect,
                       const std::string &effect,
                       bool use_default_transition);
  bool service_dispatch_(uint32_t now);
  void perform_dispatch_call_(light::LightState *state);
  bool any_target_on_() const;

  std::vector<light::LightState *> lights_;
  std::vector<std::string> effects_;
  std::string id_;
  ESPPreferenceObject active_index_pref_{};
  uint32_t long_press_ms_{500};
  uint32_t effect_interval_ms_{900};
  uint32_t press_started_ms_{0};
  uint32_t last_effect_update_ms_{0};
  uint32_t ignore_press_until_ms_{0};
  size_t active_index_{0};
  bool active_index_pref_ready_{false};
  bool active_index_known_{false};
  bool restore_{false};
  bool pressed_{false};
  bool selecting_{false};
  bool suppress_toggle_{false};
  bool dispatch_active_{false};
  bool dispatch_set_state_{false};
  bool dispatch_state_value_{false};
  bool dispatch_set_effect_{false};
  bool dispatch_use_default_transition_{false};
  size_t dispatch_index_{0};
  uint32_t dispatch_next_ms_{0};
  std::string dispatch_effect_;
};

}  // namespace cfx_effect_selector
}  // namespace esphome
