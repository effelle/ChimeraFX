#pragma once

#include "esphome/components/light/light_state.h"
#include "esphome/components/select/select.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace cfx_sequence {

class CfxSeqOnStartTrigger : public esphome::Trigger<> {};
class CfxSeqOnCompleteTrigger : public esphome::Trigger<> {};
class CfxSeqOnReachTrigger : public esphome::Trigger<float> {
public:
  explicit CfxSeqOnReachTrigger(float target_position)
      : target_position_(target_position) {}
  float get_target_position() const { return target_position_; }

protected:
  float target_position_;
};

class CfxSeqOnPixelNumTrigger : public esphome::Trigger<int32_t> {
public:
  explicit CfxSeqOnPixelNumTrigger(int32_t target_pixel)
      : target_pixel_(target_pixel) {}
  int32_t get_target_pixel() const { return target_pixel_; }

protected:
  int32_t target_pixel_;
};

class CFXSequence {
public:
  CFXSequence(const std::string &id, const std::string &name,
              const std::string &effect, bool restore = true);

  // Sequence runtime controllers
  void start();
  void stop();
  void force_reset();

  void add_light(light::LightState *state) { this->lights_.push_back(state); }

  // Custom payload bindings
  void set_speed(uint8_t speed) { this->speed_ = speed; }
  void set_intensity(uint8_t intensity) { this->intensity_ = intensity; }
  void set_palette(uint8_t palette) { this->palette_ = palette; }
  void set_iterations(uint32_t iterations) { this->iterations_ = iterations; }
  void set_brightness(float brightness) { this->brightness_ = brightness; }

  esphome::optional<uint8_t> get_speed() const { return this->speed_; }
  esphome::optional<uint8_t> get_intensity() const { return this->intensity_; }
  esphome::optional<uint8_t> get_palette() const { return this->palette_; }
  esphome::optional<float> get_brightness() const { return this->brightness_; }
  uint32_t get_iterations() const { return this->iterations_; }

  std::string get_id() const { return this->id_; }
  std::string get_name() const { return this->name_; }

  void add_on_start_trigger(CfxSeqOnStartTrigger *t) {
    this->on_start_triggers_.push_back(t);
  }
  void add_on_complete_trigger(CfxSeqOnCompleteTrigger *t) {
    this->on_complete_triggers_.push_back(t);
  }
  void add_on_reach_trigger(CfxSeqOnReachTrigger *t) {
    this->on_reach_triggers_.push_back(t);
  }
  void add_on_pixel_num_trigger(CfxSeqOnPixelNumTrigger *t) {
    this->on_pixel_num_triggers_.push_back(t);
  }

  // Called by bound effects to report tracking
  void report_event_start();
  void report_event_complete();
  void check_positional_triggers(int32_t current_pixel, int32_t total_pixels);
  void clear_active_binding();

protected:
  std::string id_;
  std::string name_;
  std::string effect_;

  esphome::optional<uint8_t> speed_;
  esphome::optional<uint8_t> intensity_;
  esphome::optional<uint8_t> palette_;
  esphome::optional<float> brightness_;
  uint32_t iterations_{0};
  bool restore_state_{true};

  std::vector<light::LightState *> lights_;

  std::vector<CfxSeqOnStartTrigger *> on_start_triggers_;
  std::vector<CfxSeqOnCompleteTrigger *> on_complete_triggers_;
  std::vector<CfxSeqOnReachTrigger *> on_reach_triggers_;
  std::vector<CfxSeqOnPixelNumTrigger *> on_pixel_num_triggers_;

  float last_triggered_percentage_{-1.0f};
  int32_t last_triggered_pixel_{-1};

  bool is_starting_{false};
  bool is_stopping_{false};
  bool is_running_{false};

  struct SavedState {
    light::LightColorValues values;
    light::ColorMode color_mode;
    std::string effect;
  };
  std::vector<SavedState> saved_states_;

public:
  bool is_starting() const { return this->is_starting_; }
  bool is_running() const { return this->is_running_; }
  static std::vector<CFXSequence *> instances;
  bool owns_light(light::LightState *state) {
    for (auto *l : this->lights_) {
      if (l == state)
        return true;
    }
    return false;
  }
};

template <typename... Ts> class StartAction : public Action<Ts...> {
public:
  StartAction(const std::string &target_id) : target_id_(target_id) {}

  void play(const Ts &...x) override {
    for (auto *seq : CFXSequence::instances) {
      if (seq->get_id() == this->target_id_) {
        seq->start();
        return;
      }
    }
  }

protected:
  std::string target_id_;
};

template <typename... Ts> class StopAction : public Action<Ts...> {
public:
  StopAction(const std::string &target_id) : target_id_(target_id) {}

  void play(const Ts &...x) override {
    for (auto *seq : CFXSequence::instances) {
      if (seq->get_id() == this->target_id_) {
        seq->stop();
        return;
      }
    }
  }

protected:
  std::string target_id_;
};

class CFXSequenceSelect : public esphome::select::Select,
                          public esphome::Component {
public:
  void setup() override;
  void control(const std::string &value) override;

  /// Safely update the UI without triggering the callback.
  void publish_state_silent(const std::string &value);

  static CFXSequenceSelect *instance;
  static bool suppress_callback_;
};

} // namespace cfx_sequence
} // namespace esphome
