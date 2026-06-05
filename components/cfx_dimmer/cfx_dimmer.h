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

  void add_light(light::LightState *state);
  void set_long_press_ms(uint32_t value) { this->long_press_ms_ = value; }
  void set_ramp_time_ms(uint32_t value) { this->ramp_time_ms_ = value; }
  void set_min_brightness(float value) { this->min_brightness_ = value; }
  void set_max_brightness(float value) { this->max_brightness_ = value; }
  void set_off_brightness(float value) { this->off_brightness_ = value; }
  void set_restore_direction(bool value) { this->restore_direction_ = value; }

  void press();
  void release();

 protected:
  struct SavedState {
    bool valid{false};
    float brightness{1.0f};
    std::string effect{};
  };

  static constexpr uint32_t MIN_RAMP_TRANSITION_MS = 50;
  static constexpr uint32_t RAMP_UPDATE_INTERVAL_MS = 50;
  static constexpr uint32_t POST_ACTION_QUIET_MS = 350;
  static constexpr float OFF_BRIGHTNESS_HYSTERESIS = 0.015f;

  void finalize_release_(uint32_t released_at_ms);
  void start_ramp_(uint32_t now);
  void finish_ramp_();
  void freeze_ramp_(uint32_t now);
  void service_manual_ramp_(uint32_t now);
  void apply_brightness_(light::LightState *state, float brightness,
                         uint32_t transition_ms);
  void restore_saved_state_(size_t index, light::LightState *state);
  void save_target_state_(size_t index, light::LightState *state);
  void save_direction_();
  void toggle_targets_();
  void turn_off_targets_();
  float off_visibility_cutoff_() const;
  bool any_target_visible_() const;
  uint32_t ramp_duration_ms_(float start, float target) const;
  float ramp_target_brightness_() const;
  float target_start_brightness_(light::LightState *state) const;
  float ramp_current_brightness_(size_t index, uint32_t now) const;
  float toggle_on_brightness_(light::LightState *state) const;
  bool target_has_effect_(light::LightState *state) const;
  float clamp_brightness_(float value) const;

  std::vector<light::LightState *> lights_;
  std::vector<SavedState> saved_states_;
  std::string id_;
  ESPPreferenceObject direction_pref_{};
  bool direction_pref_ready_{false};
  uint32_t long_press_ms_{500};
  uint32_t ramp_time_ms_{2000};
  uint32_t ramp_end_ms_{0};
  uint32_t last_ramp_update_ms_{0};
  uint32_t ignore_press_until_ms_{0};
  float min_brightness_{0.01f};
  float max_brightness_{1.0f};
  float off_brightness_{0.10f};
  bool restore_direction_{false};
  bool next_direction_up_{true};
  bool pressed_{false};
  bool ramping_{false};
  bool ramp_finished_{false};
  bool suppress_toggle_{false};
  bool ramp_direction_up_{true};
  uint32_t press_started_ms_{0};
  uint32_t ramp_started_ms_{0};
  std::vector<float> ramp_start_brightness_;
  std::vector<uint32_t> ramp_durations_ms_;
  std::vector<bool> ramp_manual_;
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
