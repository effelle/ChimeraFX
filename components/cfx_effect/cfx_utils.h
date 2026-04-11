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
#include "esp_random.h"
#include "esp_system.h"
#endif

#include "cfx_compat.h"
#include "esphome/core/color.h"
#include "esphome/core/log.h"

namespace cfx {

// ============================================================================
// ============================================================================
// RANDOM HELPERS
// ============================================================================

// ============================================================================
// RANDOM HELPERS
// CFX-002 FIX: All helpers now use hardware RNG (esp_random on ESP-IDF,
// random() on Arduino framework) instead of the unseeded libc rand() which
// always starts from seed=1 at boot and produces identical sequences every
// power cycle.
// ============================================================================

namespace detail {
// Returns a 32-bit hardware-entropy random word.
// esp_random() is available in both Arduino and bare ESP-IDF builds for ESP32.
// For ESP8266 the Arduino random() call also uses the hardware PRNG.
inline uint32_t hw_rand32() {
#ifdef ARDUINO
  return (uint32_t)random();
#else
  return esp_random();
#endif
}
} // namespace detail

// Scale one byte by a second one, which is treated as the numerator of a
// fraction whose denominator is 256.
inline uint8_t scale8(uint8_t i, uint8_t scale) {
  return ((uint16_t)i * (uint16_t)scale) >> 8;
}

// Random 16-bit value (0-65535)
inline uint16_t hw_random16() {
  return (uint16_t)(detail::hw_rand32() & 0xFFFF);
}

// Random 16-bit value in range [min, max)
inline uint16_t hw_random16(uint16_t min, uint16_t max) {
  if (min >= max)
    return min;
  return min + (uint16_t)(detail::hw_rand32() % (uint32_t)(max - min));
}

// Random 8-bit value (0-255)
inline uint8_t hw_random8() { return (uint8_t)(detail::hw_rand32() & 0xFF); }

// Random 8-bit value in range [0, max)
inline uint8_t hw_random8(uint8_t max) {
  if (max == 0)
    return 0; // Safety: prevent div/0
  return (uint8_t)(detail::hw_rand32() % (uint32_t)max);
}

// Random 8-bit value in range [min, max)
inline uint8_t hw_random8(uint8_t min, uint8_t max) {
  if (min >= max)
    return min;
  return min + (uint8_t)(detail::hw_rand32() % (uint32_t)(max - min));
}
// CFX-005 FIX: sin8() now uses a 256-byte lookup table (identical to FastLED
// sin8_C). The old sinf() call cost ~40-80 cycles on ESP32; the LUT costs a
// single array access. Effects with per-pixel sin8 calls (plasma, sinelon,
// sinewave, noisepal) benefit most — several ms saved per frame on long strips.
// Resolve PROGMEM_OR_RAM — on plain ESP-IDF/Arduino builds this is just empty
#ifndef PROGMEM_OR_RAM
#define PROGMEM_OR_RAM
#endif

static const uint8_t sin8_table[256] PROGMEM_OR_RAM = {
    128, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 162, 165, 167, 170,
    173, 176, 179, 182, 185, 188, 190, 193, 196, 198, 201, 203, 206, 208, 211,
    213, 215, 218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 238, 240,
    241, 243, 244, 245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254,
    254, 255, 255, 255, 255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251,
    250, 250, 249, 248, 246, 245, 244, 243, 241, 240, 238, 237, 235, 234, 232,
    230, 228, 226, 224, 222, 220, 218, 215, 213, 211, 208, 206, 203, 201, 198,
    196, 193, 190, 188, 185, 182, 179, 176, 173, 170, 167, 165, 162, 158, 155,
    152, 149, 146, 143, 140, 137, 134, 131, 128, 124, 121, 118, 115, 112, 109,
    106, 103, 100, 97,  93,  90,  88,  85,  82,  79,  76,  73,  70,  67,  65,
    62,  59,  57,  54,  52,  49,  47,  44,  42,  40,  37,  35,  33,  31,  29,
    27,  25,  23,  21,  20,  18,  17,  15,  14,  12,  11,  10,  9,   7,   6,
    5,   5,   4,   3,   2,   2,   1,   1,   1,   0,   0,   0,   0,   0,   0,
    0,   1,   1,   1,   2,   2,   3,   4,   5,   5,   6,   7,   9,   10,  11,
    12,  14,  15,  17,  18,  20,  21,  23,  25,  27,  29,  31,  33,  35,  37,
    40,  42,  44,  47,  49,  52,  54,  57,  59,  62,  65,  67,  70,  73,  76,
    79,  82,  85,  88,  90,  93,  97,  100, 103, 106, 109, 112, 115, 118, 121,
    124};

inline uint8_t sin8(uint8_t theta) { return sin8_table[theta]; }

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

// Alias for compatibility
inline uint8_t beatsin8(accum88 beats_per_minute, uint8_t lowest = 0,
                        uint8_t highest = 255, uint32_t timebase = 0,
                        uint8_t phase_offset = 0) {
  return beatsin8_t(beats_per_minute, lowest, highest, timebase, phase_offset);
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
// CFX-006 FIX: Add safety check to prevent integer division by zero if in_max
// == in_min
inline long cfx_map(long x, long in_min, long in_max, long out_min,
                    long out_max) {
  if (in_max == in_min)
    return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Constrain helper
template <class T> const T &cfx_constrain(const T &x, const T &a, const T &b) {
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

// inoise16(x,y): 16-bit version of the Perlin noise, returns 0-65535
inline uint16_t inoise16(uint16_t x, uint16_t y) {
  return (
      uint16_t)(((perlin2D_raw((uint32_t)x << 8, (uint32_t)y << 8) * 1620) >>
                 10) +
                32771);
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
    r = hw_random8(); // CFX-002: was rand() % 256
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

/**
 * FORCE WHITE: Centralized math to shift the common RGB component to the White
 * channel. Applied BEFORE gamma correction to maintain linear physics.
 */
inline void apply_force_white(uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &w) {
  uint8_t min_rgb = std::min({r, g, b});
  if (min_rgb > 0) {
    r -= min_rgb;
    g -= min_rgb;
    b -= min_rgb;
    uint16_t new_w = (uint16_t)w + min_rgb;
    w = (new_w > 255) ? 255 : (uint8_t)new_w;
  }
}

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

  uint32_t target_frame_us =
      16666; // Default 60fps, updated from update_interval
  static constexpr uint32_t LOG_INTERVAL_MS = 2000; // Log every 2 seconds

  void set_target_interval_ms(uint32_t interval_ms) {
    target_frame_us = interval_ms * 1000;
  }

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

      // Detect jitter (>50% deviation from target interval)
      if (delta_us < target_frame_us / 2 ||
          delta_us > target_frame_us * 3 / 2) {
        jitter_count++;
      }

      // Detect gaps (>50ms)
      if (delta_us > 50000) {
        gap_count++;
      }
    }
    last_frame_us = now_us;
  }

  // Deferred logging state — set by maybe_log(), consumed by flush_log().
  // Keeps heap queries out of the effect service hot path. (CFX-033)
  bool pending_log_{false};
  const char *pending_name_{nullptr};

  // Call from the service loop hot path — zero-cost flag check only.
  // If the log interval has elapsed, sets pending_log_ for flush_log().
  void maybe_log(const char *effect_name) {
    if (!enabled)
      return;

    uint32_t now_ms = cfx_millis();
    if (now_ms - last_log_time >= LOG_INTERVAL_MS) {
      if (frame_count <= 10) {
        ESP_LOGI("chimera_fx",
                 "[%s] Debug enabled, but not enough frames: %u frames in %ums",
                 effect_name, frame_count, now_ms - last_log_time);
        last_log_time = now_ms;
        return;
      }
      pending_log_ = true;
      pending_name_ = effect_name;
    }
  }

  // Call from apply() AFTER runners finish — safely outside the DMA window.
  // Performs the expensive heap queries and full log output. (CFX-033)
  void flush_log() {
    if (!pending_log_)
      return;
    pending_log_ = false;

    uint32_t avg_frame_us = (uint32_t)(total_frame_us / frame_count);
    float fps =
        frame_count > 0 ? (1000000.0f * frame_count) / total_frame_us : 0;
    float jitter_pct =
        frame_count > 0 ? (100.0f * jitter_count) / frame_count : 0;

    uint32_t free_heap = 0;
    uint32_t max_block = 0;

#ifdef ARDUINO
    free_heap = ESP.getFreeHeap();
    // max_block omitted to prevent ISR starvation
#else
    free_heap = esp_get_free_heap_size();
    // max_block omitted to prevent ISR starvation
#endif

    float avg_frame_ms = (float)avg_frame_us / 1000.0f;
    uint32_t free_heap_kb = free_heap / 1024;

    ESP_LOGI("chimera_fx",
             "[%s] FPS:%.1f | Time: %.1fms | Jitter: %.0f%% | Heap: %ukB",
             pending_name_ ? pending_name_ : "?",
             fps, avg_frame_ms, jitter_pct, free_heap_kb);

    reset();
    last_log_time = cfx_millis();
  }

  void idle_log(const char *effect_name, uint32_t frame_count_in,
                uint32_t period_start_ms, uint64_t total_frame_us_in,
                uint32_t jitter_count_in) {
    if (!enabled) return;
    uint32_t now_ms = cfx_millis();
    if (now_ms - last_log_time < LOG_INTERVAL_MS) return;

    uint32_t free_heap = 0;
#ifdef ARDUINO
    free_heap = ESP.getFreeHeap();
#else
    free_heap = esp_get_free_heap_size();
#endif
    uint32_t free_heap_kb = free_heap / 1024;

    float fps = 0.0f;
    float avg_frame_ms = 0.0f;
    float jitter_pct = 0.0f;

    if (frame_count_in > 0 && total_frame_us_in > 0) {
      fps          = (1000000.0f * frame_count_in) / (float)total_frame_us_in;
      avg_frame_ms = (float)(total_frame_us_in / frame_count_in) / 1000.0f;
      jitter_pct   = (100.0f * jitter_count_in) / (float)frame_count_in;
    }

    ESP_LOGI("chimera_fx",
             "[%s] FPS:%.1f | Time: %.1fms | Jitter: %.0f%% | Heap: %ukB [IDLE]",
             effect_name ? effect_name : "?",
             fps, avg_frame_ms, jitter_pct, free_heap_kb);

    last_log_time = now_ms;
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

// ============================================================================
// MONOCHROMATIC EFFECT HELPERS
// ============================================================================

// Integer-only dim: factor 0 = black, 255 = full brightness
inline esphome::Color dim(esphome::Color col, uint8_t f) {
  return esphome::Color((uint8_t)(((uint16_t)col.r * f) >> 8),
               (uint8_t)(((uint16_t)col.g * f) >> 8),
               (uint8_t)(((uint16_t)col.b * f) >> 8),
               (uint8_t)(((uint16_t)col.w * f) >> 8));
}

// Additive white-flash boost
inline esphome::Color boost(esphome::Color col, uint8_t b) {
  return esphome::Color((uint8_t)((int)col.r + b > 255 ? 255 : col.r + b),
               (uint8_t)((int)col.g + b > 255 ? 255 : col.g + b),
               (uint8_t)((int)col.b + b > 255 ? 255 : col.b + b),
               (uint8_t)((int)col.w + b > 255 ? 255 : col.w + b));
}

// Cubic smoothstep approximation for ease-in-out
// Maps prog [0,1] → eased [0,1]
inline float ease_in_out(float p) {
  if (p <= 0.0f) return 0.0f;
  if (p >= 1.0f) return 1.0f;
  return p * p * (3.0f - 2.0f * p);
}

// Gamma-corrected dim: quadratic curve spends more time in the dark registers.
// Non-zero input never returns black (dim8_video equivalent).
inline esphome::Color gamma_dim(esphome::Color col, uint8_t bright) {
  uint8_t b = (bright == 0) ? 0 : (uint8_t)(((uint16_t)bright * bright) >> 8) + 1;
  return dim(col, b);
}

// Knuth multiplicative hash — deterministic pseudo-random uint32_t from any key.
// Used for stateless flash timing (Gas Discharge) and barcode pattern (Lithograph).
inline uint32_t knuth32(uint32_t key) {
  return key * 2654435761u;
}

} // namespace cfx
