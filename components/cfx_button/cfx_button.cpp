#include "cfx_button.h"

#include "esphome/core/log.h"

namespace esphome {
namespace cfx_button {

static const char *const TAG = "cfx_button";

static uint8_t to_u8_(float value) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= 1.0f) {
    return 255;
  }
  return static_cast<uint8_t>((value * 255.0f) + 0.5f);
}

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

void CFXButton::inject_remote_dimmer_up(bool pressed) {
  if (!this->dimmer_press_up_ || !this->dimmer_release_up_) {
    ESP_LOGW(TAG, "Remote dimmer up input ignored: controller does not "
                  "support dimmer up");
    return;
  }
  if (!this->remote_dimmer_up_state_.is_armed()) {
    this->remote_dimmer_up_state_.prime(!pressed);
  }
  ESP_LOGD(TAG, "Remote dimmer up input %s",
           pressed ? "pressed" : "released");
  this->handle_dimmer_up_state_(pressed, &this->remote_dimmer_up_state_);
}

void CFXButton::inject_remote_dimmer_down(bool pressed) {
  if (!this->dimmer_press_down_ || !this->dimmer_release_down_) {
    ESP_LOGW(TAG, "Remote dimmer down input ignored: controller does not "
                  "support dimmer down");
    return;
  }
  if (!this->remote_dimmer_down_state_.is_armed()) {
    this->remote_dimmer_down_state_.prime(!pressed);
  }
  ESP_LOGD(TAG, "Remote dimmer down input %s",
           pressed ? "pressed" : "released");
  this->handle_dimmer_down_state_(pressed, &this->remote_dimmer_down_state_);
}

void CFXButton::handle_state_(bool pressed) {
  const CFXButtonEvent event = this->state_.on_state(pressed);
  if (event == CFXButtonEvent::PRESS) {
    ESP_LOGD(TAG, "Button press");
    this->press_();
  } else if (event == CFXButtonEvent::RELEASE) {
    ESP_LOGD(TAG, "Button release");
    this->release_();
  }
  this->emit_sync_input_(CFXButtonInputAction::PRIMARY, event);
  this->emit_sync_command_(CFXButtonInputAction::PRIMARY, event);
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
  this->emit_sync_input_(CFXButtonInputAction::DIMMER_UP, event);
  this->emit_sync_command_(CFXButtonInputAction::DIMMER_UP, event);
}

void CFXButton::handle_dimmer_up_state_(bool pressed, CFXButtonState *state) {
  const CFXButtonEvent event = state->on_state(pressed);
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
  this->emit_sync_input_(CFXButtonInputAction::DIMMER_DOWN, event);
  this->emit_sync_command_(CFXButtonInputAction::DIMMER_DOWN, event);
}

void CFXButton::handle_dimmer_down_state_(bool pressed,
                                          CFXButtonState *state) {
  const CFXButtonEvent event = state->on_state(pressed);
  if (event == CFXButtonEvent::PRESS) {
    this->dimmer_press_down_();
  } else if (event == CFXButtonEvent::RELEASE) {
    this->dimmer_release_down_();
  }
}

void CFXButton::emit_sync_input_(CFXButtonInputAction action,
                                 CFXButtonEvent event) {
  if (event != CFXButtonEvent::PRESS && event != CFXButtonEvent::RELEASE) {
    return;
  }
  const bool pressed = event == CFXButtonEvent::PRESS;
  for (auto &callback : this->sync_input_callbacks_) {
    callback(action, pressed);
  }
}

void CFXButton::emit_sync_command_(CFXButtonInputAction action,
                                   CFXButtonEvent event) {
  if (event != CFXButtonEvent::PRESS && event != CFXButtonEvent::RELEASE) {
    return;
  }
  if ((this->sync_kind_ == CFXButtonSyncKind::HUE ||
       this->sync_kind_ == CFXButtonSyncKind::CCT ||
       this->sync_kind_ == CFXButtonSyncKind::EFFECT) &&
      action == CFXButtonInputAction::PRIMARY) {
    return;
  }

  const bool pressed = event == CFXButtonEvent::PRESS;
  CFXButtonSyncCommand command;
  command.kind = this->sync_kind_;
  command.action = action;
  command.pressed = pressed;

  if (this->sync_kind_ == CFXButtonSyncKind::DIMMER &&
      (action == CFXButtonInputAction::DIMMER_UP ||
       action == CFXButtonInputAction::DIMMER_DOWN)) {
    command.direction_up = action == CFXButtonInputAction::DIMMER_UP;
    command.direction_down = action == CFXButtonInputAction::DIMMER_DOWN;
    if (pressed) {
      command.has_brightness = true;
      command.brightness =
          to_u8_(action == CFXButtonInputAction::DIMMER_UP
                     ? this->dimmer_max_brightness_
                     : this->dimmer_min_brightness_);
      command.has_ramp = true;
      command.ramp_ms = this->dimmer_ramp_time_ms_;
    }
  } else if (this->sync_kind_ == CFXButtonSyncKind::BINARY &&
             action == CFXButtonInputAction::PRIMARY && pressed) {
    command.toggle = true;
  }
  this->emit_sync_command_(command);
}

void CFXButton::emit_sync_command_(const CFXButtonSyncCommand &command) {
  for (auto &callback : this->sync_command_callbacks_) {
    callback(command);
  }
}

}  // namespace cfx_button
}  // namespace esphome
