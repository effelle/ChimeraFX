/*
 * ChimeraFX - WLED Effects for ESPHome
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Based on WLED by Aircoookie (https://github.com/wled/WLED)
 *
 * Licensed under the EUPL-1.2
 *
 * Centralized math utilities for DRY code organization.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "esp_heap_caps.h"
#include "esp_system.h"
#endif

#include "cfx_compat.h"
#include "esphome/core/log.h"

namespace cfx {

// ============================================================================
// ============================================================================
// RANDOM HELPERS
// ============================================================================

// Scale one byte by a second one, which is treated as the numerator of a
// fraction whose denominator is 256.
inline uint8_t scale8(uint8_t i, uint8_t scale) {
  return ((uint16_t)i * (uint16_t)scale) >> 8;
}

// Random 16-bit value (0-65535)
inline uint16_t hw_random16() { return rand() & 0xFFFF; }

// Random 16-bit value in range [min, max)
inline uint16_t hw_random16(uint16_t min, uint16_t max) {
  if (min >= max)
    return min;
  return min + (rand() % (max - min));
}

// Random 8-bit value (0-255)
inline uint8_t hw_random8() { return rand() % 256; }

// Random 8-bit value in range [0, max)
inline uint8_t hw_random8(uint8_t max) {
  if (max == 0)
    return 0; // Safety: prevent div/0
  return rand() % max;
}

// Random 8-bit value in range [min, max)
inline uint8_t hw_random8(uint8_t min, uint8_t max) {
  if (min >= max)
    return min;
  return min + (rand() % (max - min));
}
inline uint8_t sin8(uint8_t theta) {
  // Simple approximation or std::sin
  // return (sin(theta * 6.2831853f / 256.0f) + 1.0f) * 127.5f;
  // Use integer approximation for speed if needed, but float is fine on ESP32
  // We'll use a lookup-table-free robust version or just std::sin
  return (uint8_t)((sinf(theta * 0.02454369f) + 1.0f) * 127.5f);
}

typedef uint16_t accum88;

// Generates a saw wave with a given BPM
inline uint8_t beat8(accum88 beats_per_minute, uint32_t timebase = 0) {
  // BPM is usually 8.8 fixed point in FastLED, but here we might treat it as
  // simple int? WLED passes 8.8
  // (millis() * bpm * 256) / 60000
  // = (millis() * bpm) * 0.0042666...
  // = (millis() * bpm) * 280 / 65536 approx
  return ((cfx_millis() - timebase) * beats_per_minute * 280) >> 16;
}

// WLED's beatsin8_t from util.cpp
inline uint8_t beatsin8_t(accum88 beats_per_minute, uint8_t lowest = 0,
                          uint8_t highest = 255, uint32_t timebase = 0,
                          uint8_t phase_offset = 0) {
  uint8_t beat = beat8(beats_per_minute, timebase);
  uint8_t beatsin = sin8(beat + phase_offset);
  uint8_t rangewidth = highest - lowest;
  uint8_t scaledbeat = scale8(beatsin, rangewidth);
  uint8_t result = lowest + scaledbeat;
  return result;
}

// Triangle wave: 0-65535 input -> 0-65535 output
inline uint16_t triwave16(uint16_t in) {
  if (in < 0x8000) {
    return in * 2;
  } else {
    return 0xFFFF - ((in - 0x8000) * 2);
  }
}

// ============================================================================
// MATH HELPERS
// ============================================================================

// WLED map function
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Constrain helper
template <class T> const T &constrain(const T &x, const T &a, const T &b) {
  if (x < a)
    return a;
  else if (b < x)
    return b;
  else
    return x;
}

// WLED sin_gap function
// Creates a sine wave with a flat gap at the bottom (0)
inline uint8_t sin_gap(uint16_t in) {
  if (in & 0x100)
    return 0;
  return cfx::sin8(in + 192);
}

// ============================================================================
// NOISE FUNCTIONS
// ============================================================================

// WLED-faithful 2D Perlin noise (ported from util.cpp by @dedehai)
// Fixed-point integer math, optimized for speed on ESP32
// Returns 0-255 smooth noise value

#define PERLIN_SHIFT 1

// Hash grid corner to gradient direction
static inline __attribute__((always_inline)) int32_t
hashToGradient(uint32_t h) {
  return (h & 0x03) - 2; // PERLIN_SHIFT 1 → closest to FastLED
}

// 2D gradient: dot product of gradient vector with distance vector
static inline __attribute__((always_inline)) int32_t gradient2D(uint32_t x0,
                                                                int32_t dx,
                                                                uint32_t y0,
                                                                int32_t dy) {
  uint32_t h = (x0 * 0x27D4EB2D) ^ (y0 * 0xB5297A4D);
  h ^= h >> 15;
  h *= 0x92C3412B;
  h ^= h >> 13;
  return (hashToGradient(h) * dx + hashToGradient(h >> PERLIN_SHIFT) * dy) >>
         (1 + PERLIN_SHIFT);
}

// Cubic smoothstep: t*(3 - 2t²), fixed-point
static inline uint32_t perlin_smoothstep(uint32_t t) {
  uint32_t t_squared = (t * t) >> 16;
  uint32_t factor = (3 << 16) - (t << 1);
  return (t_squared * factor) >> 18;
}

// Linear interpolation for Perlin noise
static inline int32_t perlin_lerp(int32_t a, int32_t b, int32_t t) {
  return a + (((b - a) * t) >> 14);
}

// 2D Perlin noise raw (returns signed ~±20633)
inline int32_t perlin2D_raw(uint32_t x, uint32_t y) {
  int32_t x0 = x >> 16;
  int32_t y0 = y >> 16;
  int32_t x1 = (x0 + 1) & 0xFF; // wrap at 255 for 8-bit input
  int32_t y1 = (y0 + 1) & 0xFF;

  int32_t dx0 = x & 0xFFFF;
  int32_t dy0 = y & 0xFFFF;
  int32_t dx1 = dx0 - 0x10000;
  int32_t dy1 = dy0 - 0x10000;

  int32_t g00 = gradient2D(x0, dx0, y0, dy0);
  int32_t g10 = gradient2D(x1, dx1, y0, dy0);
  int32_t g01 = gradient2D(x0, dx0, y1, dy1);
  int32_t g11 = gradient2D(x1, dx1, y1, dy1);

  uint32_t tx = perlin_smoothstep(dx0);
  uint32_t ty = perlin_smoothstep(dy0);

  int32_t nx0 = perlin_lerp(g00, g10, tx);
  int32_t nx1 = perlin_lerp(g01, g11, tx);

  return perlin_lerp(nx0, nx1, ty);
}

// perlin8(x,y): WLED-compatible 2D noise, returns 0-255
inline uint8_t inoise8(uint16_t x, uint16_t y) {
  return (((perlin2D_raw((uint32_t)x << 8, (uint32_t)y << 8) * 1620) >> 10) +
          32771) >>
         8;
}

// ============================================================================
// COLOR MATH
// ============================================================================

// Blend two 32-bit RGBW colors (0=color1, 255=color2)
inline uint32_t color_blend(uint32_t color1, uint32_t color2, uint8_t blend) {
  if (blend == 0)
    return color1;
  if (blend == 255)
    return color2;

  uint8_t r1 = (color1 >> 16) & 0xFF;
  uint8_t g1 = (color1 >> 8) & 0xFF;
  uint8_t b1 = color1 & 0xFF;
  uint8_t w1 = (color1 >> 24) & 0xFF;

  uint8_t r2 = (color2 >> 16) & 0xFF;
  uint8_t g2 = (color2 >> 8) & 0xFF;
  uint8_t b2 = color2 & 0xFF;
  uint8_t w2 = (color2 >> 24) & 0xFF;

  uint8_t r3 = ((uint16_t)r1 * (255 - blend) + (uint16_t)r2 * blend) >> 8;
  uint8_t g3 = ((uint16_t)g1 * (255 - blend) + (uint16_t)g2 * blend) >> 8;
  uint8_t b3 = ((uint16_t)b1 * (255 - blend) + (uint16_t)b2 * blend) >> 8;
  uint8_t w3 = ((uint16_t)w1 * (255 - blend) + (uint16_t)w2 * blend) >> 8;

  return ((uint32_t)w3 << 24) | ((uint32_t)r3 << 16) | ((uint32_t)g3 << 8) | b3;
}

// Get random wheel index avoiding previous value (for smooth color
// transitions)
inline uint8_t get_random_wheel_index(uint8_t pos) {
  uint8_t r = 0;
  uint8_t x = 0;
  uint8_t y = 0;
  uint8_t d = 0;
  uint8_t loops = 0;
  while (d < 42 && loops < 15) {
    r = rand() % 256;
    x = abs(pos - r);
    y = 255 - x;
    d = (x < y) ? x : y;
    loops++;
  }
  if (loops >= 15)
    r = (pos + 42) % 256; // Fallback to safe shift
  return r;
}

// WLED color_wheel legacy support
// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
inline uint32_t color_wheel(uint8_t pos) {
  pos = 255 - pos;
  if (pos < 85) {
    return ((uint32_t)(255 - pos * 3) << 16) | ((uint32_t)(0) << 8) | (pos * 3);
  } else if (pos < 170) {
    pos -= 85;
    return ((uint32_t)(0) << 16) | ((uint32_t)(pos * 3) << 8) | (255 - pos * 3);
  } else {
    pos -= 170;
    return ((uint32_t)(pos * 3) << 16) | ((uint32_t)(255 - pos * 3) << 8) | (0);
  }
}

// Gamma inverse placeholder (can be extended later)
inline uint8_t gamma8inv(uint8_t v) { return v; }

// ============================================================================
// Frame Diagnostics (Runtime controllabe)

struct FrameDiagnostics {
  bool enabled = false;
  uint32_t frame_count = 0;
  uint32_t last_frame_us = 0; // Last frame timestamp in microseconds
  uint32_t min_frame_us = UINT32_MAX;
  uint32_t max_frame_us = 0;
  uint64_t total_frame_us = 0; // For average calculation
  uint32_t jitter_count = 0;   // Frames with >50% deviation from target
  uint32_t gap_count = 0;      // Frames with >50ms gap
  uint32_t last_log_time = 0;

  static constexpr uint32_t TARGET_FRAME_US = 16666; // 60fps = 16.67ms
  static constexpr uint32_t LOG_INTERVAL_MS = 2000;  // Log every 2 seconds

  void reset() {
    frame_count = 0;
    min_frame_us = UINT32_MAX;
    max_frame_us = 0;
    total_frame_us = 0;
    jitter_count = 0;
    gap_count = 0;
  }

  // Call at start of effect service - measures time since last call
  void frame_start() {
    if (!enabled)
      return;

    uint32_t now_us = cfx_micros();
    if (last_frame_us > 0) {
      uint32_t delta_us = now_us - last_frame_us;

      // Track min/max/total
      if (delta_us < min_frame_us)
        min_frame_us = delta_us;
      if (delta_us > max_frame_us)
        max_frame_us = delta_us;
      total_frame_us += delta_us;
      frame_count++;

      // Detect jitter (>50% deviation from 16.67ms target)
      if (delta_us < TARGET_FRAME_US / 2 ||
          delta_us > TARGET_FRAME_US * 3 / 2) {
        jitter_count++;
      }

      // Detect gaps (>50ms)
      if (delta_us > 50000) {
        gap_count++;
      }
    }
    last_frame_us = now_us;
  }

  // Call periodically to log statistics
  void maybe_log(const char *effect_name) {
    if (!enabled)
      return;

    uint32_t now_ms = cfx_millis();
    if (now_ms - last_log_time >= LOG_INTERVAL_MS && frame_count > 10) {
      uint32_t avg_frame_us = (uint32_t)(total_frame_us / frame_count);
      float fps =
          frame_count > 0 ? (1000000.0f * frame_count) / total_frame_us : 0;
      float jitter_pct =
          frame_count > 0 ? (100.0f * jitter_count) / frame_count : 0;

      uint32_t free_heap = 0;
      uint32_t max_block = 0;

#ifdef ARDUINO
      free_heap = ESP.getFreeHeap();
      max_block = ESP.getMaxAllocHeap();
#else
      free_heap = esp_get_free_heap_size();
      max_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif

      float avg_frame_ms = (float)avg_frame_us / 1000.0f;
      uint32_t free_heap_kb = free_heap / 1024;
      uint32_t max_block_kb = max_block / 1024;

      ESP_LOGI("cfx_diag",
               "[%s] FPS:%.1f | Time: %.1fms | Jitter: %.0f%% | Heap: %ukB "
               "Free (%ukB Max)",
               effect_name, fps, avg_frame_ms, jitter_pct, free_heap_kb,
               max_block_kb);

      reset();
      last_log_time = now_ms;
    }
  }
};

// Global diagnostics instance for each effect that needs it
// Effects can instantiate their own or use shared instance

// ============================================================================
// TIMING HELPERS (WLED-faithful speed scaling)
// ============================================================================

// Result struct for frame timing calculations
struct FrameTiming {
  uint32_t deltams;    // Speed-scaled delta for wave position updates
  uint32_t scaled_now; // Speed-scaled time for beat functions
  uint8_t wled_speed;  // WLED-scaled speed (128 ESPHome -> 83 WLED)
};

// Calculate WLED-faithful timing based on speed slider
// This implements the exact formula from WLED's mode_pacifica:
//   deltams = (FRAMETIME >> 2) + ((FRAMETIME * speed) >> 7)
//   deltat = (strip.now >> 2) + ((strip.now * speed) >> 7)
//
// Parameters:
//   speed: Current effect speed (0-255)
//   last_millis: Reference to static variable maintained by caller
//
// Returns: FrameTiming with deltams and scaled_now
inline FrameTiming calculate_frame_timing(uint8_t speed,
                                          uint32_t &last_millis) {
  uint32_t real_now = ::cfx_millis(); // Use global cfx_millis from cfx_compat.h
  uint32_t frametime = real_now - last_millis;
  if (frametime > 100)
    frametime = 16; // Clamp on first call or large gaps
  last_millis = real_now;

  uint32_t deltams = (frametime >> 2) + ((frametime * speed) >> 7);

  // WLED exact deltat formula - speed-scaled monotonic time for position/beat
  // functions. uint32_t wrapping is safe: callers use modular arithmetic
  // (& 0xFFFF, triwave16, etc.) so only lower bits matter.
  uint32_t scaled_now = (real_now >> 2) + ((real_now * (uint32_t)speed) >> 7);

  // WLED speed scaling: 128 ESPHome -> 83 WLED internal
  // Bit-shift approximation: *83/128 ≈ *83>>7, *172/127 ≈ *173>>7
  uint8_t wled_speed = (speed <= 128) ? ((speed * 83) >> 7)
                                      : (83 + (((speed - 128) * 173) >> 7));

  return {deltams, scaled_now, wled_speed};
}

} // namespace cfx
