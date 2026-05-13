#include "cfx_power_manager.h"

#ifdef USE_ESP32

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>

namespace esphome {
namespace cfx_light {

static const char *const TAG_POWER = "cfx_power";

CFXPowerManager *CFXPowerManager::active_{nullptr};

void CFXPowerReductionSelect::control(size_t index) {
  if (this->manager_ != nullptr) {
    this->manager_->set_target_reduction_percent(static_cast<float>(index * 10),
                                                 true);
  }
}

CFXPowerManager::CFXPowerManager() { active_ = this; }

void CFXPowerManager::setup() {
  if (this->reduction_enabled_ && this->restore_reduction_ &&
      global_preferences != nullptr) {
    this->pref_ = global_preferences->make_preference<uint8_t>(
        fnv1a_hash("cfx_power_reduction"), true);
    uint8_t restored = 0;
    if (this->pref_.load(&restored)) {
      this->manual_reduction_percent_ = normalize_reduction_(restored);
      this->update_effective_reduction_();
      this->current_reduction_percent_ =
          static_cast<float>(this->target_reduction_percent_);
    }
  }
  if (this->energy_sensor_ != nullptr && global_preferences != nullptr) {
    this->energy_pref_ = global_preferences->make_preference<float>(
        fnv1a_hash("cfx_estimated_energy_kwh"), true);
    this->energy_pref_ready_ = true;
    this->energy_pref_.load(&this->energy_kwh_);
    this->energy_sensor_->publish_state(this->energy_kwh_);
  }
  this->publish_reduction_state_();
}

void CFXPowerManager::loop() {
  const uint32_t now = millis();

  if (this->reduction_enabled_ &&
      std::fabs(this->current_reduction_percent_ -
                static_cast<float>(this->target_reduction_percent_)) > 0.01f) {
    if (this->ramp_last_ms_ == 0) {
      this->ramp_last_ms_ = now;
    }
    const uint32_t dt = now - this->ramp_last_ms_;
    this->ramp_last_ms_ = now;
    const float target = static_cast<float>(this->target_reduction_percent_);
    if (this->ramp_time_ms_ == 0 || dt >= this->ramp_time_ms_) {
      this->current_reduction_percent_ = target;
    } else {
      const float max_step = 90.0f * static_cast<float>(dt) /
                             static_cast<float>(this->ramp_time_ms_);
      if (this->current_reduction_percent_ < target) {
        this->current_reduction_percent_ =
            std::min(target, this->current_reduction_percent_ + max_step);
      } else {
        this->current_reduction_percent_ =
            std::max(target, this->current_reduction_percent_ - max_step);
      }
    }
    for (auto &entry : this->outputs_) {
      if (entry.output != nullptr) {
        entry.output->schedule_show();
      }
    }
  } else {
    this->ramp_last_ms_ = 0;
  }

  if (!this->monitor_enabled_ || this->update_interval_ms_ == 0) {
    return;
  }
  if (this->last_sample_ms_ != 0 &&
      (now - this->last_sample_ms_) < this->update_interval_ms_) {
    return;
  }
  this->last_sample_ms_ = now;
  this->sample_();
}

void CFXPowerManager::configure_monitor(uint32_t update_interval_ms,
                                        float supply_voltage,
                                        float psu_current_limit_ma,
                                        float psu_efficiency,
                                        float power_factor,
                                        float mains_voltage,
                                        float controller_current_ma) {
  this->monitor_enabled_ = true;
  this->update_interval_ms_ = update_interval_ms;
  this->supply_voltage_ = supply_voltage;
  this->psu_current_limit_ma_ =
      psu_current_limit_ma > 0.0f ? psu_current_limit_ma : 0.0f;
  if (this->psu_current_limit_ma_ <= 0.0f) {
    ESP_LOGW(TAG_POWER,
             "No psu_current_limit configured; power monitor will report "
             "uncapped estimated LED demand.");
  }
  this->psu_efficiency_ = psu_efficiency > 0.0f ? psu_efficiency : 1.0f;
  this->power_factor_ = power_factor > 0.0f ? power_factor : 1.0f;
  this->mains_voltage_ = mains_voltage;
  this->controller_current_ma_ =
      controller_current_ma > 0.0f ? controller_current_ma : 0.0f;
}

void CFXPowerManager::set_meter_sensors(sensor::Sensor *mains_voltage_sensor,
                                        sensor::Sensor *power_factor_sensor) {
  this->mains_voltage_sensor_ = mains_voltage_sensor;
  this->power_factor_sensor_ = power_factor_sensor;
}

void CFXPowerManager::configure_reduction(bool restore,
                                          uint32_t ramp_time_ms) {
  this->reduction_enabled_ = true;
  this->restore_reduction_ = restore;
  this->ramp_time_ms_ = ramp_time_ms;
}

void CFXPowerManager::configure_auto_reduction(uint32_t safe_hold_ms) {
  this->auto_reduction_enabled_ = true;
  this->auto_safe_hold_ms_ = safe_hold_ms;
  if (this->auto_safe_hold_ms_ == 0) {
    this->auto_safe_hold_ms_ = this->update_interval_ms_;
  }
}

void CFXPowerManager::set_node_sensors(sensor::Sensor *dc_current,
                                       sensor::Sensor *dc_power,
                                       sensor::Sensor *ac_power,
                                       sensor::Sensor *apparent_power,
                                       sensor::Sensor *ac_current,
                                       sensor::Sensor *energy,
                                       sensor::Sensor *psu_load,
                                       text_sensor::TextSensor *budget_status) {
  this->dc_current_sensor_ = dc_current;
  this->dc_power_sensor_ = dc_power;
  this->ac_power_sensor_ = ac_power;
  this->apparent_power_sensor_ = apparent_power;
  this->ac_current_sensor_ = ac_current;
  this->energy_sensor_ = energy;
  this->psu_load_sensor_ = psu_load;
  this->budget_status_sensor_ = budget_status;
}

void CFXPowerManager::set_reduction_select(CFXPowerReductionSelect *select) {
  this->reduction_select_ = select;
  if (select != nullptr) {
    select->set_manager(this);
  }
}

void CFXPowerManager::register_output(CFXLightOutput *output, const char *name,
                                      float idle_ma, float rgb_channel_ma,
                                      float white_channel_ma) {
  CFXPowerModel model;
  model.idle_ma = idle_ma;
  model.rgb_channel_ma = rgb_channel_ma;
  model.white_channel_ma = white_channel_ma;
  this->outputs_.push_back({output, name, model});
  if (output != nullptr) {
    output->set_power_manager(this);
  }
}

void CFXPowerManager::record_output_frame(CFXLightOutput *output) {
  if (!this->monitor_enabled_ || output == nullptr) {
    return;
  }

  const float dynamic_scale =
      static_cast<float>(this->get_transmit_scale()) / 255.0f;
  for (auto &entry : this->outputs_) {
    if (entry.output != output) {
      continue;
    }
    const float current_ma =
        output->estimate_power_current_ma(entry.model, dynamic_scale);
    entry.estimated_dc_current_ma = current_ma;
    entry.accumulated_dc_current_ma += current_ma;
    entry.accumulated_frames++;
    return;
  }
}

uint8_t CFXPowerManager::get_transmit_scale() const {
  if (!this->reduction_enabled_ || this->current_reduction_percent_ <= 0.0f) {
    return 255;
  }
  float scale = 1.0f - (this->current_reduction_percent_ / 100.0f);
  if (scale < 0.0f) {
    scale = 0.0f;
  }
  if (scale > 1.0f) {
    scale = 1.0f;
  }
  return static_cast<uint8_t>(std::round(scale * 255.0f));
}

void CFXPowerManager::set_target_reduction_percent(float value, bool persist) {
  const uint8_t normalized = normalize_reduction_(value);
  this->manual_reduction_percent_ = normalized;
  this->update_effective_reduction_();
  if (!this->reduction_enabled_) {
    this->current_reduction_percent_ =
        static_cast<float>(this->target_reduction_percent_);
  }
  if (persist && this->restore_reduction_) {
    this->pref_.save(&normalized);
  }
  this->publish_reduction_state_();
  ESP_LOGI(TAG_POWER, "Power reduction target: %u%%", normalized);
}

void CFXPowerManager::sample_() {
  const float dynamic_scale =
      static_cast<float>(this->get_transmit_scale()) / 255.0f;
  float total_demand_ma = this->controller_current_ma_;
  bool outputs_idle = true;

  for (auto &entry : this->outputs_) {
    if (entry.output == nullptr) {
      continue;
    }
    if (entry.accumulated_frames > 0) {
      entry.estimated_dc_current_ma =
          entry.accumulated_dc_current_ma /
          static_cast<float>(entry.accumulated_frames);
    } else {
      entry.estimated_dc_current_ma =
          entry.output->estimate_power_current_ma(entry.model, dynamic_scale);
    }
    entry.accumulated_dc_current_ma = 0.0f;
    entry.accumulated_frames = 0;
    if (entry.estimated_dc_current_ma >
        (entry.model.idle_ma * static_cast<float>(entry.output->size()) +
         1.0f)) {
      outputs_idle = false;
    }
    total_demand_ma += entry.estimated_dc_current_ma;
  }

  const float dc_power_w =
      this->supply_voltage_ * total_demand_ma / 1000.0f;
  const float power_factor =
      live_sensor_or_(this->power_factor_sensor_, this->power_factor_, 0.01f,
                      1.0f);
  const float mains_voltage =
      live_sensor_or_(this->mains_voltage_sensor_, this->mains_voltage_, 1.0f,
                      1000.0f);
  const float ac_power_w = dc_power_w / this->psu_efficiency_;
  const float apparent_power_va = ac_power_w / power_factor;
  const float ac_current_a = apparent_power_va / mains_voltage;
  const uint32_t now_ms = millis();
  const float psu_load_percent =
      this->psu_current_limit_ma_ > 0.0f
          ? (total_demand_ma / this->psu_current_limit_ma_) * 100.0f
          : std::nanf("");

  this->apply_auto_reduction_(psu_load_percent, outputs_idle, now_ms);

  if (this->dc_current_sensor_ != nullptr) {
    this->dc_current_sensor_->publish_state(total_demand_ma / 1000.0f);
  }
  if (this->dc_power_sensor_ != nullptr) {
    this->dc_power_sensor_->publish_state(dc_power_w);
  }
  if (this->ac_power_sensor_ != nullptr) {
    this->ac_power_sensor_->publish_state(ac_power_w);
  }
  if (this->apparent_power_sensor_ != nullptr) {
    this->apparent_power_sensor_->publish_state(apparent_power_va);
  }
  if (this->ac_current_sensor_ != nullptr) {
    this->ac_current_sensor_->publish_state(ac_current_a);
  }
  if (this->energy_sensor_ != nullptr) {
    if (this->last_energy_sample_ms_ == 0) {
      this->last_energy_sample_ms_ = now_ms;
    } else {
      const uint32_t dt_ms = now_ms - this->last_energy_sample_ms_;
      this->last_energy_sample_ms_ = now_ms;
      this->energy_kwh_ +=
          ac_power_w * static_cast<float>(dt_ms) / 3600000000.0f;
    }
    this->energy_sensor_->publish_state(this->energy_kwh_);
    if (this->energy_pref_ready_ &&
        (this->last_energy_save_ms_ == 0 ||
         (now_ms - this->last_energy_save_ms_) >= 300000u)) {
      this->energy_pref_.save(&this->energy_kwh_);
      this->last_energy_save_ms_ = now_ms;
    }
  }
  if (this->psu_load_sensor_ != nullptr) {
    if (this->psu_current_limit_ma_ > 0.0f) {
      float psu_load = psu_load_percent;
      if (psu_load > 100.0f) {
        psu_load = 100.0f;
      }
      this->psu_load_sensor_->publish_state(psu_load);
    } else {
      this->psu_load_sensor_->publish_state(std::nanf(""));
    }
  }
  if (this->budget_status_sensor_ != nullptr) {
    if (this->psu_current_limit_ma_ <= 0.0f) {
      this->budget_status_sensor_->publish_state("NO_LIMIT");
    } else {
      if (psu_load_percent > 100.0f) {
        this->budget_status_sensor_->publish_state("OVERBUDGET");
      } else if (psu_load_percent >= 85.0f) {
        this->budget_status_sensor_->publish_state("WARNING");
      } else {
        this->budget_status_sensor_->publish_state("SAFE");
      }
    }
  }
}

void CFXPowerManager::apply_auto_reduction_(float psu_load_percent,
                                            bool outputs_idle,
                                            uint32_t now_ms) {
  if (!this->auto_reduction_enabled_ || !this->reduction_enabled_ ||
      this->psu_current_limit_ma_ <= 0.0f) {
    return;
  }

  if (outputs_idle) {
    this->auto_safe_since_ms_ = 0;
    this->set_auto_reduction_percent_(0);
    return;
  }

  if (!std::isfinite(psu_load_percent)) {
    this->auto_safe_since_ms_ = 0;
    return;
  }

  if (psu_load_percent > 100.0f) {
    this->auto_safe_since_ms_ = 0;
    const uint8_t next =
        this->auto_reduction_percent_ >= 50
            ? normalize_reduction_(this->auto_reduction_percent_ + 10)
            : 50;
    this->set_auto_reduction_percent_(next);
    return;
  }

  if (psu_load_percent >= 85.0f) {
    this->auto_safe_since_ms_ = 0;
    if (this->auto_reduction_percent_ < 20) {
      this->set_auto_reduction_percent_(20);
    }
    return;
  }

  if (this->auto_reduction_percent_ == 0) {
    this->auto_safe_since_ms_ = 0;
    return;
  }
  if (this->auto_safe_since_ms_ == 0) {
    this->auto_safe_since_ms_ = now_ms;
    return;
  }
  if ((now_ms - this->auto_safe_since_ms_) >= this->auto_safe_hold_ms_) {
    this->auto_safe_since_ms_ = 0;
    this->set_auto_reduction_percent_(0);
  }
}

void CFXPowerManager::set_auto_reduction_percent_(uint8_t value) {
  const uint8_t normalized = normalize_reduction_(value);
  if (normalized == this->auto_reduction_percent_) {
    return;
  }
  this->auto_reduction_percent_ = normalized;
  this->update_effective_reduction_();
  this->publish_reduction_state_();
  ESP_LOGI(TAG_POWER, "Auto power reduction: %u%%", normalized);
}

void CFXPowerManager::update_effective_reduction_() {
  this->target_reduction_percent_ =
      std::max(this->manual_reduction_percent_, this->auto_reduction_percent_);
}

void CFXPowerManager::publish_reduction_state_() {
  if (this->reduction_select_ != nullptr) {
    this->reduction_select_->publish_state(
        static_cast<size_t>(this->target_reduction_percent_ / 10));
  }
}

float CFXPowerManager::live_sensor_or_(sensor::Sensor *sensor, float fallback,
                                       float min_value, float max_value) {
  if (sensor == nullptr || !sensor->has_state()) {
    return fallback;
  }
  const float value = sensor->state;
  if (!std::isfinite(value) || value < min_value || value > max_value) {
    return fallback;
  }
  return value;
}

uint8_t CFXPowerManager::normalize_reduction_(float value) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= 90.0f) {
    return 90;
  }
  return static_cast<uint8_t>(std::round(value / 10.0f) * 10.0f);
}

}  // namespace cfx_light
}  // namespace esphome

#endif  // USE_ESP32
