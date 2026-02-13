/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Copyright (c) FastLED
 *
 * This file is part of the ChimeraFX for ESPHome.
 * Adapted from the FastLED library.
 */

#pragma once

#include "esphome/core/color.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#define FASTLED_INTERNAL

#define FASTLED_INLINE static inline __attribute__((always_inline))

// Scale8 optimization (approximate)
#define SCALE8(i, scale) (((uint16_t)i * (uint16_t)(scale)) >> 8)
#define SCALE8_VIDEO(i, scale) (((int)i * (int)scale) >> 8)

// --- Color Structures ---

struct CRGB {
  union {
    struct {
      uint8_t r;
      uint8_t g;
      uint8_t b;
    };
    uint8_t raw[3];
  };

  CRGB() : r(0), g(0), b(0) {}
  CRGB(uint8_t ir, uint8_t ig, uint8_t ib) : r(ir), g(ig), b(ib) {}
  CRGB(uint32_t colorcode)
      : r((colorcode >> 16) & 0xFF), g((colorcode >> 8) & 0xFF),
        b(colorcode & 0xFF) {}
  CRGB(esphome::Color c) : r(c.r), g(c.g), b(c.b) {}

  // Operators
  CRGB &operator+=(const CRGB &rhs) {
    r = std::min(255, (int)r + rhs.r);
    g = std::min(255, (int)g + rhs.g);
    b = std::min(255, (int)b + rhs.b);
    return *this;
  }

  uint8_t getAverageLight() const { return (r + g + b) / 3; }

  // Predefined Colors
  static const uint32_t Black = 0x000000;
  static const uint32_t White = 0xFFFFFF;
  static const uint32_t Red = 0xFF0000;
  static const uint32_t Green = 0x00FF00;
  static const uint32_t Blue = 0x0000FF;
};

struct CHSV {
  union {
    struct {
      uint8_t h;
      uint8_t s;
      uint8_t v;
    };
    uint8_t raw[3];
  };

  CHSV() : h(0), s(0), v(0) {}
  CHSV(uint8_t ih, uint8_t is, uint8_t iv) : h(ih), s(is), v(iv) {}
};

// Palette type for Pacifica
struct CRGBPalette16 {
  CRGB entries[16];

  CRGBPalette16() {
    for (int i = 0; i < 16; i++)
      entries[i] = CRGB(0, 0, 0);
  }

  CRGBPalette16(std::initializer_list<uint32_t> colors) {
    int i = 0;
    for (auto c : colors) {
      if (i >= 16)
        break;
      entries[i++] = CRGB(c);
    }
  }

  // Constructor for single color palette (used by stubs)
  CRGBPalette16(CRGB c) {
    for (int i = 0; i < 16; i++)
      entries[i] = c;
  }

  // Constructor for 4-stop HSV palette (used by Noise Pal dynamic palettes)
  // Interpolates 4 CHSV colors across 16 entries (stops at 0, 4, 8, 12)
  CRGBPalette16(CHSV c1, CHSV c2, CHSV c3, CHSV c4) {
    CHSV stops[4] = {c1, c2, c3, c4};
    for (int i = 0; i < 16; i++) {
      int segment = i / 4;    // 0-3: which pair of stops
      int pos_in_seg = i % 4; // 0-3: position within segment
      int next_seg = (segment + 1) > 3 ? 3 : (segment + 1);
      // Blend HSV components linearly, then convert to RGB
      uint8_t blend_amt = pos_in_seg * 64; // 0, 64, 128, 192
      uint8_t inv = 255 - blend_amt;
      CHSV blended;
      blended.h = ((uint16_t)stops[segment].h * inv +
                   (uint16_t)stops[next_seg].h * blend_amt) >>
                  8;
      blended.s = ((uint16_t)stops[segment].s * inv +
                   (uint16_t)stops[next_seg].s * blend_amt) >>
                  8;
      blended.v = ((uint16_t)stops[segment].v * inv +
                   (uint16_t)stops[next_seg].v * blend_amt) >>
                  8;
      CRGB rgb;
      hsv2rgb_rainbow(blended, rgb);
      entries[i] = rgb;
    }
  }

  CRGB operator[](uint8_t index) const { return entries[index & 0x0F]; }
};

