/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Binary-sensor binding wrapper for ChimeraFX button controllers.
 */

#pragma once

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

  template<typename T> void set_controller(T *controller) {
    this->press_ = [controller]() { controller->press(); };
    this->release_ = [controller]() { controller->release(); };
  }

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  void handle_state_(bool pressed);

  binary_sensor::BinarySensor *button_{nullptr};
  std::function<void()> press_;
  std::function<void()> release_;
  CFXButtonState state_;
};

}  // namespace cfx_button
}  // namespace esphome
