#pragma once

#include "cfx_dimmer_gesture.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"
#include <string>
#include <vector>

namespace esphome {
namespace cfx_dimmer {

class CFXDimmer : public Component {
 public:
  explicit CFXDimmer(const std::string &id) : id_(id) {}
  void loop() override;

  void add_light(light::LightState *state);
  void set_long_press_ms(uint32_t value) { this->long_press_ms_ = value; }
  void set_ramp_time_ms(uint32_t value) { this->ramp_time_ms_ = value; }
  void set_min_brightness(float value) { this->min_brightness_ = value; }
  void set_max_brightness(float value) { this->max_brightness_ = value; }

  void press();
  void release();

 protected:
  static constexpr uint32_t MIN_RAMP_TRANSITION_MS = 50;
  static constexpr uint32_t RAMP_UPDATE_INTERVAL_MS = 50;
  static constexpr uint32_t POST_ACTION_QUIET_MS = 350;

  void finalize_release_(DimmerReleaseAction action, uint32_t released_at_ms);
  void start_ramp_(uint32_t now);
  void finish_ramp_();
  void freeze_ramp_(uint32_t now);
  void service_manual_ramp_(uint32_t now);
  void apply_brightness_(light::LightState *state, float brightness,
                         uint32_t transition_ms);
  void turn_on_targets_();
  void turn_off_targets_();
  bool any_target_on_() const;
  float average_target_brightness_() const;
  uint32_t ramp_duration_ms_(float start, float target) const;
  float ramp_target_brightness_() const;
  float target_start_brightness_(light::LightState *state) const;
  float ramp_current_brightness_(size_t index, uint32_t now) const;
  bool target_has_effect_(light::LightState *state) const;
  float clamp_brightness_(float value) const;

  std::vector<light::LightState *> lights_;
  std::string id_;
  uint32_t long_press_ms_{500};
  uint32_t ramp_time_ms_{2000};
  uint32_t ramp_end_ms_{0};
  uint32_t last_ramp_update_ms_{0};
  uint32_t ignore_press_until_ms_{0};
  float min_brightness_{0.15f};
  float max_brightness_{1.0f};
  bool next_direction_up_{false};
  bool pressed_{false};
  bool ramping_{false};
  bool ramp_finished_{false};
  bool ramp_direction_up_{true};
  bool first_ramp_after_boot_{true};
  DimmerGesture gesture_{};
  uint32_t press_started_ms_{0};
  uint32_t ramp_started_ms_{0};
  std::vector<float> ramp_start_brightness_;
  std::vector<uint32_t> ramp_durations_ms_;
  std::vector<bool> ramp_manual_;
};

}  // namespace cfx_dimmer
}  // namespace esphome
