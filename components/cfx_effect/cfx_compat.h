/*
 * ChimeraFX - WLED Effects for ESPHome
 * Copyright (c) 2026 Federico Leoni
 * Based on WLED by Aircoookie (https://github.com/wled/WLED)
 *
 * Licensed under the EUPL-1.2
 *
 * Framework compatibility layer for Arduino and ESP-IDF.
 */

#pragma once

#include <cstdint>

#ifdef USE_ARDUINO
#include <Arduino.h>
#define CFX_ARDUINO 1
#else
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define CFX_ESP_IDF 1
#endif

inline uint32_t cfx_millis() {
#ifdef CFX_ARDUINO
  return millis();
#else
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
#endif
}

inline void cfx_delay(uint32_t ms) {
#ifdef CFX_ARDUINO
  delay(ms);
#else
  vTaskDelay(pdMS_TO_TICKS(ms));
#endif
}

inline void cfx_yield() {
#ifdef CFX_ARDUINO
  yield();
#else
  taskYIELD();
#endif
}
