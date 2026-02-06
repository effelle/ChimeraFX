/*
 * ChimeraFX - WLED Effects for ESPHome
 * Copyright (c) 2026 Federico Leoni (effelle)
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

// ============================================================================
// TIMING FUNCTIONS
// ============================================================================

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

// ============================================================================
// PROGMEM COMPATIBILITY - Store constant data in Flash
// ============================================================================
// ESP-IDF: const data is already in flash, but we use rodata section for
// clarity Arduino: Uses PROGMEM attribute and pgm_read_* functions

#ifdef CFX_ARDUINO
// Arduino framework - use standard PROGMEM
#define CFX_PROGMEM PROGMEM
#define cfx_pgm_read_dword(addr) pgm_read_dword(addr)
#else
// ESP-IDF framework - const data is already in flash on ESP32
// The rodata section ensures it stays in flash
#define CFX_PROGMEM __attribute__((section(".rodata")))
// ESP32 can read flash directly without special functions
#define cfx_pgm_read_dword(addr) (*(const uint32_t *)(addr))
#endif
