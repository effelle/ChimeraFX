#pragma once

#include "cfx_button_sync_command.h"
#include "cfx_cct_policy.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <functional>
#include <string>
#include <vector>

namespace esphome {
namespace cfx_cct_sweeper {

struct CFXColor {
  float red{1.0f};
  float green{1.0f};
  float blue{1.0f};
  float white{0.0f};
};

class CFXCCTSweeper : public Component {
 public:
  using SyncCommandCallback =
      std::function<void(const cfx_button::CFXButtonSyncCommand &command)>;

  explicit CFXCCTSweeper(const std::string &id) : id_(id) {}
  void setup() override;
  void loop() override;

  void add_light(light::LightState *state);
  void set_long_press_ms(uint32_t value) { this->long_press_ms_ = value; }
  void set_sweep_time_ms(uint32_t value) { this->sweep_time_ms_ = value; }
  void set_native_white(float r, float g, float b, float w) {
    this->native_white_ = {r, g, b, w};
  }
  void set_preferred_white(float r, float g, float b, float w) {
    this->preferred_white_ = {r, g, b, w};
  }
  void set_restore(bool value) { this->restore_ = value; }
  void add_sync_command_callback(SyncCommandCallback callback) {
    this->sync_command_callbacks_.push_back(callback);
  }

  void press();
  void release();

 protected:
  struct SweepTarget {
    bool valid{false};
    CFXColor start{};
    uint32_t duration_ms{0};
  };

  struct StoredPreferredWhite {
    uint32_t version{1};
    float red{1.0f};
    float green{1.0f};
    float blue{1.0f};
    float white{1.0f};
  };

  static constexpr uint32_t MIN_SWEEP_TRANSITION_MS = 50;
  static constexpr uint32_t POST_SWEEP_GUARD_MS = 350;
  static constexpr uint32_t USE_DEFAULT_TRANSITION = UINT32_MAX;
  static constexpr float WHITE_MATCH_TOLERANCE = 0.04f;
  static constexpr float SWEEP_MEASURED_MAX_DRIFT = 0.20f;
  static constexpr uint32_t PREFERRED_WHITE_VERSION = 1;

  void start_sweep_(uint32_t now);
  void finish_sweep_();
  void freeze_sweep_();
  CFXColor sweep_color_at_(const SweepTarget &target, uint32_t now) const;
  bool measured_sweep_color_(light::LightState *state,
                             const CFXColor &estimated,
                             CFXColor &measured) const;
  void handle_short_press_();
  void restore_retained_state_();
  bool emit_sync_retained_state_();
  void emit_sync_color_(const CFXColor &color, uint32_t transition_ms);
  void apply_color_(light::LightState *state, const CFXColor &color,
                    uint32_t transition_ms);
  void apply_color_to_all_(const CFXColor &color, uint32_t transition_ms);
  void save_preferred_white_();
  void log_configured_colors_() const;
  void log_light_state_(light::LightState *state, const char *context) const;
  bool any_target_on_() const;
  CCTEndpoint current_endpoint_() const;
  bool matches_color_(light::LightState *state, const CFXColor &color) const;
  uint32_t sweep_duration_ms_(const CFXColor &start,
                              const CFXColor &target) const;
  CFXColor sweep_start_color_(light::LightState *state) const;
  CFXColor remote_color_(light::LightState *state) const;
  CFXColor sweep_target_() const;
  CFXColor clamp_color_(const CFXColor &color) const;
  float color_distance_(const CFXColor &a, const CFXColor &b) const;

  std::vector<light::LightState *> lights_;
  std::vector<SweepTarget> sweep_targets_;
  std::vector<SyncCommandCallback> sync_command_callbacks_;
  std::string id_;
  ESPPreferenceObject preferred_white_pref_{};
  bool preferred_white_pref_ready_{false};
  uint32_t long_press_ms_{500};
  uint32_t sweep_time_ms_{4000};
  uint32_t sweep_end_ms_{0};
  uint32_t sweep_started_ms_{0};
  uint32_t ignore_press_until_ms_{0};
  CFXColor active_sweep_target_{1.0f, 0.55f, 0.18f, 1.0f};
  CFXColor native_white_{0.0f, 0.0f, 0.0f, 1.0f};
  CFXColor preferred_white_{1.0f, 1.0f, 1.0f, 1.0f};
  const CFXColor warm_white_{1.0f, 0.55f, 0.18f, 1.0f};
  const CFXColor cool_white_{0.70f, 0.85f, 1.0f, 1.0f};
  bool restore_{false};
  bool next_direction_warmer_{true};
  bool first_sweep_after_boot_{true};
  bool gesture_began_on_{false};
  bool has_retained_state_{false};
  bool pressed_{false};
  bool sweeping_{false};
  bool sweep_finished_{false};
  bool suppress_toggle_{false};
  bool sweep_direction_warmer_{true};
  CCTEndpoint last_endpoint_{CCTEndpoint::PREFERRED};
  uint32_t press_started_ms_{0};
};

}  // namespace cfx_cct_sweeper
}  // namespace esphome