// Blend modes
#define NOBLEND 0
#define LINEARBLEND 1

// --- Math Helpers (needed by ColorFromPalette) ---
FASTLED_INLINE uint8_t scale8(uint8_t i, uint8_t scale) {
  return (uint16_t(i) * uint16_t(scale)) >> 8;
}

FASTLED_INLINE CRGB blend(const CRGB &p1, const CRGB &p2, uint8_t amountOfP2) {
  uint8_t amountOfP1 = 255 - amountOfP2;
  return CRGB((p1.r * amountOfP1 + p2.r * amountOfP2) >> 8,
              (p1.g * amountOfP1 + p2.g * amountOfP2) >> 8,
              (p1.b * amountOfP1 + p2.b * amountOfP2) >> 8);
}

// ColorFromPalette: get color from palette with blending
FASTLED_INLINE CRGB ColorFromPalette(const CRGBPalette16 &pal, uint8_t index,
                                     uint8_t brightness = 255,
                                     uint8_t blendType = LINEARBLEND) {
  uint8_t hi4 = index >> 4;   // High 4 bits = palette index
  uint8_t lo4 = index & 0x0F; // Low 4 bits = blend amount

  CRGB c1 = pal[hi4];
  if (blendType == NOBLEND || lo4 == 0) {
    // No blending
    if (brightness == 255)
      return c1;
    return CRGB(scale8(c1.r, brightness), scale8(c1.g, brightness),
                scale8(c1.b, brightness));
  }

  // Linear blend
  CRGB c2 = pal[hi4 + 1];
  uint8_t blendAmount = lo4 << 4; // Scale 0-15 to 0-240
  CRGB result = blend(c1, c2, blendAmount);
  if (brightness == 255)
    return result;
  return CRGB(scale8(result.r, brightness), scale8(result.g, brightness),
              scale8(result.b, brightness));
}

// nblendPaletteTowardPalette: smoothly blend current palette toward target
// Steps each R/G/B channel by 1 per entry, up to maxChanges total per call
// WLED uses this for Noise Pal's slow palette evolution
static inline void nblendPaletteTowardPalette(CRGBPalette16 &current,
                                              CRGBPalette16 &target,
                                              uint8_t maxChanges) {
  uint8_t *p1 = (uint8_t *)current.entries;
  uint8_t *p2 = (uint8_t *)target.entries;
  const uint8_t totalChannels = 16 * 3; // 16 entries × 3 channels (RGB)

  uint8_t changes = 0;
  for (uint8_t i = 0; i < totalChannels; i++) {
    if (changes >= maxChanges)
      break;
    if (p1[i] == p2[i])
      continue;
    if (p1[i] < p2[i]) {
      p1[i]++;
      changes++;
    } else {
      p1[i]--;
      changes++;
    }
  }
}

// --- Math Helpers ---

// Lookup table declarations
extern const uint8_t sin8_data[256];
extern const uint8_t gamma8_lut[256]; // Gamma 2.2 correction LUT

// Fixed-point math macros for efficient integer arithmetic
#define FP16_MULT(a, b) (((int32_t)(a) * (int32_t)(b)) >> 16)
#define FP8_MULT(a, b) (((uint16_t)(a) * (uint16_t)(b)) >> 8)

// sin8: 0-255 input -> 0-255 sine wave output
FASTLED_INLINE uint8_t sin8(uint8_t theta) { return sin8_data[theta]; }

FASTLED_INLINE uint8_t cos8(uint8_t theta) {
  // cos is sine shifted by 90 degrees (64 in 0-255 space)
  return sin8_data[(uint8_t)(theta + 64)];
}

FASTLED_INLINE uint8_t max(uint8_t a, uint8_t b) { return (a > b) ? a : b; }
FASTLED_INLINE uint8_t min(uint8_t a, uint8_t b) { return (a < b) ? a : b; }

