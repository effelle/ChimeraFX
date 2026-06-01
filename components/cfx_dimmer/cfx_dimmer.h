#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <string>
#include <vector>

namespace esphome {
namespace cfx_dimmer {

class CFXDimmer : public Component {
 public:
  explicit CFXDimmer(const std::string &id) : id_(id) {}
  void setup() override;
  void loop() override;

  void add_light(light::LightState *state) { this->lights_.push_back(state); }
  void set_long_press_ms(uint32_t value) { this->long_press_ms_ = value; }
  void set_ramp_time_ms(uint32_t value) { this->ramp_time_ms_ = value; }
  void set_min_brightness(float value) { this->min_brightness_ = value; }
  void set_max_brightness(float value) { this->max_brightness_ = value; }
  void set_restore_direction(bool value) { this->restore_direction_ = value; }

  void press();
  void release();

 protected:
  static constexpr uint32_t RAMP_UPDATE_INTERVAL_MS = 35;

  void start_ramp_(uint32_t now);
  void apply_ramp_(uint32_t now);
  void apply_brightness_(light::LightState *state, float brightness);
  void save_direction_();
  void toggle_targets_();
  bool any_target_on_() const;
  float target_start_brightness_(light::LightState *state) const;
  float clamp_brightness_(float value) const;

  std::vector<light::LightState *> lights_;
  std::vector<float> ramp_start_brightness_;
  std::string id_;
  ESPPreferenceObject direction_pref_{};
  bool direction_pref_ready_{false};
  uint32_t long_press_ms_{500};
  uint32_t ramp_time_ms_{2000};
  float min_brightness_{0.01f};
  float max_brightness_{1.0f};
  bool restore_direction_{false};
  bool next_direction_up_{true};
  bool pressed_{false};
  bool ramping_{false};
  bool suppress_toggle_{false};
  bool ramp_direction_up_{true};
  uint32_t press_started_ms_{0};
  uint32_t ramp_started_ms_{0};
  uint32_t last_ramp_update_ms_{0};
};

template<typename... Ts> class PressAction : public ::esphome::Action<Ts...> {
 public:
  explicit PressAction(CFXDimmer *dimmer) : dimmer_(dimmer) {}
  void play(const Ts &...x) override {
    if (this->dimmer_ != nullptr) {
      this->dimmer_->press();
    }
  }

 protected:
  CFXDimmer *dimmer_;
};

template<typename... Ts> class ReleaseAction : public ::esphome::Action<Ts...> {
 public:
  explicit ReleaseAction(CFXDimmer *dimmer) : dimmer_(dimmer) {}
  void play(const Ts &...x) override {
    if (this->dimmer_ != nullptr) {
      this->dimmer_->release();
    }
  }

 protected:
  CFXDimmer *dimmer_;
};

}  // namespace cfx_dimmer
}  // namespace esphome
