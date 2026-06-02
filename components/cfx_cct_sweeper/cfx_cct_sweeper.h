#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
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
  explicit CFXCCTSweeper(const std::string &id) : id_(id) {}
  void setup() override;
  void loop() override;

  void add_light(light::LightState *state);
  void set_long_press_ms(uint32_t value) { this->long_press_ms_ = value; }
  void set_sweep_time_ms(uint32_t value) { this->sweep_time_ms_ = value; }
  void set_favorite_white(float r, float g, float b, float w) {
    this->favorite_white_ = {r, g, b, w};
  }
  void set_warm_white(float r, float g, float b, float w) {
    this->warm_white_ = {r, g, b, w};
  }
  void set_cool_white(float r, float g, float b, float w) {
    this->cool_white_ = {r, g, b, w};
  }
  void set_restore_direction(bool value) { this->restore_direction_ = value; }

  void press();
  void release();

 protected:
  struct SavedColor {
    bool valid{false};
    CFXColor color{};
  };

  struct SweepTarget {
    bool valid{false};
    CFXColor start{};
    uint32_t duration_ms{0};
  };

  static constexpr uint32_t MIN_SWEEP_TRANSITION_MS = 50;
  static constexpr uint32_t POST_SWEEP_GUARD_MS = 350;
  static constexpr float WHITE_MATCH_TOLERANCE = 0.04f;

  void start_sweep_(uint32_t now);
  void finish_sweep_();
  void freeze_sweep_();
  CFXColor sweep_color_at_(const SweepTarget &target, uint32_t now) const;
  void toggle_favorite_white_();
  void save_current_colors_();
  void restore_saved_color_(size_t index, light::LightState *state);
  void apply_color_(light::LightState *state, const CFXColor &color,
                    uint32_t transition_ms);
  void save_direction_();
  bool matches_favorite_white_(light::LightState *state) const;
  uint32_t sweep_duration_ms_(const CFXColor &start,
                              const CFXColor &target) const;
  CFXColor current_color_(light::LightState *state) const;
  CFXColor remote_color_(light::LightState *state) const;
  CFXColor sweep_target_() const;
  CFXColor clamp_color_(const CFXColor &color) const;
  float color_distance_(const CFXColor &a, const CFXColor &b) const;

  std::vector<light::LightState *> lights_;
  std::vector<SavedColor> saved_colors_;
  std::vector<SweepTarget> sweep_targets_;
  std::string id_;
  ESPPreferenceObject direction_pref_{};
  bool direction_pref_ready_{false};
  uint32_t long_press_ms_{500};
  uint32_t sweep_time_ms_{4000};
  uint32_t sweep_end_ms_{0};
  uint32_t sweep_started_ms_{0};
  uint32_t ignore_press_until_ms_{0};
  CFXColor active_sweep_target_{1.0f, 0.55f, 0.18f, 1.0f};
  CFXColor favorite_white_{1.0f, 1.0f, 1.0f, 1.0f};
  CFXColor warm_white_{1.0f, 0.55f, 0.18f, 1.0f};
  CFXColor cool_white_{0.70f, 0.85f, 1.0f, 1.0f};
  bool restore_direction_{false};
  bool next_direction_warmer_{true};
  bool pressed_{false};
  bool sweeping_{false};
  bool sweep_finished_{false};
  bool suppress_toggle_{false};
  bool sweep_direction_warmer_{true};
  uint32_t press_started_ms_{0};
};

template<typename... Ts> class PressAction : public ::esphome::Action<Ts...> {
 public:
  explicit PressAction(CFXCCTSweeper *sweeper) : sweeper_(sweeper) {}
  void play(const Ts &...x) override {
    if (this->sweeper_ != nullptr) {
      this->sweeper_->press();
    }
  }

 protected:
  CFXCCTSweeper *sweeper_;
};

template<typename... Ts> class ReleaseAction : public ::esphome::Action<Ts...> {
 public:
  explicit ReleaseAction(CFXCCTSweeper *sweeper) : sweeper_(sweeper) {}
  void play(const Ts &...x) override {
    if (this->sweeper_ != nullptr) {
      this->sweeper_->release();
    }
  }

 protected:
  CFXCCTSweeper *sweeper_;
};

}  // namespace cfx_cct_sweeper
}  // namespace esphome
