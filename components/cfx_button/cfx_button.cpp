#include "cfx_button.h"

#include "esphome/core/log.h"

namespace esphome {
namespace cfx_button {

static const char *const TAG = "cfx_button";

void CFXButton::setup() {
  if (!this->press_ || !this->release_) {
    this->mark_failed();
    return;
  }
  if ((this->dimmer_up_button_ != nullptr &&
       (!this->dimmer_press_up_ || !this->dimmer_release_up_)) ||
      (this->dimmer_down_button_ != nullptr &&
       (!this->dimmer_press_down_ || !this->dimmer_release_down_))) {
    this->mark_failed();
    return;
  }

  this->bind_button_(
      this->button_, &this->state_,
      [this](bool pressed) { this->handle_state_(pressed); });
  this->bind_button_(this->dimmer_up_button_, &this->dimmer_up_state_,
                     [this](bool pressed) {
                       this->handle_dimmer_up_state_(pressed);
                     });
  this->bind_button_(this->dimmer_down_button_, &this->dimmer_down_state_,
                     [this](bool pressed) {
                       this->handle_dimmer_down_state_(pressed);
                     });
}

void CFXButton::bind_button_(binary_sensor::BinarySensor *button,
                             CFXButtonState *state,
                             std::function<void(bool)> handler) {
  if (button == nullptr) {
    return;
  }

  if (button->has_state()) {
    state->on_state(button->state);
  }

  button->add_on_state_callback(handler);
}

void CFXButton::loop() {
  if (!this->state_.is_armed() && this->button_ != nullptr &&
      this->button_->has_state()) {
    this->handle_state_(this->button_->state);
  }
  if (!this->dimmer_up_state_.is_armed() &&
      this->dimmer_up_button_ != nullptr &&
      this->dimmer_up_button_->has_state()) {
    this->handle_dimmer_up_state_(this->dimmer_up_button_->state);
  }
  if (!this->dimmer_down_state_.is_armed() &&
      this->dimmer_down_button_ != nullptr &&
      this->dimmer_down_button_->has_state()) {
    this->handle_dimmer_down_state_(this->dimmer_down_button_->state);
  }
}

void CFXButton::dump_config() {
  ESP_LOGCONFIG(TAG, "CFX Button:");
  if (this->button_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Binary Sensor: %s",
                  this->button_->get_name().c_str());
  }
  if (this->dimmer_up_button_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Dimmer Up Binary Sensor: %s",
                  this->dimmer_up_button_->get_name().c_str());
  }
  if (this->dimmer_down_button_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Dimmer Down Binary Sensor: %s",
                  this->dimmer_down_button_->get_name().c_str());
  }
}

void CFXButton::inject_remote_state(bool pressed) {
  if (!this->remote_state_.is_armed()) {
    this->remote_state_.prime(!pressed);
  }
  ESP_LOGD(TAG, "Remote input %s", pressed ? "pressed" : "released");
  this->handle_state_(pressed, &this->remote_state_);
}

void CFXButton::handle_state_(bool pressed) {
  this->handle_state_(pressed, &this->state_);
}

void CFXButton::handle_state_(bool pressed, CFXButtonState *state) {
  const CFXButtonEvent event = state->on_state(pressed);
  if (event == CFXButtonEvent::PRESS) {
    ESP_LOGD(TAG, "Button press");
    this->press_();
  } else if (event == CFXButtonEvent::RELEASE) {
    ESP_LOGD(TAG, "Button release");
    this->release_();
  }
}

void CFXButton::handle_dimmer_up_state_(bool pressed) {
  const CFXButtonEvent event = this->dimmer_up_state_.on_state(pressed);
  if (event == CFXButtonEvent::PRESS) {
    this->dimmer_press_up_();
  } else if (event == CFXButtonEvent::RELEASE) {
    this->dimmer_release_up_();
  }
}

void CFXButton::handle_dimmer_down_state_(bool pressed) {
  const CFXButtonEvent event = this->dimmer_down_state_.on_state(pressed);
  if (event == CFXButtonEvent::PRESS) {
    this->dimmer_press_down_();
  } else if (event == CFXButtonEvent::RELEASE) {
    this->dimmer_release_down_();
  }
}

}  // namespace cfx_button
}  // namespace esphome
