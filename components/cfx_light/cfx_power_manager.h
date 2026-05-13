#pragma once

#ifdef USE_ESP32

#include "cfx_light.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include <vector>

namespace esphome {
namespace cfx_light {

class CFXPowerManager;

class CFXPowerReductionSelect : public select::Select {
 public:
  void set_manager(CFXPowerManager *manager) { this->manager_ = manager; }
  void control(size_t index) override;

 protected:
  CFXPowerManager *manager_{nullptr};
};

class CFXPowerManager : public Component {
 public:
  static CFXPowerManager *active() { return active_; }

  CFXPowerManager();
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void configure_monitor(uint32_t update_interval_ms, float supply_voltage,
                         float psu_current_limit_ma, float psu_efficiency,
                         float power_factor, float mains_voltage,
                         float controller_current_ma);
  void set_meter_sensors(sensor::Sensor *mains_voltage_sensor,
                         sensor::Sensor *power_factor_sensor);
  void configure_reduction(bool restore, uint32_t ramp_time_ms);
  void configure_auto_reduction(uint32_t safe_hold_ms);
  void set_node_sensors(sensor::Sensor *dc_current, sensor::Sensor *dc_power,
                        sensor::Sensor *ac_power,
                        sensor::Sensor *apparent_power,
                        sensor::Sensor *ac_current, sensor::Sensor *energy,
                        sensor::Sensor *psu_load,
                        text_sensor::TextSensor *budget_status);
  void set_reduction_select(CFXPowerReductionSelect *select);
  void register_output(CFXLightOutput *output, const char *name, float idle_ma,
                       float rgb_channel_ma, float white_channel_ma);
  void record_output_frame(CFXLightOutput *output);

  uint8_t get_transmit_scale() const;
  float get_current_reduction_percent() const {
    return this->current_reduction_percent_;
  }
  void set_target_reduction_percent(float value, bool persist);

 protected:
  struct OutputEntry {
    CFXLightOutput *output{nullptr};
    const char *name{nullptr};
    CFXPowerModel model{};
    float estimated_dc_current_ma{0.0f};
    float accumulated_dc_current_ma{0.0f};
    uint32_t accumulated_frames{0};
  };

  void sample_();
  void check_auto_reduction_(uint32_t now_ms);
  void apply_auto_reduction_(float psu_load_percent, bool outputs_idle,
                             uint32_t now_ms);
  void set_auto_reduction_percent_(uint8_t value);
  void update_effective_reduction_();
  void publish_reduction_state_();
  static float live_sensor_or_(sensor::Sensor *sensor, float fallback,
                               float min_value, float max_value);
  static uint8_t normalize_reduction_(float value);

  static CFXPowerManager *active_;
  std::vector<OutputEntry> outputs_;
  bool monitor_enabled_{false};
  bool reduction_enabled_{false};
  bool auto_reduction_enabled_{false};
  bool restore_reduction_{true};
  uint32_t update_interval_ms_{5000};
  uint32_t ramp_time_ms_{800};
  uint32_t auto_safe_hold_ms_{30000};
  uint32_t auto_check_interval_ms_{2000};
  uint32_t last_sample_ms_{0};
  uint32_t last_auto_check_ms_{0};
  uint32_t ramp_last_ms_{0};
  uint32_t auto_safe_since_ms_{0};
  float supply_voltage_{5.0f};
  float psu_current_limit_ma_{0.0f};
  float psu_efficiency_{0.85f};
  float power_factor_{0.90f};
  float mains_voltage_{0.0f};
  float controller_current_ma_{120.0f};
  float current_reduction_percent_{0.0f};
  uint8_t manual_reduction_percent_{0};
  uint8_t auto_reduction_percent_{0};
  uint8_t auto_user_override_percent_{0};
  uint8_t auto_restore_manual_percent_{0};
  uint8_t target_reduction_percent_{0};
  bool auto_restore_pending_{false};
  ESPPreferenceObject pref_{};
  ESPPreferenceObject energy_pref_{};
  bool energy_pref_ready_{false};
  float energy_kwh_{0.0f};
  uint32_t last_energy_sample_ms_{0};
  uint32_t last_energy_save_ms_{0};
  sensor::Sensor *dc_current_sensor_{nullptr};
  sensor::Sensor *dc_power_sensor_{nullptr};
  sensor::Sensor *ac_power_sensor_{nullptr};
  sensor::Sensor *apparent_power_sensor_{nullptr};
  sensor::Sensor *ac_current_sensor_{nullptr};
  sensor::Sensor *energy_sensor_{nullptr};
  sensor::Sensor *psu_load_sensor_{nullptr};
  sensor::Sensor *mains_voltage_sensor_{nullptr};
  sensor::Sensor *power_factor_sensor_{nullptr};
  text_sensor::TextSensor *budget_status_sensor_{nullptr};
  CFXPowerReductionSelect *reduction_select_{nullptr};
};

}  // namespace cfx_light
}  // namespace esphome

#endif  // USE_ESP32
