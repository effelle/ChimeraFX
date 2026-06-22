/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Binary-sensor binding wrapper for ChimeraFX button controllers.
 */

#pragma once

#include "cfx_cct_sweeper.h"
#include "cfx_dimmer.h"
#include "cfx_effect_selector.h"
#include "cfx_hue_cycler.h"
#include "cfx_button_state.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include <functional>

namespace esphome {
namespace cfx_button {

class CFXButton : public Component {
 public:
  void set_button(binary_sensor::BinarySensor *button) {
    this->button_ = button;
  }
  void set_dimmer_up_button(binary_sensor::BinarySensor *button) {
    this->dimmer_up_button_ = button;
  }
  void set_dimmer_down_button(binary_sensor::BinarySensor *button) {
    this->dimmer_down_button_ = button;
  }

  template<typename T> void set_controller(T *controller) {
    this->press_ = [controller]() { controller->press(); };
    this->release_ = [controller]() { controller->release(); };
  }
  void set_controller(cfx_dimmer::CFXDimmer *controller) {
    this->press_ = [controller]() { controller->press(); };
    this->release_ = [controller]() { controller->release(); };
    this->dimmer_press_up_ = [controller]() { controller->press_up(); };
    this->dimmer_release_up_ = [controller]() { controller->release_up(); };
    this->dimmer_press_down_ = [controller]() { controller->press_down(); };
    this->dimmer_release_down_ = [controller]() { controller->release_down(); };
  }

  void inject_remote_state(bool pressed);

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  void handle_state_(bool pressed);
  void handle_dimmer_up_state_(bool pressed);
  void handle_dimmer_down_state_(bool pressed);
  void bind_button_(binary_sensor::BinarySensor *button, CFXButtonState *state,
                    std::function<void(bool)> handler);

  binary_sensor::BinarySensor *button_{nullptr};
  binary_sensor::BinarySensor *dimmer_up_button_{nullptr};
  binary_sensor::BinarySensor *dimmer_down_button_{nullptr};
  std::function<void()> press_;
  std::function<void()> release_;
  std::function<void()> dimmer_press_up_;
  std::function<void()> dimmer_release_up_;
  std::function<void()> dimmer_press_down_;
  std::function<void()> dimmer_release_down_;
  CFXButtonState state_;
  CFXButtonState dimmer_up_state_;
  CFXButtonState dimmer_down_state_;
};

}  // namespace cfx_button
}  // namespace esphome
