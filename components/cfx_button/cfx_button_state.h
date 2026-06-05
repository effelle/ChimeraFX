/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Startup arming and edge classification for CFXButton.
 */

#pragma once

#include <cstdint>

namespace esphome {
namespace cfx_button {

enum class CFXButtonEvent : uint8_t {
  NONE,
  PRESS,
  RELEASE,
};

class CFXButtonState {
 public:
  constexpr bool is_armed() const { return this->armed_; }

  constexpr CFXButtonEvent on_state(bool pressed) {
    if (!this->armed_) {
      if (!pressed) {
        this->armed_ = true;
        this->pressed_ = false;
      }
      return CFXButtonEvent::NONE;
    }

    if (pressed == this->pressed_) {
      return CFXButtonEvent::NONE;
    }

    this->pressed_ = pressed;
    return pressed ? CFXButtonEvent::PRESS : CFXButtonEvent::RELEASE;
  }

 private:
  bool armed_{false};
  bool pressed_{false};
};

}  // namespace cfx_button
}  // namespace esphome
