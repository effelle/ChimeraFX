/*
 * ChimeraFX - WLED Effects for ESPHome
 * Copyright (c) 2026 Federico Leoni
 * Based on WLED by Aircoookie (https://github.com/wled/WLED)
 *
 * Licensed under the EUPL-1.2
 *
 * Centralized math utilities for DRY code organization.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace cfx {

// ============================================================================
// RANDOM HELPERS
// ============================================================================

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
inline uint8_t hw_random8(uint8_t max) { return rand() % max; }

// Random 8-bit value in range [min, max)
inline uint8_t hw_random8(uint8_t min, uint8_t max) {
  if (min >= max)
    return min;
  return min + (rand() % (max - min));
}

// ============================================================================
// WAVE FUNCTIONS
// ============================================================================

// Triangle wave: 0-65535 input -> 0-65535 output
inline uint16_t triwave16(uint16_t in) {
  if (in < 0x8000) {
    return in * 2;
  } else {
    return 0xFFFF - ((in - 0x8000) * 2);
  }
}

// ============================================================================
// NOISE FUNCTIONS
// ============================================================================

// Simplified Perlin-style noise approximation
// Returns pseudo-random but smooth noise value based on x,y coordinates
inline uint8_t inoise8(uint16_t x, uint16_t y) {
  // Simple hash-based noise approximation
  uint32_t hash = x * 374761393 + y * 668265263;
  hash = (hash ^ (hash >> 13)) * 1274126177;
  hash = hash ^ (hash >> 16);
  // Smooth between neighboring values
  uint8_t base = (hash >> 8) & 0xFF;
  uint8_t next = ((hash * 7) >> 8) & 0xFF;
  uint8_t blend = (x + y) & 0xFF;
  return base + (((int16_t)(next - base) * blend) >> 8);
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

// Get random wheel index avoiding previous value (for smooth color transitions)
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

// Gamma inverse placeholder (can be extended later)
inline uint8_t gamma8inv(uint8_t v) { return v; }

// ============================================================================
// TIMING HELPERS (WLED-faithful speed scaling)
// ============================================================================

// Result struct for frame timing calculations
struct FrameTiming {
  uint32_t deltams;    // Speed-scaled delta for wave position updates
  uint32_t scaled_now; // Speed-scaled time for beat functions
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

  // WLED's deltat formula produces values that are too fast for our beat
  // functions Apply 1/8 correction to match WLED's visual speed (speed=0 should
  // match WLED speed=128)
  uint64_t deltat_raw =
      ((uint64_t)real_now >> 2) + (((uint64_t)real_now * speed) >> 7);
  uint64_t deltat = deltat_raw >> 3; // Slow down by factor of 8

  return {deltams, (uint32_t)deltat};
}

} // namespace cfx
