/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Binary-sensor binding wrapper for ChimeraFX button controllers.
 */

#pragma once

#include "cfx_button_sync_command.h"
#include "cfx_cct_sweeper.h"
#include "cfx_dimmer.h"
#include "cfx_effect_selector.h"
#include "cfx_hue_cycler.h"
#include "cfx_button_state.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include <functional>
#include <vector>

namespace esphome {
namespace cfx_button {

class CFXButton : public Component {
 public:
  using SyncInputCallback =
      std::function<void(CFXButtonInputAction action, bool pressed)>;
  using SyncCommandCallback =
      std::function<void(const CFXButtonSyncCommand &command)>;

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
    this->sync_kind_ = CFXButtonSyncKind::DIMMER;
    this->dimmer_controller_ = controller;
    this->press_ = [controller]() { controller->press(); };
    this->release_ = [controller]() { controller->release(); };
    this->dimmer_press_up_ = [controller]() { controller->press_up(); };
    this->dimmer_release_up_ = [controller]() { controller->release_up(); };
    this->dimmer_press_down_ = [controller]() { controller->press_down(); };
    this->dimmer_release_down_ = [controller]() { controller->release_down(); };
    this->dimmer_min_brightness_ = controller->get_min_brightness();
    this->dimmer_max_brightness_ = controller->get_max_brightness();
    this->dimmer_ramp_time_ms_ = controller->get_ramp_time_ms();
    controller->add_sync_command_callback(
        [this](const CFXButtonSyncCommand &command) {
          this->emit_sync_command_(command);
        });
  }
  void set_controller(cfx_hue_cycler::CFXHueCycler *controller) {
    this->sync_kind_ = CFXButtonSyncKind::HUE;
    this->press_ = [controller]() { controller->press(); };
    this->release_ = [controller]() { controller->release(); };
    controller->add_sync_command_callback(
        [this](const CFXButtonSyncCommand &command) {
          this->emit_sync_command_(command);
        });
  }
  void set_controller(cfx_cct_sweeper::CFXCCTSweeper *controller) {
    this->sync_kind_ = CFXButtonSyncKind::CCT;
    this->press_ = [controller]() { controller->press(); };
    this->release_ = [controller]() { controller->release(); };
    controller->add_sync_command_callback(
        [this](const CFXButtonSyncCommand &command) {
          this->emit_sync_command_(command);
        });
  }

  void add_sync_input_callback(SyncInputCallback callback) {
    this->sync_input_callbacks_.push_back(callback);
  }
  void add_sync_command_callback(SyncCommandCallback callback) {
    this->sync_command_callbacks_.push_back(callback);
  }
  void inject_remote_state(bool pressed);
  void inject_remote_dimmer_up(bool pressed);
  void inject_remote_dimmer_down(bool pressed);

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  void handle_state_(bool pressed);
  void handle_state_(bool pressed, CFXButtonState *state);
  void handle_dimmer_up_state_(bool pressed, CFXButtonState *state);
  void handle_dimmer_down_state_(bool pressed, CFXButtonState *state);
  void handle_dimmer_up_state_(bool pressed);
  void handle_dimmer_down_state_(bool pressed);
  void emit_sync_input_(CFXButtonInputAction action, CFXButtonEvent event);
  void emit_sync_command_(CFXButtonInputAction action, CFXButtonEvent event);
  void emit_sync_command_(const CFXButtonSyncCommand &command);
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
  CFXButtonSyncKind sync_kind_{CFXButtonSyncKind::BINARY};
  float dimmer_min_brightness_{0.15f};
  float dimmer_max_brightness_{1.0f};
  uint32_t dimmer_ramp_time_ms_{2000};
  std::vector<SyncInputCallback> sync_input_callbacks_;
  std::vector<SyncCommandCallback> sync_command_callbacks_;
  CFXButtonState state_;
  CFXButtonState remote_state_;
  CFXButtonState dimmer_up_state_;
  CFXButtonState dimmer_down_state_;
  CFXButtonState remote_dimmer_up_state_;
  CFXButtonState remote_dimmer_down_state_;
  cfx_dimmer::CFXDimmer *dimmer_controller_{nullptr};
};

}  // namespace cfx_button
}  // namespace esphome
