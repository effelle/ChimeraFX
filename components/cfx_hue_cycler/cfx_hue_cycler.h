#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <string>
#include <vector>

namespace esphome {
namespace cfx_hue_cycler {

struct CFXColor {
  float red{1.0f};
  float green{1.0f};
  float blue{1.0f};
  float white{0.0f};
};

class CFXHueCycler : public Component {
 public:
  explicit CFXHueCycler(const std::string &id) : id_(id) {}
  void setup() override;
  void loop() override;

  void add_light(light::LightState *state);
  void set_long_press_ms(uint32_t value) { this->long_press_ms_ = value; }
  void set_cycle_time_ms(uint32_t value) { this->cycle_time_ms_ = value; }
  void set_white(float r, float g, float b, float w) {
    this->white_ = {r, g, b, w};
  }
  void set_saturation(float value) { this->saturation_ = value; }
  void set_restore_hue(bool value) { this->restore_hue_ = value; }

  void press();
  void release();

 protected:
  struct SavedColor {
    bool valid{false};
    CFXColor color{};
  };

  static constexpr uint32_t HUE_UPDATE_INTERVAL_MS = 120;
  static constexpr uint32_t HUE_TRANSITION_MS = 180;
  static constexpr uint32_t POST_CYCLE_GUARD_MS = 350;
  static constexpr float WHITE_MATCH_TOLERANCE = 0.04f;

  void start_cycle_(uint32_t now);
  void apply_cycle_(uint32_t now);
  CFXColor cycle_color_at_(uint32_t now);
  void freeze_cycle_();
  void toggle_white_();
  void save_current_colors_();
  void restore_saved_color_(size_t index, light::LightState *state);
  void apply_color_(light::LightState *state, const CFXColor &color,
                    uint32_t transition_ms);
  void save_hue_();
  bool matches_white_(light::LightState *state) const;
  CFXColor remote_color_(light::LightState *state) const;
  CFXColor color_from_hue_(float hue) const;
  CFXColor clamp_color_(const CFXColor &color) const;
  float remote_hue_(light::LightState *state) const;
  float normalize_hue_(float hue) const;
  float color_distance_(const CFXColor &a, const CFXColor &b) const;

  std::vector<light::LightState *> lights_;
  std::vector<SavedColor> saved_colors_;
  std::string id_;
  ESPPreferenceObject hue_pref_{};
  bool hue_pref_ready_{false};
  uint32_t long_press_ms_{500};
  uint32_t cycle_time_ms_{6000};
  uint32_t cycle_started_ms_{0};
  uint32_t last_cycle_update_ms_{0};
  uint32_t ignore_press_until_ms_{0};
  CFXColor white_{1.0f, 1.0f, 1.0f, 1.0f};
  CFXColor last_cycle_color_{0.0f, 0.62f, 1.0f, 0.0f};
  float saturation_{1.0f};
  float base_hue_{202.8f};
  float last_cycle_hue_{202.8f};
  bool restore_hue_{false};
  bool pressed_{false};
  bool cycling_{false};
  bool suppress_toggle_{false};
  uint32_t press_started_ms_{0};
};

template<typename... Ts> class PressAction : public ::esphome::Action<Ts...> {
 public:
  explicit PressAction(CFXHueCycler *cycler) : cycler_(cycler) {}
  void play(const Ts &...x) override {
    if (this->cycler_ != nullptr) {
      this->cycler_->press();
    }
  }

 protected:
  CFXHueCycler *cycler_;
};

template<typename... Ts> class ReleaseAction : public ::esphome::Action<Ts...> {
 public:
  explicit ReleaseAction(CFXHueCycler *cycler) : cycler_(cycler) {}
  void play(const Ts &...x) override {
    if (this->cycler_ != nullptr) {
      this->cycler_->release();
    }
  }

 protected:
  CFXHueCycler *cycler_;
};

}  // namespace cfx_hue_cycler
}  // namespace esphome