// --- Random Helpers (ESP-IDF compatible) ---
FASTLED_INLINE uint8_t random8() { return rand() & 0xFF; }
FASTLED_INLINE uint8_t random8(uint8_t lim) {
  if (lim == 0)
    return 0; // Safety: prevent div/0
  return rand() % lim;
}
FASTLED_INLINE uint8_t random8(uint8_t min, uint8_t lim) {
  if (min >= lim)
    return min; // Safety: prevent div/0
  return min + (rand() % (lim - min));
}
FASTLED_INLINE uint16_t random16() { return rand() & 0xFFFF; }
FASTLED_INLINE uint16_t random16(uint16_t lim) {
  if (lim == 0)
    return 0; // Safety: prevent div/0
  return rand() % lim;
}
FASTLED_INLINE uint16_t random16(uint16_t min, uint16_t lim) {
  if (min >= lim)
    return min; // Safety: prevent div/0
  return min + (rand() % (lim - min));
}

// --- Math Helpers ---
// Saturating subtraction: returns a-b, or 0 if result would be negative
FASTLED_INLINE uint8_t qsub8(uint8_t a, uint8_t b) {
  return (a > b) ? (a - b) : 0;
}

// Saturating addition: returns a+b, or 255 if result would overflow
FASTLED_INLINE uint8_t qadd8(uint8_t a, uint8_t b) {
  uint16_t sum = uint16_t(a) + uint16_t(b);
  return (sum > 255) ? 255 : uint8_t(sum);
}

// cubicwave8: attempt 8bit "triwave" with easing applied at peaks for smooth
// organic waves Creates S-curve transitions ideal for plasma effects
FASTLED_INLINE uint8_t cubicwave8(uint8_t in) {
  // First create basic triangle wave
  uint8_t triwave = (in < 128) ? (in * 2) : ((255 - in) * 2);
  // Apply cubic easing: result = 3*t^2 - 2*t^3 (scaled for 0-255)
  uint16_t t = triwave;
  uint16_t t2 = (t * t) >> 8;  // t squared, scaled
  uint16_t t3 = (t2 * t) >> 8; // t cubed, scaled
  return (uint8_t)(3 * t2 - 2 * t3);
}

// dim8_video: gamma correction that pushes mid-tones to darkness
// Creates deep "voids" in brightness - essential for Plasma contrast
// Input 128 -> Output ~64, Input 64 -> Output ~16
// Formula: (input * input) >> 8 (approximate gamma 2.0)
FASTLED_INLINE uint8_t dim8_video(uint8_t x) {
  return (uint16_t(x) * uint16_t(x)) >> 8;
}

// gamma8_fast: O(1) gamma 2.2 correction using pre-computed LUT
// Use this instead of dim8_video for more accurate gamma
FASTLED_INLINE uint8_t gamma8_fast(uint8_t x) { return gamma8_lut[x]; }

// cos8_t: cosine wave using sin8 lookup table (90 degree phase shift)
FASTLED_INLINE uint8_t cos8_t(uint8_t theta) {
  return sin8_data[(uint8_t)(theta + 64)];
}

// --- Timing Helpers (for Pacifica and other effects) ---
// sin16_t: FastLED-exact sin16_C implementation
// Piecewise linear approximation for 16-bit sine wave
// Output range: approximately -32137 to +32137 (matches FastLED)
FASTLED_INLINE int16_t sin16_t(uint16_t theta) {
  // FastLED's exact base values and slopes for piecewise linear approximation
  static const uint16_t base[] = {0,     6393,  12539, 18204,
                                  23170, 27245, 30273, 32137};
  static const uint8_t slope[] = {49, 48, 44, 38, 31, 23, 14, 4};

  uint16_t offset = (theta & 0x3FFF) >> 3; // 0..2047
  if (theta & 0x4000)
    offset = 2047 - offset;

  uint8_t section = offset / 256; // 0..7
  uint16_t b = base[section];
  uint8_t m = slope[section];

  uint8_t secoffset8 = (uint8_t)(offset) / 2;

  uint16_t mx = m * secoffset8;
  int16_t y = mx + b;

  if (theta & 0x8000)
    y = -y;

  return y;
}

// scale16: 16-bit scaling
FASTLED_INLINE uint16_t scale16(uint16_t i, uint16_t scale) {
  return (uint32_t(i) * uint32_t(scale)) >> 16;
}

