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

  if (this->button_ == nullptr) {
    return;
  }

  if (this->button_->has_state()) {
    this->state_.on_state(this->button_->state);
  }

  this->button_->add_on_state_callback(
      [this](bool pressed) { this->inject_remote_state(pressed); });
}

void CFXButton::loop() {
  if (!this->state_.is_armed() && this->button_ != nullptr &&
      this->button_->has_state()) {
    this->handle_state_(this->button_->state);
  }
}

void CFXButton::dump_config() {
  ESP_LOGCONFIG(TAG, "CFX Button:");
  if (this->button_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Binary Sensor: %s",
                  this->button_->get_name().c_str());
  }
}

void CFXButton::inject_remote_state(bool pressed) {
  this->handle_state_(pressed);
}

void CFXButton::handle_state_(bool pressed) {
  const CFXButtonEvent event = this->state_.on_state(pressed);
  if (event == CFXButtonEvent::PRESS) {
    this->press_();
  } else if (event == CFXButtonEvent::RELEASE) {
    this->release_();
  }
}

}  // namespace cfx_button
}  // namespace esphome
