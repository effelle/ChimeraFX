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
      this->target_reduction_percent_ = normalize_reduction_(restored);
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
      const float max_step = 30.0f * static_cast<float>(dt) /
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
                                        float mains_voltage) {
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
  this->mains_voltage_ = mains_voltage > 0.0f ? mains_voltage : 120.0f;
}

void CFXPowerManager::configure_reduction(bool restore,
                                          uint32_t ramp_time_ms) {
  this->reduction_enabled_ = true;
  this->restore_reduction_ = restore;
  this->ramp_time_ms_ = ramp_time_ms;
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
                                      float white_channel_ma,
                                      sensor::Sensor *strip_dc_current,
                                      sensor::Sensor *strip_dc_power) {
  CFXPowerModel model;
  model.idle_ma = idle_ma;
  model.rgb_channel_ma = rgb_channel_ma;
  model.white_channel_ma = white_channel_ma;
  this->outputs_.push_back(
      {output, name, model, strip_dc_current, strip_dc_power});
  if (output != nullptr) {
    output->set_power_manager(this);
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
  this->target_reduction_percent_ = normalized;
  if (!this->reduction_enabled_) {
    this->current_reduction_percent_ = static_cast<float>(normalized);
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
  float total_demand_ma = 0.0f;

  for (auto &entry : this->outputs_) {
    if (entry.output == nullptr) {
      continue;
    }
    entry.estimated_dc_current_ma =
        entry.output->estimate_power_current_ma(entry.model, dynamic_scale);
    total_demand_ma += entry.estimated_dc_current_ma;
  }

  for (auto &entry : this->outputs_) {
    const float current_ma = entry.estimated_dc_current_ma;
    const float dc_power_w = this->supply_voltage_ * current_ma / 1000.0f;
    if (entry.strip_dc_current != nullptr) {
      entry.strip_dc_current->publish_state(current_ma / 1000.0f);
    }
    if (entry.strip_dc_power != nullptr) {
      entry.strip_dc_power->publish_state(dc_power_w);
    }
  }

  const float dc_power_w =
      this->supply_voltage_ * total_demand_ma / 1000.0f;
  const float ac_power_w = dc_power_w / this->psu_efficiency_;
  const float apparent_power_va = ac_power_w / this->power_factor_;
  const float ac_current_a = apparent_power_va / this->mains_voltage_;
  const uint32_t now_ms = millis();

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
      float psu_load = (total_demand_ma / this->psu_current_limit_ma_) * 100.0f;
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
      this->budget_status_sensor_->publish_state("No PSU limit");
    } else {
      const float psu_load_percent =
          (total_demand_ma / this->psu_current_limit_ma_) * 100.0f;
      if (psu_load_percent >= 100.0f) {
        this->budget_status_sensor_->publish_state("Exceeds PSU model");
      } else if (psu_load_percent >= 85.0f) {
        this->budget_status_sensor_->publish_state("Near PSU limit");
      } else {
        this->budget_status_sensor_->publish_state("Comfortable");
      }
    }
  }
}

void CFXPowerManager::publish_reduction_state_() {
  if (this->reduction_select_ != nullptr) {
    this->reduction_select_->publish_state(
        static_cast<size_t>(this->target_reduction_percent_ / 10));
  }
}

uint8_t CFXPowerManager::normalize_reduction_(float value) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= 30.0f) {
    return 30;
  }
  return static_cast<uint8_t>(std::round(value / 10.0f) * 10.0f);
}

}  // namespace cfx_light
}  // namespace esphome

#endif  // USE_ESP32