// beat16: sawtooth wave 0-65535
// bpm can be simple BPM (1-255) OR Q8.8 format (256+)
// FastLED: if bpm < 256, converts to Q8.8 first, then uses beat88 with >> 16
// For bpm >= 256, assumes it's already Q8.8 format
FASTLED_INLINE uint16_t beat16(uint16_t bpm, uint32_t timebase = 0) {
  extern uint32_t get_millis();
  uint32_t ms = (timebase == 0) ? get_millis() : timebase;

  // Match FastLED behavior exactly:
  // If bpm < 256, treat as simple BPM and convert to Q8.8
  // If bpm >= 256, assume it's already Q8.8 format
  uint32_t bpm88 = (bpm < 256) ? (bpm << 8) : bpm;

  // beat88 formula: (ms * bpm88 * 280) >> 16
  return (uint16_t)(((uint32_t)ms * bpm88 * 280) >> 16);
}

// beat8: sawtooth wave 0-255
// bpm is SIMPLE BPM (1-255), NOT accum88
FASTLED_INLINE uint8_t beat8(uint16_t bpm, uint32_t timebase = 0) {
  return beat16(bpm, timebase) >> 8;
}

// beat88: sawtooth wave 0-65535 using 8.8 fixed-point BPM
// WLED passes BPM in Q8.8 format (e.g. 256 = 1 BPM)
// This effectively runs 256x slower than beat16 for the same integer input
// value.
FASTLED_INLINE uint16_t beat88_t(uint16_t bpm88, uint32_t timebase = 0) {
  extern uint32_t get_millis();
  uint32_t ms = (timebase == 0) ? get_millis() : timebase;
  // Standard beat16 formula: (ms * bpm_simple * 280) >> 8
  // Q8.8 BPM adjustment: bpm_simple = bpm88 / 256
  // Result: (ms * bpm88 * 280) >> 16
  return (uint16_t)(((uint32_t)ms * bpm88 * 280) >> 16);
}

// beatsin16: generates sine wave based on BPM
FASTLED_INLINE uint16_t beatsin16_t(uint16_t bpm, uint16_t lowest,
                                    uint16_t highest, uint32_t timebase = 0,
                                    uint16_t phase_offset = 0) {
  uint16_t beat = beat16(bpm, timebase);
  int16_t sin_val = sin16_t(beat + phase_offset);
  uint16_t range = highest - lowest;
  return lowest + scale16(sin_val + 32768, range);
}

// beatsin8: 8-bit sine based on BPM
FASTLED_INLINE uint8_t beatsin8_t(uint16_t bpm, uint8_t lowest, uint8_t highest,
                                  uint32_t timebase = 0,
                                  uint8_t phase_offset = 0) {
  uint16_t result = beatsin16_t(bpm, lowest * 256, highest * 256, timebase,
                                phase_offset * 256);
  return result >> 8;
}

// beatsin88: sine wave using 8.8 fixed-point BPM
// WLED passes BPM in Q8.8 format (e.g. 256 = 1 BPM)
// beat16 expects simple integer BPM.
// We must implement the Q8.8 calculation manually to avoid 256x speedup.
FASTLED_INLINE uint16_t beatsin88_t(uint16_t bpm88, uint16_t lowest,
                                    uint16_t highest, uint32_t timebase = 0,
                                    uint16_t phase_offset = 0) {
  extern uint32_t get_millis();
  uint32_t ms = (timebase == 0) ? get_millis() : timebase;

  // Calculate beat position for Q8.8 BPM
  // Standard beat16 formula: (ms * bpm_simple * 280) >> 8
  // With Q8.8, bpm_simple = bpm88 / 256.0
  // So: (ms * bpm88/256 * 280) >> 8  ==  (ms * bpm88 * 280) >> 16
  uint16_t beat = (uint16_t)(((uint32_t)ms * bpm88 * 280) >> 16);

  int16_t sin_val = sin16_t(beat + phase_offset);
  uint16_t range = highest - lowest;
  return lowest + scale16(sin_val + 32768, range);
}

// --- HSV to RGB Stub (Simple) ---
void hsv2rgb_rainbow(const CHSV &hsv, CRGB &rgb);

// Global Palette Lookups (Stubs)
extern const CRGBPalette16 RainbowColors_p;
extern const CRGBPalette16 OceanColors_p;
extern const CRGBPalette16 PartyColors_p;
