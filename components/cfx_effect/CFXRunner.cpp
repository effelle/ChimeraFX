/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Copyright (c) Aircoookie (WLED)
 *
 * This file is part of the ChimeraFX for ESPHome.
 * Values and logic derived from WLED by Aircoookie.
 */

#include "CFXRunner.h"
#include "cfx_compat.h"
#include "cfx_utils.h"

// ESP-IDF heap diagnostics (for production monitoring)
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <algorithm> // For std::min, std::max
#include <cmath>     // For powf

CFXRunner *instance = nullptr;

// Forward declarations
uint16_t mode_running_lights(void);
uint16_t mode_running_dual(void);
uint16_t mode_saw(void);
uint16_t mode_blink(void);
uint16_t mode_blink_rainbow(void);
uint16_t mode_strobe(void);
uint16_t mode_strobe_rainbow(void);
uint16_t mode_multi_strobe(void);
uint16_t mode_sparkle(void);
uint16_t mode_flash_sparkle(void);
uint16_t mode_hyper_sparkle(void);

// Global time provider for FastLED timing functions
uint32_t get_millis() { return instance ? instance->now : cfx_millis(); }

// Constructor
CFXRunner::CFXRunner(esphome::light::AddressableLight *light) {
  target_light = light;
  instance = this;
  _mode = FX_MODE_STATIC;
  _name = "CFX";
  frame_time = 0;

  // Initialize Segment defaults
  _segment.start = 0;
  _segment.stop = light->size();
  _segment.mode = FX_MODE_STATIC;
  _segment.speed = DEFAULT_SPEED;
  _segment.intensity = DEFAULT_INTENSITY;
  _segment.palette = 0;
  _segment.colors[0] = DEFAULT_COLOR; // Orange default

  // Initialize Gamma Table
  // Default to 2.8 (WLED standard) so effects look correct out of the box
  _gamma = 2.8f;
  setGamma(_gamma);
}

// === Gamma Correction Helpers ===

// Re-calculate the LUT based on the new gamma value
// Goal: Output = Input^5.6 (Perceptually correct for effects)
// Input is linear 0-255.
// We want: Lut[i] = ( (i/255)^(5.6/Gamma) ) * 255
void CFXRunner::setGamma(float g) {
  if (g < 0.1f)
    g = 1.0f; // Safety
  _gamma = g;

  // The power we need to raise input by to get x^3.5 output (Compromise for
  // Aurora vs Plasma) If Gamma=3.5 -> p=1.0 If Gamma=1.0 -> p=3.5 ->
  // (x^3.5)^1.0 = x^3.5
  float power = 3.5f / _gamma;

  for (int i = 0; i < 256; i++) {
    _lut[i] = (uint8_t)(powf((float)i / 255.0f, power) * 255.0f);
  }
}

// Adjust a "floor" brightness value (e.g. Breath effect minimum)
// A raw floor of 30 is ~12% in linear space.
// If Gamma is 1.0, 30 is still 30/255 (~12%) brightness.
// If Gamma is 2.8, 30 is (30/255)^2.8 = ~0.2% brightness (too dark!)
// We must SCALE the floor up if Gamma is high, or down if Gamma is low,
// so that the *perceived* floor remains constant.
uint8_t CFXRunner::shiftFloor(uint8_t val) {
  // If gamma is standard (2.8), return original value (it's tuned for this)
  if (_gamma > 2.7f && _gamma < 2.9f)
    return val;

  // Otherwise, inversely apply the gamma difference
  // We want PerceivedFloor = (val/255)^2.8
  // NewVal = (PerceivedFloor^(1/NewGamma)) * 255
  float perceived = powf((float)val / 255.0f, 2.8f);
  return (uint8_t)(powf(perceived, 1.0f / _gamma) * 255.0f);
}

// Adjust a "Fade Factor" (Multiplicative fade, e.g. Meteor: val = val *
// factor/256) A factor of 200/255 means "retain 78% of brightness". If Gamma
// is 1.0: 78% brightness -> 78% perceived. If Gamma is 2.8: 78% val ->
// (0.78^2.8) = 50% perceived brightness! (Fades much faster) We need to adjust
// the factor so the *perceived decay rate* is constant.
uint8_t CFXRunner::getFadeFactor(uint8_t factor) {
  if (_gamma > 2.7f && _gamma < 2.9f)
    return factor;

  float retention = (float)factor / 255.0f;
  // Standard retention in linear light (Gamma 2.8)
  // Perceived retention p = r^2.8
  // We want r_new such that r_new^gamma_new = p
  // r_new = p^(1/gamma_new) = r^(2.8/gamma_new)
  float new_retention = powf(retention, 2.8f / _gamma);
  return (uint8_t)(new_retention * 255.0f);
}

// Adjust a "Subtractive Factor" (e.g. Twinkle: val = val - factor)
// Subtractive fades are tricky because they depend on the absolute value.
// We approximate by scaling the step size to match the mid-range slope.
uint8_t CFXRunner::getSubFactor(uint8_t factor) {
  if (_gamma > 2.7f && _gamma < 2.9f)
    return factor;

  // Simple heuristic: High gamma compresses low end, so subtract less.
  // Low gamma expands low end, so subtract more.
  float scale = _gamma / 2.8f;
  int new_factor = (int)((float)factor * scale);
  return (uint8_t)std::max(1, std::min(255, new_factor));
}

void Segment::setPixelColor(int n, uint32_t c) {
  if (n < 0 || n >= length())
    return;

  // Map usage to global buffer - apply mirror (inversion) if enabled
  int global_index = mirror ? (stop - 1 - n) : (start + n);

  if (instance && instance->target_light && global_index >= 0 &&
      global_index < instance->target_light->size()) {
    esphome::Color esphome_color(CFX_R(c), CFX_G(c), CFX_B(c), CFX_W(c));
    (*instance->target_light)[global_index] = esphome_color;
  }
}

uint32_t Segment::getPixelColor(int n) {
  if (n < 0 || n >= length())
    return 0;

  // Apply mirror (inversion) if enabled
  int global_index = mirror ? (stop - 1 - n) : (start + n);

  if (instance && instance->target_light && global_index >= 0 &&
      global_index < instance->target_light->size()) {
    esphome::Color c = (*instance->target_light)[global_index].get();
    return RGBW32(c.r, c.g, c.b, c.w);
  }
  return 0;
}

void Segment::fill(uint32_t c) {
  if (!instance || !instance->target_light)
    return;

  int len = length();
  int light_size = instance->target_light->size();
  int global_start = start;

  // Optimized tight loop: Resolve pointers once
  esphome::light::AddressableLight &light = *instance->target_light;
  esphome::Color esphome_color(CFX_R(c), CFX_G(c), CFX_B(c), CFX_W(c));

  for (int i = 0; i < len; i++) {
    int global_index = global_start + i;
    if (global_index < light_size) {
      light[global_index] = esphome_color;
    }
  }
}

void Segment::fadeToBlackBy(uint8_t fadeBy) {
  if (!instance || !instance->target_light)
    return;

  // GAMMA CORRECTION for Fade Speed
  // A raw "fadeBy" of 10 creates a very different decay curve at Gamma 1.0
  // vs 2.8. We adjust it so the visual decay SPEED is constant.
  uint8_t adjustedFade = instance->getFadeFactor(255 - fadeBy);
  // Invert back: We calculated retention, now we want fade amount
  // Wait, getFadeFactor takes "Retention" (0=Black, 255=Full).
  // fadeBy is "Amount to subtract" (10 = subtract 10/256).
  // Retention = 255 - fadeBy.
  // NewRetention = getFadeFactor(Retention).
  // NewFadeBy = 255 - NewRetention.

  uint8_t retention = 255 - fadeBy;
  uint8_t newRetention = instance->getFadeFactor(retention);
  uint8_t effectiveFade = 255 - newRetention;

  int len = length();
  int light_size = instance->target_light->size();
  int global_start = start;
  esphome::light::AddressableLight &light = *instance->target_light;

  for (int i = 0; i < len; i++) {
    int global_index = global_start + i;
    if (global_index < light_size) {
      // Read directly from ESPHome buffer for speed
      esphome::Color c = light[global_index].get();
      // Use standard bit math with Gamma-Corrected effectiveFade
      c.r = (c.r * (255 - effectiveFade)) >> 8;
      c.g = (c.g * (255 - effectiveFade)) >> 8;
      c.b = (c.b * (255 - effectiveFade)) >> 8;
      c.w = (c.w * (255 - effectiveFade)) >> 8;
      light[global_index] = c;
    }
  }
}

// --- Effect Implementations ---

#ifdef ESP8266
#define W_MAX_COUNT 9
#else
#define W_MAX_COUNT 20
#endif
#define W_MAX_SPEED 6
#define W_WIDTH_FACTOR 6
#define AW_SHIFT 16
#define AW_SCALE (1 << AW_SHIFT)

struct CRGBW {
  union {
    struct {
      uint8_t r;
      uint8_t g;
      uint8_t b;
      uint8_t w;
    };
    uint8_t raw[4];
  };
  CRGBW() : r(0), g(0), b(0), w(0) {}
  CRGBW(uint8_t ir, uint8_t ig, uint8_t ib, uint8_t iw = 0)
      : r(ir), g(ig), b(ib), w(iw) {}
};

// Math Helpers - use cfx:: namespace from cfx_utils.h
using cfx::color_blend;
using cfx::gamma8inv;
using cfx::get_random_wheel_index;
using cfx::hw_random16;
using cfx::hw_random8;
using cfx::inoise8;
using cfx::triwave16;

// Note: triwave16 and inoise8 now come from cfx_utils.h

// Add support for CRGBW math
static CRGBW color_add(CRGBW c1, CRGBW c2) {
  return CRGBW(std::min(255, (int)c1.r + c2.r), std::min(255, (int)c1.g + c2.g),
               std::min(255, (int)c1.b + c2.b),
               std::min(255, (int)c1.w + c2.w));
}

// Scale color by fadeAmount/256 (for fade effects)
static CRGBW color_fade(CRGBW c, uint8_t fadeAmount) {
  return CRGBW(
      ((uint16_t)c.r * fadeAmount) >> 8, ((uint16_t)c.g * fadeAmount) >> 8,
      ((uint16_t)c.b * fadeAmount) >> 8, ((uint16_t)c.w * fadeAmount) >> 8);
}

// Note: color_blend and get_random_wheel_index now come from cfx_utils.h

// ============================================================================
// PALETTE SYSTEM - Palettes stored in Flash (PROGMEM)
// Uses CFX_PROGMEM for ESP-IDF/Arduino compatibility
// ============================================================================

// Palette 0: Aurora - Green/Teal/Cyan gradient
static const uint32_t PaletteAurora[16] CFX_PROGMEM = {
    0x00FF1E, 0x00FF1E, 0x00FF1E, 0x00FF1E, // Green
    0x00FF1E, 0x00FF1E, 0x00FF1E, 0x00FF28, // Green -> slightly Tealer
    0x00FF3C, 0x00FF5A, 0x00FF82, 0x00FFB4, // Teals
    0x00FFDC, 0x32FFFF, 0x64FFFF, 0x96FFFF  // Cyan -> Light Cyan
};

// Palette 1: Forest - Earth greens and browns
static const uint32_t PaletteForest[16] CFX_PROGMEM = {
    0x003200, 0x005014, 0x006400, 0x147814, 0x009600, 0x32B41E,
    0x50C832, 0x649600, 0x967800, 0x646400, 0x32B41E, 0x009600,
    0x007814, 0x006400, 0x005014, 0x003C0A};

// Palette 2: Halloween - Orange, Purple, Lime
static const uint32_t PaletteHalloween[16] CFX_PROGMEM = {
    0x2E004F, 0x4B0082, 0x6600CC, 0x800080, // Deep Purple
    0xFF4500, 0xFF8C00, 0xFFA500, 0xFFD700, // Red-Orange to Gold
    0x32CD32, 0x00FF00, 0xADFF2F, 0x7FFF00, // Lime/Green
    0x800080, 0x6600CC, 0x4B0082, 0x2E004F  // Purple
};

// Palette 3: Rainbow - Full spectrum
static const uint32_t PaletteRainbow[16] CFX_PROGMEM = {
    0xFF0000, 0xFF5000, 0xFF9600, 0xFFFF00, 0x96FF00, 0x00FF00,
    0x00FF96, 0x00FFFF, 0x0096FF, 0x0000FF, 0x5000FF, 0x9600FF,
    0xFF00FF, 0xFF0096, 0xFF0050, 0xFF0000};

// Palette 4: Fire - Red/orange/yellow
static const uint32_t PaletteFire[16] CFX_PROGMEM = {
    0x320000, 0x640000, 0x960000, 0xC80000, 0xFF0000, 0xFF3200,
    0xFF6400, 0xFF9600, 0xFFC800, 0xFFFF00, 0xFFFF64, 0xFFC800,
    0xFF9600, 0xFF6400, 0xFF3200, 0xC80000};

// Palette 5: Sunset - Purple/red/orange/yellow
static const uint32_t PaletteSunset[16] CFX_PROGMEM = {
    0x780082, 0xB40078, 0xDC143C, 0xFF3C28, 0xFF6414, 0xFF8C00,
    0xFFB400, 0xFFDC64, 0xFFB400, 0xFF8C00, 0xFF6414, 0xFF3C28,
    0xDC143C, 0xB40078, 0x8C008C, 0x780082};

// Palette 6: Ice - Cool whites and light blues
static const uint32_t PaletteIce[16] CFX_PROGMEM = {
    0xC8F0FF, 0xB4DCFF, 0x96C8FF, 0x78B4FF, 0x64A0FF, 0x508CFF,
    0xC8F0FF, 0xDCFAFF, 0xFFFFFF, 0xDCFAFF, 0xC8F0FF, 0xB4DCFF,
    0x96C8FF, 0x78B4FF, 0xB4DCFF, 0xC8F0FF};

// Palette 7: Party - Vibrant mixed colors
static const uint32_t PaletteParty[16] CFX_PROGMEM = {
    0xFF00FF, 0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x00FFFF,
    0x0080FF, 0x8000FF, 0xFF0080, 0xFF0000, 0xFFC800, 0x00FF80,
    0x00C8FF, 0xC800FF, 0xFF00C8, 0xFF6400};

// Palette 8: Lava - Black/red/orange gradient
static const uint32_t PaletteLava[16] CFX_PROGMEM = {
    0x000000, 0x320000, 0x640000, 0x960000, 0xC80000, 0xFF1400,
    0xFF3C00, 0xFF6400, 0xFF8C00, 0xFFB400, 0xFFDC00, 0xFFFF64,
    0xFFDC00, 0xFF8C00, 0xFF3C00, 0x960000};

// Palette 9: Pastel - Soft desaturated colors
static const uint32_t PalettePastel[16] CFX_PROGMEM = {
    0xFFB4B4, 0xFFC896, 0xFFFFB4, 0xC8FFB4, 0xB4FFC8, 0xB4E6FF,
    0xC8B4FF, 0xFFB4F0, 0xFFC8C8, 0xFFE6B4, 0xE6FFB4, 0xB4FFE6,
    0xB4C8FF, 0xE6B4FF, 0xFFB4DC, 0xFFBEBE};

// Palette 10: Ocean (formerly Pacifica) - Deep ocean blues with white crests
static const uint32_t PaletteOcean[16] CFX_PROGMEM = {
    0x000212, 0x000F1E, 0x001937, 0x002850, 0x004678, 0x0064B4,
    0x148CF0, 0x28C8FF, 0x50DCFF, 0x96E6FF, 0xC8F0FF, 0xC8F0FF,
    0x96E6FF, 0x28C8FF, 0x004678, 0x000212};

// Palette 11: HeatColors - For Sunrise effect (black → red → orange → yellow →
// white)
static const uint32_t PaletteHeatColors[16] CFX_PROGMEM = {
    0x000000, 0x330000, 0x660000, 0x990000, 0xCC0000, 0xFF0000,
    0xFF3300, 0xFF6600, 0xFF9900, 0xFFCC00, 0xFFFF00, 0xFFFF33,
    0xFFFF66, 0xFFFF99, 0xFFFFCC, 0xFFFFFF};

// Palette 12: Sakura - Pink/White cherry blossom gradient
static const uint32_t PaletteSakura[16] CFX_PROGMEM = {
    0xFFC0CB, 0xFFB7C5, 0xFFADBE, 0xFFA4B8, 0xFF9AB1, 0xFF91AB,
    0xFFD1DC, 0xFFE4EC, 0xFFF5F8, 0xFFFFFF, 0xFFF5F8, 0xFFE4EC,
    0xFFD1DC, 0xFFC0CB, 0xFFADBE, 0xFFC0CB};

// Palette 13: Rivendell - Fantasy green/teal elven forest
static const uint32_t PaletteRivendell[16] CFX_PROGMEM = {
    0x003320, 0x004D30, 0x006644, 0x008060, 0x009980, 0x00B399,
    0x00CCB3, 0x33FFCC, 0x66FFDD, 0x99FFEE, 0x66FFDD, 0x33FFCC,
    0x00CCB3, 0x00B399, 0x008060, 0x006644};

// Palette 14: Cyberpunk - Neon pink/cyan
static const uint32_t PaletteCyberpunk[16] CFX_PROGMEM = {
    0xFF00FF, 0xFF33CC, 0xFF66AA, 0xFF0099, 0x00FFFF, 0x33FFFF,
    0x66FFFF, 0x00CCFF, 0x0099FF, 0x0066FF, 0xFF00FF, 0x00FFFF,
    0xFF33CC, 0x00CCFF, 0xFF00FF, 0x00FFFF};

// Palette 15: Orange & Teal - Cinematic color grading
static const uint32_t PaletteOrangeTeal[16] CFX_PROGMEM = {
    0x008B8B, 0x00A0A0, 0x00B5B5, 0x00CCCC, 0x20B2AA, 0xFF8C00,
    0xFFA500, 0xFFB347, 0xFFC87C, 0xFFD700, 0xFF8C00, 0x00CCCC,
    0x20B2AA, 0xFFA500, 0x008B8B, 0xFF8C00};

// Palette 16: Christmas - Red/green/white holiday
static const uint32_t PaletteChristmas[16] CFX_PROGMEM = {
    0xFF0000, 0xCC0000, 0x990000, 0x009900, 0x00CC00, 0x00FF00,
    0xFFFFFF, 0xFFFFCC, 0xFFFFFF, 0x00FF00, 0x00CC00, 0x009900,
    0xFF0000, 0xCC0000, 0xFFFFFF, 0xFF0000};

// Palette 17: Red & Blue - Continuous Gradient (No Voids)
// Approximates WLED's rgi_15_gp without black gaps
static const uint32_t PaletteRedBlue[16] CFX_PROGMEM = {
    0xFF0000, 0xAA0055, 0x5500AA, 0x0000FF, // Red -> Blue
    0x0000FF, 0x5500AA, 0xAA0055, 0xFF0000, // Blue -> Red
    0xFF0000, 0xAA0055, 0x5500AA, 0x0000FF, // Red -> Blue
    0x0000FF, 0x5500AA, 0xAA0055, 0xFF0000  // Blue -> Red
};

// Palette 18: Matrix - Green digital rain
static const uint32_t PaletteMatrix[16] CFX_PROGMEM = {
    0x000000, 0x001100, 0x002200, 0x003300, 0x004400, 0x006600,
    0x008800, 0x00AA00, 0x00CC00, 0x00FF00, 0x33FF33, 0x00FF00,
    0x00CC00, 0x00AA00, 0x006600, 0x003300};

// Palette 19: Sunny/Gold - Warm white/gold gradient
static const uint32_t PaletteSunnyGold[16] CFX_PROGMEM = {
    0xFFE4B5, 0xFFD39B, 0xFFC87C, 0xFFB347, 0xFFA500, 0xFF8C00,
    0xFFD700, 0xFFE135, 0xFFF68F, 0xFFFACD, 0xFFFFE0, 0xFFFACD,
    0xFFF68F, 0xFFE135, 0xFFD700, 0xFFE4B5};

// Palette 20: Fairy - Magentas, Teals, Pinks (based on fairy_reaf_gp)
static const uint32_t PaletteFairy[16] CFX_PROGMEM = {
    0xDC13BB, 0xD017C7, 0xC31BD3, 0xB71FDF, // Magenta/Pinkish
    0x8050EB, 0x4881F6, 0x11B3FF, 0x0CE1DB, // Transition to Teal
    0x3EBFE4, 0x709CED, 0xA279F6, 0xD456FF, // Teal to Light Purple
    0xCBF2DF, 0xD8F5E7, 0xE5F9EF, 0xF2FCF7  // Pale to White
};

// Palette 21: Twilight - Deep Purples, Magentas, Royal Blues (based on
// BlacK_Blue_Magenta_White_gp)
static const uint32_t PaletteTwilight[16] CFX_PROGMEM = {
    0x000000, 0x00003A, 0x000075, 0x0000B0, // Black -> Dark Blue
    0x0000EB, 0x1800F3, 0x3000FB, 0x4800FF, // Blue -> Royal Blue
    0x6600FF, 0x8400FF, 0xA200FF, 0xC000FF, // Purple
    0xFF00FF, 0xDD33FF, 0xBB66FF, 0x9999FF  // Magenta -> Lighter
};

// Palette 255: Solid Color - filled dynamically from segment.colors[0]
static uint32_t PaletteSolid[16] = {0}; // Will be filled at runtime

// Fill PaletteSolid with current color
static void fillSolidPalette(uint32_t color) {
  for (int i = 0; i < 16; i++) {
    PaletteSolid[i] = color;
  }
}

// Palette Lookup Function
// Index 0 = "Default" is special (use effect preset), handled by caller
// Actual palettes start at index 1
static const uint32_t *getPaletteByIndex(uint8_t palette_index) {
  switch (palette_index) {
  case 0: // "Default" - use effect preset (caller handles this)
    return PaletteRainbow; // Fallback if caller doesn't handle
  case 1:
    return PaletteAurora;
  case 2:
    return PaletteForest;
  case 3:
    return PaletteHalloween; // Index 3 (was Ocean) -> Halloween
  case 4:
    return PaletteRainbow;
  case 5:
    return PaletteFire;
  case 6:
    return PaletteSunset;
  case 7:
    return PaletteIce;
  case 8:
    return PaletteParty;
  case 9:
    return PaletteLava;
  case 10:
    return PalettePastel;
  case 11:
    return PaletteOcean; // Index 11 (was Pacifica) -> Ocean
  case 12:
    return PaletteHeatColors;
  case 13:
    return PaletteSakura;
  case 14:
    return PaletteRivendell;
  case 15:
    return PaletteCyberpunk;
  case 16:
    return PaletteOrangeTeal;
  case 17:
    return PaletteChristmas;
  case 18:
    return PaletteRedBlue;
  case 19:
    return PaletteMatrix;
  case 20:
    return PaletteSunnyGold;
  case 21:
    return PaletteSolid;
  case 22:
    return PaletteFairy;
  case 23:
    return PaletteTwilight;
  case 255:
    // Solid color mode - caller must call fillSolidPalette first
    // 21 = selector position, 255 = internal constant
    return PaletteSolid;
  default:
    return PaletteRainbow; // Fallback to Rainbow (most generic)
  }
}

// Simple Linear Interpolation Palette Lookup (Updated for dynamic palettes)
// Uses cfx_pgm_read_dword for PROGMEM/Flash compatibility
static CRGBW ColorFromPalette(uint8_t index, uint8_t brightness,
                              const uint32_t *palette) {
  uint8_t i = index >> 4;   // 0-15
  uint8_t f = index & 0x0F; // Fraction 0-15

  // Wrap around - read from Flash using compatibility macro
  uint32_t c1 = cfx_pgm_read_dword(&palette[i]);
  uint32_t c2 = cfx_pgm_read_dword(&palette[(i + 1) & 15]);

  // Lerp RGB
  uint8_t r1 = (c1 >> 16) & 0xFF;
  uint8_t g1 = (c1 >> 8) & 0xFF;
  uint8_t b1 = c1 & 0xFF;
  uint8_t r2 = (c2 >> 16) & 0xFF;
  uint8_t g2 = (c2 >> 8) & 0xFF;
  uint8_t b2 = c2 & 0xFF;

  // Safety: cast to int to avoid signed truncation when r2 < r1
  uint8_t r = (uint8_t)std::max(0, (int)r1 + ((((int)r2 - (int)r1) * f) >> 4));
  uint8_t g = (uint8_t)std::max(0, (int)g1 + ((((int)g2 - (int)g1) * f) >> 4));
  uint8_t b = (uint8_t)std::max(0, (int)b1 + ((((int)b2 - (int)b1) * f) >> 4));

  // Apply brightness
  r = (r * brightness) >> 8;
  g = (g * brightness) >> 8;
  b = (b * brightness) >> 8;

  return CRGBW(r, g, b, 0); // White channel unused for simple palette
}

// AuroraWave Struct (POD version)
struct AuroraWave {
  int32_t center;
  uint32_t ageFactor_cached;
  uint16_t ttl;
  uint16_t age;
  uint16_t width;
  uint16_t basealpha;
  uint8_t
      speed_factor_byte; // Original WLED uses byte for random factor (10-31)
  int16_t wave_start;
  int16_t wave_end;
  bool goingleft;
  bool alive; // No default init, must be POD
  CRGBW basecolor;

  void init(uint32_t segment_length, CRGBW color) {
    ttl = hw_random16(500, 1501);
    basecolor = color;
    basealpha = hw_random8(50, 100) * AW_SCALE / 100;
    age = 0;
    width =
        hw_random16(segment_length / 20, segment_length / W_WIDTH_FACTOR) + 1;
    center = (((uint32_t)hw_random8(101) << AW_SHIFT) / 100) * segment_length;
    goingleft = hw_random8() & 0x01;

    speed_factor_byte = hw_random8(10, 31);
    alive = true;
  }

  void updateCachedValues() {
    // Safety: Prevent Div/0 if ttl is garbage small
    if (ttl < 2)
      return;

    uint32_t half_ttl = ttl >> 1;
    if (age < half_ttl) {
      ageFactor_cached = ((uint32_t)age << AW_SHIFT) / half_ttl;
    } else {
      ageFactor_cached = ((uint32_t)(ttl - age) << AW_SHIFT) / half_ttl;
    }
    if (ageFactor_cached >= AW_SCALE)
      ageFactor_cached = AW_SCALE - 1;

    uint32_t center_led = center >> AW_SHIFT;
    wave_start = (int16_t)center_led - (int16_t)width;
    wave_end = (int16_t)center_led + (int16_t)width;
  }

  CRGBW getColorForLED(int ledIndex) {
    if (ledIndex < wave_start || ledIndex > wave_end)
      return CRGBW(0, 0, 0, 0);
    int32_t ledIndex_scaled = (int32_t)ledIndex << AW_SHIFT;
    int32_t offset = ledIndex_scaled - center;
    if (offset < 0)
      offset = -offset;

    // Safety Check: Prevent Div/0
    if (width == 0)
      return CRGBW(0, 0, 0, 0);

    uint32_t offsetFactor = offset / width;
    if (offsetFactor > AW_SCALE)
      return CRGBW(0, 0, 0, 0);

    uint32_t brightness_factor = (AW_SCALE - offsetFactor);
    brightness_factor = (brightness_factor * ageFactor_cached) >> AW_SHIFT;
    brightness_factor = (brightness_factor * basealpha) >> AW_SHIFT;

    CRGBW rgb;
    rgb.r = (basecolor.r * brightness_factor) >> AW_SHIFT;
    rgb.g = (basecolor.g * brightness_factor) >> AW_SHIFT;
    rgb.b = (basecolor.b * brightness_factor) >> AW_SHIFT;
    rgb.w = (basecolor.w * brightness_factor) >> AW_SHIFT;
    return rgb;
  }

  void update(uint32_t segment_length, uint32_t input_speed) {
    // FIX: Scale speed by ~0.66 (170/256) to match WLED 42fps vs ESPHome 60fps
    // 128 input -> ~85 effective
    uint32_t effective_speed = (input_speed * 170) >> 8;

    uint32_t step = (uint32_t)speed_factor_byte * W_MAX_SPEED * effective_speed;
    step = (step << AW_SHIFT) / (100 * 255 * 4);

    center += goingleft ? -step : step;
    age++;

    if (age > ttl) {
      alive = false;
    } else {
      uint32_t width_scaled = (uint32_t)width << AW_SHIFT;
      uint32_t segment_length_scaled = segment_length << AW_SHIFT;

      if (goingleft) {
        if (center < -(int32_t)width_scaled)
          alive = false;
      } else {
        if (center > (int32_t)segment_length_scaled + (int32_t)width_scaled)
          alive = false;
      }
    }
  }

  bool stillAlive() { return alive; }
};

// --- Effect Implementations ---

uint16_t mode_static(void) {
  if (!instance)
    return FRAMETIME;

  if (instance->_segment.palette != 255 && instance->_segment.palette != 0) {
    // Palette Active: Render Gradient explicitly for "Static" Effect (ID 0)
    // This allows users to display a stationary palette pattern.
    uint16_t len = instance->_segment.length();
    const uint32_t *active_palette =
        getPaletteByIndex(instance->_segment.palette);

    for (int i = 0; i < len; i++) {
      uint8_t colorIndex = (i * 255) / (len > 1 ? len - 1 : 1);
      CRGBW c = ColorFromPalette(colorIndex, 255, active_palette);
      instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
    }
  } else {
    // Solid Color Mode (Palette 255)
    // Use the color set by the light state (via setPixelColor/setColor)
    // Fill the segment with the primary color
    instance->_segment.fill(instance->_segment.colors[0]);
  }
  return FRAMETIME; // Refresh rate
}

uint16_t mode_aurora(void) {
  // === FRAME DIAGNOSTICS (enabled with CFX_FRAME_DIAGNOSTICS) ===
  static cfx::FrameDiagnostics aurora_diag;
  aurora_diag.frame_start();
  aurora_diag.maybe_log("Aurora");

  AuroraWave *waves;

  // === WLED-FAITHFUL TIMING using centralized helper ===
  // Use segment.step as per-instance timing state (avoids static variable
  // cross-contamination)
  auto timing = cfx::calculate_frame_timing(instance->_segment.speed,
                                            instance->_segment.step);
  // Use deltams to determine effective speed for updates
  uint32_t effective_speed = timing.deltams > 0 ? instance->_segment.speed : 0;

  // Intensity Scaling Boost: 128 selector -> 175 internal
  uint8_t selector = instance->_segment.intensity;
  uint8_t internal_intensity;
  if (selector <= 128) {
    internal_intensity = (uint32_t)selector * 175 / 128;
  } else {
    internal_intensity = 175 + ((uint32_t)(selector - 128) * 80 / 127);
  }

  // Active Count: Depends on boosted intensity, but we always alloc Max
  int active_count = 2 + ((internal_intensity * (W_MAX_COUNT - 2)) / 255);
  instance->_segment.aux1 = active_count;

  if (!instance->_segment.allocateData(sizeof(AuroraWave) * W_MAX_COUNT)) {
    return mode_static();
  }

  // Handle Reset / Garbage Memory
  if (instance->_segment.reset) {
    memset(instance->_segment.data, 0, instance->_segment._dataLen);
    instance->_segment.reset = false;
  }

  waves = reinterpret_cast<AuroraWave *>(instance->_segment.data);

  // Service Waves - Loop through ALL potential waves for smooth transitions
  for (int i = 0; i < W_MAX_COUNT; i++) {
    // Safety: If ttl is 0, it's definitely uninitialized/dead data
    if (waves[i].ttl == 0) {
      waves[i].alive = false;
    }

    if (waves[i].alive) {
      // Accelerated Fade Out for excess waves to achieve requested ~250-500ms
      // feel
      if (i >= active_count) {
        waves[i].basealpha =
            (waves[i].basealpha * 224) >> 8; // Fade out ~12% per frame
        if (waves[i].basealpha < 10)
          waves[i].alive = false;
      }

      waves[i].update(instance->_segment.length(), effective_speed);

      if (!waves[i].stillAlive()) {
        // Wave died naturally. Only re-init if we are below the active
        // threshold
        if (i < active_count) {
          uint8_t colorIndex = rand() % 256;
          const uint32_t *active_palette =
              getPaletteByIndex(instance->_segment.palette);
          CRGBW color = ColorFromPalette(colorIndex, 255, active_palette);
          waves[i].init(instance->_segment.length(), color);
        }
      }
    } else {
      // Dead slot. If it's within the active_count, start a new wave
      if (i < active_count) {
        uint8_t colorIndex = rand() % 256;
        const uint32_t *active_palette =
            getPaletteByIndex(instance->_segment.palette);
        CRGBW color = ColorFromPalette(colorIndex, 255, active_palette);
        waves[i].init(instance->_segment.length(), color);
      }
    }

    if (waves[i].alive) {
      waves[i].updateCachedValues();
    }
  }

  // Render - All alive waves contribute
  CRGBW background(0, 0, 0, 0);

  for (int i = 0; i < instance->_segment.length(); i++) {
    CRGBW mixedRgb = background;
    for (int j = 0; j < W_MAX_COUNT; j++) {
      if (waves[j].alive) {
        CRGBW rgb = waves[j].getColorForLED(i);
        mixedRgb = color_add(mixedRgb, rgb);
      }
    }

    // GAMMA CORRECTION: Apply gamma to final linear output to simulate "deep
    // black" contrast which was previously provided by the driver's gamma
    // correction.
    instance->_segment.setPixelColor(i,
                                     RGBW32(instance->applyGamma(mixedRgb.r),
                                            instance->applyGamma(mixedRgb.g),
                                            instance->applyGamma(mixedRgb.b),
                                            instance->applyGamma(mixedRgb.w)));
  }

  return FRAMETIME;
}

// --- Fire2012 Effect ---
// Exact WLED implementation by Mark Kriegsman
// Adapted for ESPHome framework
// VIRTUAL RESOLUTION UPDATE:
// Simulates fire on a fixed 60-pixel grid to ensure identical behavior on any
// strip length, then scales output to actual length.
uint16_t mode_fire_2012(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // 1. Define Virtual Grid
  const int VIRTUAL_HEIGHT = 60;

  // Allocate heat array for Virtual Grid
  if (!instance->_segment.allocateData(VIRTUAL_HEIGHT))
    return mode_static();
  uint8_t *heat = instance->_segment.data;

  // WLED-FAITHFUL TIMING using centralized helper
  // NOTE: Static var is acceptable for single-strip. segment.aux0 is
  // used by the fire iteration logic below, so it cannot be repurposed.
  static uint32_t fire_last_millis = 0;
  auto timing =
      cfx::calculate_frame_timing(instance->_segment.speed, fire_last_millis);

  const uint32_t it = timing.scaled_now >> 5; // div 32

  const uint8_t ignition = max(3, VIRTUAL_HEIGHT / 10);

  for (int i = 0; i < VIRTUAL_HEIGHT; i++) {
    uint8_t cool =
        (it != instance->_segment.step)
            ? random8((((20 + timing.wled_speed / 3) * 16) / VIRTUAL_HEIGHT) +
                      2)
            : random8(4);
    uint8_t minTemp = (i < ignition) ? (ignition - i) / 4 + 16
                                     : 0; // don't black out ignition area
    uint8_t temp = qsub8(heat[i], cool);
    heat[i] = (temp < minTemp) ? minTemp : temp;
  }

  if (it != instance->_segment.step) {
    for (int k = VIRTUAL_HEIGHT - 1; k > 1; k--) {
      heat[k] = (heat[k - 1] + (heat[k - 2] << 1)) / 3; // heat[k-2] * 2
    }

    if (random8() <= instance->_segment.intensity) {
      uint8_t y = random8(ignition);
      uint8_t boost = 17 * (ignition - y / 2) / ignition; // WLED default boost
      heat[y] = qadd8(heat[y], random8(96 + 2 * boost, 207 + boost));
    }
  }

  float scale = (float)VIRTUAL_HEIGHT / (float)len;

  for (int j = 0; j < len; j++) {
    int v_index = (int)(j * scale);
    if (v_index >= VIRTUAL_HEIGHT)
      v_index = VIRTUAL_HEIGHT - 1;

    uint8_t t = min(heat[v_index], (uint8_t)240);
    uint8_t r, g, b;
    // Heat colors: black -> red -> orange -> yellow -> white
    if (t <= 85) {
      r = t * 3;
      g = 0;
      b = 0;
    } else if (t <= 170) {
      r = 255;
      g = (t - 85) * 3;
      b = 0;
    } else {
      r = 255;
      g = 255;
      b = (t - 170) * 3;
    }
    instance->_segment.setPixelColor(j, RGBW32(r, g, b, 0));
  }

  if (it != instance->_segment.step)
    instance->_segment.step = it;

  return FRAMETIME;
}

uint16_t mode_fire_dual(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // 1. Define Virtual Grid
  const int VIRTUAL_HEIGHT = 60;

  // Allocate heat array for Virtual Grid
  if (!instance->_segment.allocateData(VIRTUAL_HEIGHT))
    return mode_static();
  uint8_t *heat = instance->_segment.data;

  // === WLED-FAITHFUL TIMING using centralized helper ===
  static uint32_t fire_last_millis = 0;
  auto timing =
      cfx::calculate_frame_timing(instance->_segment.speed, fire_last_millis);

  const uint32_t it = timing.scaled_now >> 5; // div 32
  const uint8_t ignition = max(3, VIRTUAL_HEIGHT / 10);

  // --- Step 1-3: Run Simulation on Virtual Grid (Same as Fire 2012) ---
  for (int i = 0; i < VIRTUAL_HEIGHT; i++) {
    uint8_t cool =
        (it != instance->_segment.step)
            ? random8((((20 + timing.wled_speed / 3) * 16) / VIRTUAL_HEIGHT) +
                      2)
            : random8(4);
    uint8_t minTemp = (i < ignition) ? (ignition - i) / 4 + 16 : 0;
    uint8_t temp = qsub8(heat[i], cool);
    heat[i] = (temp < minTemp) ? minTemp : temp;
  }

  if (it != instance->_segment.step) {
    for (int k = VIRTUAL_HEIGHT - 1; k > 1; k--) {
      heat[k] = (heat[k - 1] + (heat[k - 2] << 1)) / 3;
    }
    if (random8() <= instance->_segment.intensity) {
      uint8_t y = random8(ignition);
      uint8_t boost = 17 * (ignition - y / 2) / ignition;
      heat[y] = qadd8(heat[y], random8(96 + 2 * boost, 207 + boost));
    }
    instance->_segment.step = it;
  }

  // --- Step 4: Map Virtual Heat to Physical LEDs ---
  // Check for mirror mode: flames start from center toward edges
  bool mirror_mode = instance->_segment.mirror;

  // Vacuum: 2 pixels in center where no fire exists (only in normal mode)
  // In mirror mode, vacuum is at the edges
  int vacuum = 2;
  int half_len = (len - vacuum) / 2;
  if (half_len < 1)
    half_len = 1; // Safety for very short strips

  // Scale factor: Virtual -> Physical Half
  // "Zoom": Map only the bottom 48 pixels of the 60px virtual fire to the strip
  // This crops the top ~20% (mostly smoke/black) to ensure flames meet in the
  // middle Fixes "vacuum too big" on long strips
  float scale = (float)(VIRTUAL_HEIGHT - 12) / (float)half_len;

  // Helper lambda for heat-to-color conversion
  auto heat_to_rgb = [&heat](int v_index, uint8_t &r, uint8_t &g, uint8_t &b) {
    uint8_t t = min(heat[v_index], (uint8_t)240);
    if (t <= 85) {
      r = t * 3;
      g = 0;
      b = 0;
    } else if (t <= 170) {
      r = 255;
      g = (t - 85) * 3;
      b = 0;
    } else {
      r = 255;
      g = 255;
      b = (t - 170) * 3;
    }
  };

  if (mirror_mode) {
    // MIRROR MODE: Flames start from CENTER, expand toward EDGES
    // Center of strip has the hottest fire (low heat indices), edges have the
    // coolest (high heat indices)

    // Left half: Center -> Left edge
    // j=0 should place HOTTEST (v_index near 0) at CENTER, coolest at edge
    for (int j = 0; j < half_len; j++) {
      // Reverse the v_index: j=0 gets low heat index (hot), j=half_len-1 gets
      // high (cool)
      int v_index = (int)((half_len - 1 - j) * scale);
      if (v_index >= VIRTUAL_HEIGHT)
        v_index = VIRTUAL_HEIGHT - 1;

      uint8_t r, g, b;
      heat_to_rgb(v_index, r, g, b);

      // j=0 goes to left edge (pixel 0), j=half_len-1 goes to center-left
      instance->_segment.setPixelColor(j, RGBW32(r, g, b, 0));
    }

    // Vacuum in center (between the two halves)
    for (int j = half_len; j < len - half_len; j++) {
      instance->_segment.setPixelColor(j, RGBW32(0, 0, 0, 0));
    }

    // Right half: Center -> Right edge
    // Mirror of left half
    for (int j = 0; j < half_len; j++) {
      int v_index = (int)((half_len - 1 - j) * scale);
      if (v_index >= VIRTUAL_HEIGHT)
        v_index = VIRTUAL_HEIGHT - 1;

      uint8_t r, g, b;
      heat_to_rgb(v_index, r, g, b);

      // j=0 goes to right edge (len-1), j=half_len-1 goes to center-right
      instance->_segment.setPixelColor(len - 1 - j, RGBW32(r, g, b, 0));
    }
  } else {
    // NORMAL MODE: Flames start from EDGES, meet in CENTER

    // Render Left Flame (0 -> half_len)
    for (int j = 0; j < half_len; j++) {
      int v_index = (int)(j * scale);
      if (v_index >= VIRTUAL_HEIGHT)
        v_index = VIRTUAL_HEIGHT - 1;

      uint8_t r, g, b;
      heat_to_rgb(v_index, r, g, b);
      instance->_segment.setPixelColor(j, RGBW32(r, g, b, 0));
    }

    // Render Vacuum (Black)
    for (int j = half_len; j < len - half_len; j++) {
      instance->_segment.setPixelColor(j, RGBW32(0, 0, 0, 0));
    }

    // Render Right Flame (len-1 -> len-1-half_len) - Mirrored
    for (int j = 0; j < half_len; j++) {
      int v_index = (int)(j * scale);
      if (v_index >= VIRTUAL_HEIGHT)
        v_index = VIRTUAL_HEIGHT - 1;

      uint8_t r, g, b;
      heat_to_rgb(v_index, r, g, b);

      // Mirror to other side: len - 1 - j
      instance->_segment.setPixelColor(len - 1 - j, RGBW32(r, g, b, 0));
    }
  }

  return FRAMETIME;
}

// --- Pacifica Effect ---
// Exact WLED implementation by Mark Kriegsman
// Gentle ocean waves - December 2019
// OPTIMIZED: Pre-computed palette caches for fast lookup

// Static palette caches - 256 entries each for all 3 WLED palettes
static CRGB pacifica_cache_1[256];
static CRGB pacifica_cache_2[256];
static CRGB pacifica_cache_3[256]; // WLED palette 3 (blue-purple)
static bool pacifica_caches_initialized = false;

// Initialize palette caches once (called on first frame)
static void pacifica_init_caches() {
  if (pacifica_caches_initialized)
    return;

  // Palette 1: Deep ocean blues transitioning to cyan-green (WLED
  // pacifica_palette_1)
  static const CRGBPalette16 pal1 = {0x000507, 0x000409, 0x00030B, 0x00030D,
                                     0x000210, 0x000212, 0x000114, 0x000117,
                                     0x000019, 0x00001C, 0x000026, 0x000031,
                                     0x00003B, 0x000046, 0x14554B, 0x28AA50};

  // Palette 2: Similar with different cyan-green highlights (WLED
  // pacifica_palette_2)
  static const CRGBPalette16 pal2 = {0x000507, 0x000409, 0x00030B, 0x00030D,
                                     0x000210, 0x000212, 0x000114, 0x000117,
                                     0x000019, 0x00001C, 0x000026, 0x000031,
                                     0x00003B, 0x000046, 0x0C5F52, 0x19BE5F};

  // Palette 3: Blue-purple (WLED pacifica_palette_3) - used for layers 3 and 4
  static const CRGBPalette16 pal3 = {0x000208, 0x00030E, 0x000514, 0x00061A,
                                     0x000820, 0x000927, 0x000B2D, 0x000C33,
                                     0x000E39, 0x001040, 0x001450, 0x001860,
                                     0x001C70, 0x002080, 0x1040BF, 0x2060FF};

  // Pre-compute all 256 interpolated colors for each palette
  for (int i = 0; i < 256; i++) {
    pacifica_cache_1[i] = ColorFromPalette(pal1, i, 255, LINEARBLEND);
    pacifica_cache_2[i] = ColorFromPalette(pal2, i, 255, LINEARBLEND);
    pacifica_cache_3[i] = ColorFromPalette(pal3, i, 255, LINEARBLEND);
  }

  pacifica_caches_initialized = true;
}

// Helper: WLED-EXACT wave layer function
// This matches WLED's pacifica_one_layer() precisely
static void pacifica_one_layer_wled(CRGB &c, uint16_t i, uint8_t cache_id,
                                    uint16_t cistart, uint16_t wavescale,
                                    uint8_t bri, uint16_t ioff) {
  // WLED EXACT: unsigned ci = cistart;
  unsigned ci = cistart;
  // WLED EXACT: unsigned waveangle = ioff;
  unsigned waveangle = ioff;
  // WLED EXACT: unsigned wavescale_half = (wavescale >> 1) + 20;
  unsigned wavescale_half = (wavescale >> 1) + 20;

  // WLED EXACT: waveangle += ((120 + SEGMENT.intensity) * i);
  waveangle += ((120 + instance->_segment.intensity) * i);

  // WLED EXACT: unsigned s16 = sin16_t(waveangle) + 32768;
  unsigned s16 = sin16_t(waveangle) + 32768;
  // WLED EXACT: unsigned cs = scale16(s16, wavescale_half) + wavescale_half;
  unsigned cs = scale16(s16, wavescale_half) + wavescale_half;
  // WLED EXACT: ci += (cs * i);
  ci += (cs * i);

  // WLED EXACT: unsigned sindex16 = sin16_t(ci) + 32768;
  unsigned sindex16 = sin16_t(ci) + 32768;
  // WLED EXACT: unsigned sindex8 = scale16(sindex16, 240);
  unsigned sindex8 = scale16(sindex16, 240);

  // Get color from cache (WLED uses ColorFromPalette directly)
  CRGB *cache;
  if (cache_id == 1)
    cache = pacifica_cache_1;
  else if (cache_id == 2)
    cache = pacifica_cache_2;
  else
    cache = pacifica_cache_3;

  CRGB layer = cache[sindex8];

  // Apply brightness scaling
  layer.r = scale8(layer.r, bri);
  layer.g = scale8(layer.g, bri);
  layer.b = scale8(layer.b, bri);

  // Additive blending (WLED uses c += layer)
  c.r = qadd8(c.r, layer.r);
  c.g = qadd8(c.g, layer.g);
  c.b = qadd8(c.b, layer.b);
}

// Helper: Add whitecaps to peaks (WLED exact)
static void pacifica_add_whitecaps(CRGB &c, uint16_t wave,
                                   uint8_t basethreshold) {
  uint8_t threshold = scale8(sin8(wave), 20) + basethreshold;
  uint8_t l = c.getAverageLight();
  if (l > threshold) {
    uint8_t overage = l - threshold;
    uint8_t overage2 = qadd8(overage, overage);
    c.r = qadd8(c.r, overage);
    c.g = qadd8(c.g, overage2);
    c.b = qadd8(c.b, qadd8(overage2, overage2));
  }
}

// Helper: Deepen colors (darken valleys) and ensure blue tint
static void pacifica_deepen_colors(CRGB &c) {
  c.b = scale8(c.b, 145);
  c.g = scale8(c.g, 200);
  // ESPHome gamma 2.8 crushes low values. Boost floor to ensure visible blue
  // tint. WLED uses (2, 5, 7) but ESPHome needs much higher for visibility
  // after gamma.
  c.r |= 8;  // Minimum red floor (boosted from 2)
  c.g |= 20; // Minimum green floor (boosted from 5)
  c.b |= 28; // Minimum blue floor (boosted from 7)
}

// Helper: Deepen colors with TEAL preservation (less aggressive darkening)
// This version maintains the teal appearance instead of crushing to deep blue
static void pacifica_deepen_colors_teal(CRGB &c) {
  // Relaxed scaling - don't crush colors as much
  c.b = scale8(c.b, 200); // Was 145, now less aggressive
  c.g = scale8(c.g, 220); // Was 200, now less aggressive
  // Teal floor (visible cyan-green tint)
  c.r |= 2;  // Minimal red
  c.g |= 8;  // Green present (teal component)
  c.b |= 12; // Blue dominant but not overwhelming
}

// Helper: Add one wave layer with pre-computed waveangle (intensity-zoomed)
// This version accepts a pre-calculated waveangle instead of computing from
// intensity
static void pacifica_one_layer_zoomed(CRGB &c, uint16_t i, uint8_t cache_id,
                                      uint16_t cistart, uint16_t wavescale,
                                      uint8_t bri, uint16_t waveangle) {
  uint16_t ci = cistart;
  uint16_t wavescale_half = (wavescale >> 1) + 20;

  // Use pre-computed waveangle (already includes intensity-based zoom)
  uint16_t s16 = sin16_t(waveangle) + 32768;
  uint16_t cs = scale16(s16, wavescale_half) + wavescale_half;
  ci += (cs * i);

  // Get full 16-bit sine for interpolation
  uint16_t sindex16_raw = sin16_t(ci) + 32768;

  // High byte = cache index, remainder = fractional for LERP
  uint8_t index_lo = sindex16_raw >> 8;
  uint8_t frac = sindex16_raw & 0xFF;
  uint8_t index_hi = index_lo + 1;

  // Scale to 240 range
  index_lo = scale8(index_lo, 240);
  index_hi = scale8(index_hi, 240);

  // Get adjacent cache entries and LERP
  CRGB *cache = (cache_id == 1) ? pacifica_cache_1 : pacifica_cache_2;
  CRGB lo = cache[index_lo];
  CRGB hi = cache[index_hi];

  CRGB layer;
  layer.r = lo.r + (((int16_t)(hi.r - lo.r) * frac) >> 8);
  layer.g = lo.g + (((int16_t)(hi.g - lo.g) * frac) >> 8);
  layer.b = lo.b + (((int16_t)(hi.b - lo.b) * frac) >> 8);

  // Apply brightness scaling
  layer.r = scale8(layer.r, bri);
  layer.g = scale8(layer.g, bri);
  layer.b = scale8(layer.b, bri);

  // Additive blending
  c.r = qadd8(c.r, layer.r);
  c.g = qadd8(c.g, layer.g);
  c.b = qadd8(c.b, layer.b);
}

// --- Ocean Effect ---
// Inspired by WLED's Pacifica, optimized for long strips and ambient lighting.
// Uses bidirectional wave interference with collision-based whitecaps.
uint16_t mode_ocean() {
  // === FRAME DIAGNOSTICS (enabled with CFX_FRAME_DIAGNOSTICS) ===
  static cfx::FrameDiagnostics ocean_diag;
  ocean_diag.frame_start();
  ocean_diag.maybe_log("Ocean");

  if (!instance)
    return 350;

  // Initialize palette caches on first call
  pacifica_init_caches();

  int len = instance->_segment.length();
  uint8_t speed = instance->_segment.speed;

  // Time base - uniform for all pixels (no position-dependent acceleration)
  uint32_t now = cfx_millis();
  uint32_t t = (now * (speed + 1)) >> 7;

  // === WAVE POSITIONS (time-based, moves independently of strip position) ===
  // Forward waves (move from start to end)
  uint16_t fwd1_pos = (t * 5); // Slow forward
  uint16_t fwd2_pos = (t * 7); // Medium forward

  // Backward waves (move from end to start)
  uint16_t bwd1_pos = -(t * 6); // Slow backward
  uint16_t bwd2_pos = -(t * 9); // Medium backward

  // BOOSTED layer brightness for low global brightness visibility
  // Higher values = more visible at low brightness settings
  uint8_t bri1 = 140 + ((sin8((t >> 3) & 0xFF) * 80) >> 8); // 140-220
  uint8_t bri2 = 130 + ((sin8((t >> 4) & 0xFF) * 70) >> 8); // 130-200
  uint8_t bri3 = 120 + ((sin8((t >> 5) & 0xFF) * 60) >> 8); // 120-180
  uint8_t bri4 = 100 + ((sin8((t >> 6) & 0xFF) * 50) >> 8); // 100-150

  // Whitecap base
  uint8_t wave_threshold = (t >> 2) & 0xFF;

  for (int i = 0; i < len; i++) {
    // Spatial position scaled to wave space
    uint16_t spatial = i * 256;

    // === CALCULATE 4 WAVE PHASES AT THIS PIXEL ===
    uint8_t idx1 = ((spatial >> 1) + fwd1_pos) >> 8;
    uint8_t idx2 = ((spatial >> 2) + fwd2_pos) >> 8;
    uint8_t idx3 = ((spatial >> 1) + bwd1_pos) >> 8;
    uint8_t idx4 = ((spatial >> 2) + bwd2_pos) >> 8;

    // BRIGHTER base ocean color for low brightness visibility
    CRGB c = CRGB(16, 48, 64);

    // Layer 1 (palette 1, forward)
    CRGB layer1 = pacifica_cache_1[idx1];
    c.r = qadd8(c.r, scale8(layer1.r, bri1));
    c.g = qadd8(c.g, scale8(layer1.g, bri1));
    c.b = qadd8(c.b, scale8(layer1.b, bri1));

    // Layer 2 (palette 2, forward)
    CRGB layer2 = pacifica_cache_2[idx2];
    c.r = qadd8(c.r, scale8(layer2.r, bri2));
    c.g = qadd8(c.g, scale8(layer2.g, bri2));
    c.b = qadd8(c.b, scale8(layer2.b, bri2));

    // Layer 3 (palette 3, backward)
    CRGB layer3 = pacifica_cache_3[idx3];
    c.r = qadd8(c.r, scale8(layer3.r, bri3));
    c.g = qadd8(c.g, scale8(layer3.g, bri3));
    c.b = qadd8(c.b, scale8(layer3.b, bri3));

    // Layer 4 (palette 3, backward different freq)
    CRGB layer4 = pacifica_cache_3[idx4];
    c.r = qadd8(c.r, scale8(layer4.r, bri4));
    c.g = qadd8(c.g, scale8(layer4.g, bri4));
    c.b = qadd8(c.b, scale8(layer4.b, bri4));

    // === COLLISION WHITECAPS (ambient-friendly) ===
    uint8_t fwd_bright = (layer1.b + layer2.b) >> 1;
    uint8_t bwd_bright = (layer3.b + layer4.b) >> 1;

    uint8_t collision = (fwd_bright * bwd_bright) >> 8;
    if (collision > 50) {
      uint8_t whiteness = (collision - 50) >> 1;
      c.r = qadd8(c.r, whiteness >> 1);
      c.g = qadd8(c.g, whiteness);
      c.b = qadd8(c.b, qadd8(whiteness, whiteness >> 1));
    }

    // Additional whitecaps on very bright areas
    uint8_t l = (c.r + c.g + c.b) / 3;
    uint8_t threshold = scale8(sin8(wave_threshold + (i * 7)), 20) + 45;
    if (l > threshold) {
      uint8_t overage = l - threshold;
      c.r = qadd8(c.r, overage >> 1);
      c.g = qadd8(c.g, overage);
      c.b = qadd8(c.b, qadd8(overage, overage >> 1));
    }

    // Color deepening (teal character) - REDUCED scaling to preserve brightness
    c.b = scale8(c.b, 220); // Was 200
    c.g = scale8(c.g, 235); // Was 220

    // Ensure minimum visible floor (preserves animation at low brightness)
    c.r = (c.r < 8) ? 8 : c.r;
    c.g = (c.g < 16) ? 16 : c.g;
    c.b = (c.b < 24) ? 24 : c.b;

    // GAMMA CORRECTION: Apply gamma to final linear output to simulate "deep
    // ocean" contrast which was previously provided by the driver's gamma
    // correction.
    instance->_segment.setPixelColor(i, RGBW32(instance->applyGamma(c.r),
                                               instance->applyGamma(c.g),
                                               instance->applyGamma(c.b), 0));
  }

  return FRAMETIME;
}

// --- Plasma Effect ---
// Ported from WLED FX.cpp by Andrew Tuline
// Smooth, liquid organic effect using wave mixing
// Fixed: Drastically reduced spatial freq (wide gradients) + slow temporal
// drift
uint16_t mode_plasma(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Allocate storage for previous color indices (for temporal smoothing)
  if (!instance->_segment.allocateData(len)) {
    return mode_static();
  }
  uint8_t *prevColors = instance->_segment.data;

  // Initialize on first call
  if (instance->_segment.call == 0) {
    instance->_segment.aux0 = hw_random8(0, 2);
    memset(prevColors, 128, len);
  }

  // === TEMPORAL SCALING ===
  // Heavy time divisor for slow, liquid drift
  // millis >> 7 = divide by 128 for very slow motion
  uint32_t now = instance->now;
  uint32_t slowTime = (now >> 7); // Divide by 128 for slow drift

  // Phase oscillation using scaled time
  // This creates the slow "breathing" phase shift
  uint8_t aux_offset = instance->_segment.aux0;
  uint8_t phase1_raw = (slowTime * (6 + aux_offset)) >> 2; // Very slow
  uint8_t phase2_raw = (slowTime * (7 + aux_offset)) >> 2; // Slightly different

  // Convert to signed phase for organic interference
  int8_t thisPhase = (int8_t)(sin8(phase1_raw) - 128) >> 1; // -64 to +63
  int8_t thatPhase = (int8_t)(sin8(phase2_raw) - 128) >> 1; // -64 to +63

  // === UNIFIED SPATIAL SCALING ===
  // MINIMAL speed impact - wide color bands at ALL speed settings
  // Speed=0: spatialScale=2, Speed=128: spatialScale=4, Speed=255:
  // spatialScale=5 This prevents the "noisy" 5-6 LED sequences at high speed
  // values
  uint8_t speed = instance->_segment.speed;
  uint8_t spatialScale = 2 + (speed >> 6); // Range: 2-5 (was 3-18!)

  // Temporal smoothing: blend 10/256 (~4%) new color per frame
  const uint8_t blendSpeed = 10;

  // Get active palette
  const uint32_t *active_palette;
  if (instance->_segment.palette == 0) {
    active_palette = getPaletteByIndex(7); // Party palette default
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  for (int i = 0; i < len; i++) {
    // === UNIFIED spatial phase - single value for all calculations ===
    // This creates cohesive color pools without "dual pattern" artifacts
    uint8_t spatialPhase = (i * spatialScale) & 0xFF;

    // Color index from interfering waves - both use SAME spatial base
    uint8_t colorInput = (spatialPhase + thisPhase) & 0xFF;
    uint8_t targetIndex =
        sin8(colorInput) + ((cos8_t(colorInput + 64) - 128) >> 1);

    // Temporal smoothing: blend toward target for liquid feel
    uint8_t prevIndex = prevColors[i];
    int16_t diff = (int16_t)targetIndex - (int16_t)prevIndex;
    int16_t step = (diff * blendSpeed) >> 8;
    if (step == 0 && diff != 0)
      step = (diff > 0) ? 1 : -1;
    uint8_t smoothIndex = prevIndex + step;
    prevColors[i] = smoothIndex;

    // === BRIGHTNESS MODULATION controlled by INTENSITY slider ===
    // Low intensity = deep voids (high contrast, can reach near-black)
    // High intensity = uniform brightness (fills in voids)
    uint8_t intensity = instance->_segment.intensity;

    uint8_t briInput = (spatialPhase * 2 + thatPhase + 64) & 0xFF;
    uint8_t rawBri = sin8(briInput);

    // Apply gamma correction to rawBri for deep contrast curve
    // REMOVED dim8_video (x^2) to soften the curve because x^3.5 is steep
    // enough. This widens the blocks and reduces the "void" effect.
    uint8_t gammaBri = rawBri;

    // Calculate contrast depth from intensity with SHIFTED QUADRATIC curve
    // Shifted so intensity=128 (default) gives same fill as intensity=90 would
    // This provides more voids at the default setting
    int16_t shifted = (int16_t)intensity - 38; // Shift down by 38
    if (shifted < 0)
      shifted = 0;
    uint8_t fillAmount = ((uint16_t)shifted * shifted) >> 8; // Quadratic: 0-185

    // Start from gammaBri, add fillAmount to bring up the lows
    // At intensity=0-38: brightness = gammaBri (deep voids)
    // At intensity=128: brightness = gammaBri + ~12% fill (visible voids)
    // At intensity=255: brightness = ~185 fill (mostly uniform)
    uint16_t brightness16 = gammaBri + ((fillAmount * (255 - gammaBri)) >> 8);

    // Very low floor of 8 (3%) to prevent true black but allow deep voids
    // THEN APPLY GAMMA to the whole thing to crush the floor at Gamma 1.0
    uint8_t brightness = (brightness16 < 8) ? 8 : (uint8_t)brightness16;
    brightness = instance->applyGamma(brightness);

    // Get color from palette with gamma-corrected brightness
    CRGBW c = ColorFromPalette(smoothIndex, brightness, active_palette);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
  }

  instance->_segment.call++;
  return FRAMETIME;
}

// --- Colorwaves Effect (ID 63, formerly Pride 2015) ---
// Ported from WLED FX.cpp mode_colorwaves_pride_base(true)
// Author: Mark Kriegsman
// Modified: Uniform brightness, intensity controls saturation
uint16_t mode_pride_2015(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // WLED formula: duration = 10 + speed
  // At 60fps vs WLED's 42fps, we're 1.4x faster, so use 70%
  uint16_t duration = 10 + (instance->_segment.speed * 7 / 10);

  // Persistent state
  uint32_t sPseudotime = instance->_segment.step;
  uint16_t sHue16 = instance->_segment.aux0;

  // Wave timing calculations
  uint16_t msmultiplier = beatsin88_t(147, 23, 60);
  uint16_t hueinc16 = beatsin88_t(113, 1, 3000);

  uint16_t hue16 = sHue16;

  // Advance persistent state
  sPseudotime += duration * msmultiplier;
  sHue16 += duration * beatsin88_t(400, 5, 9);

  // Get active palette
  const uint32_t *active_palette;
  if (instance->_segment.palette == 0) {
    active_palette = PaletteRainbow;
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  // Intensity controls saturation (same as Colorloop)
  // 0 = white, 128 = default full saturation, 255 = pure colors
  uint8_t intensity = instance->_segment.intensity;
  uint8_t saturation = 255; // Full saturation default
  if (intensity < 128) {
    // Blend toward white (desaturate) at low intensity
    saturation = intensity * 2;
  }

  // Process all pixels - UNIFORM BRIGHTNESS (no banding)
  for (int i = 0; i < len; i++) {
    // Accumulate hue
    hue16 += hueinc16;
    uint8_t hue8 = hue16 >> 8;

    // Get color at full brightness
    CRGBW c = ColorFromPalette(hue8, 255, active_palette);

    // Apply saturation (blend toward white at low intensity)
    if (saturation < 255) {
      uint8_t white_blend = 255 - saturation;
      c.r = c.r + ((255 - c.r) * white_blend >> 8);
      c.g = c.g + ((255 - c.g) * white_blend >> 8);
      c.b = c.b + ((255 - c.b) * white_blend >> 8);
    }

    // Blend with existing for smooth transitions
    uint32_t existing = instance->_segment.getPixelColor(i);
    uint8_t er = (existing >> 16) & 0xFF;
    uint8_t eg = (existing >> 8) & 0xFF;
    uint8_t eb = existing & 0xFF;

    uint8_t blend = 64;
    uint8_t nr = er + (((int16_t)(c.r - er) * blend) >> 8);
    uint8_t ng = eg + (((int16_t)(c.g - eg) * blend) >> 8);
    uint8_t nb = eb + (((int16_t)(c.b - eb) * blend) >> 8);

    instance->_segment.setPixelColor(i, RGBW32(nr, ng, nb, c.w));
  }

  // Save state

  instance->_segment.step = sPseudotime;
  instance->_segment.aux0 = sHue16;

  return FRAMETIME;
}

// --- Breathe Effect (ID 2) ---
// Does the "standby-breathing" of well known i-Devices
// Author: WLED Team
uint16_t mode_breath(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // === WLED-FAITHFUL TIMING using centralized helper ===
  // Use segment.step as per-instance timing state (avoids static variable
  // cross-contamination)
  auto timing = cfx::calculate_frame_timing(instance->_segment.speed,
                                            instance->_segment.step);

  // Time-based counter using scaled_now (multiplied by 20 for proper speed)
  uint32_t counter = (timing.scaled_now * 20) & 0xFFFF;
  counter = (counter >> 2) + (counter >> 4); // 0-16384 + 0-2048

  unsigned var = 0;
  if (counter < 16384) {
    if (counter > 8192)
      counter = 8192 - (counter - 8192);
    var = sin16_t(counter) / 103; // Close to parabolic, max ~224
  }

  // lum = 30 + var (30 minimum = ~12% floor, max ~254)
  // WLED uses this as blend amount, not brightness multiplier
  // GAMMA CORRECTION: 30 is only ~12% at Gamma 1.0. At Gamma 2.8 it's
  // invisible.
  uint8_t lum = instance->shiftFloor(30) + var;

  // Get base color (user selected or white as fallback)
  uint32_t baseColor = instance->_segment.colors[0];
  if (baseColor == 0)
    baseColor = 0xFFFFFF; // Default white if no color set

  for (int i = 0; i < len; i++) {
    uint8_t fgR, fgG, fgB;
    uint8_t bgR, bgG, bgB;

    // Use solid color when palette is 255 (Solid) OR 0 (Default/None)
    // This fixes the issue where Default palette ignored the user's color.
    if (instance->_segment.palette == 255 || instance->_segment.palette == 0) {
      // Use user's base color as foreground
      fgR = (baseColor >> 16) & 0xFF;
      fgG = (baseColor >> 8) & 0xFF;
      fgB = baseColor & 0xFF;
    } else {
      // Use palette color as foreground (0-19)
      const uint32_t *active_palette =
          getPaletteByIndex(instance->_segment.palette);
      CRGBW c = ColorFromPalette((i * 256 / len), 255, active_palette);
      fgR = c.r;
      fgG = c.g;
      fgB = c.b;
    }

    // Calculate background color from foreground (21% brightness)
    // This ensures that even with palettes, the "off" state matches the "on"
    // color.
    bgR = (fgR * 54) >> 8;
    bgG = (fgG * 54) >> 8;
    bgB = (fgB * 54) >> 8;

    // Blend: result = background + (foreground - background) * lum / 255
    uint8_t r = bgR + (((int16_t)(fgR - bgR) * lum) >> 8);
    uint8_t g = bgG + (((int16_t)(fgG - bgG) * lum) >> 8);
    uint8_t b = bgB + (((int16_t)(fgB - bgB) * lum) >> 8);

    instance->_segment.setPixelColor(i, RGBW32(r, g, b, 0));
  }

  return FRAMETIME;
}

// --- Dissolve Effect (ID 18) ---
// Fill -> Hold ON -> Dissolve Out -> Hold OFF cycle
// Uses SHADOW BITMASK for persistence (hardware buffer cleared every frame)
uint16_t mode_dissolve(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Shadow buffer size: 1 bit per pixel, rounded up to bytes
  uint16_t shadow_size = (len + 7) / 8;

  // Allocate shadow buffer using allocateData (proper memory lifecycle
  // tracking)
  if (!instance->_segment.allocateData(shadow_size)) {
    return mode_static(); // Allocation failed
  }

  // Reset state on fresh allocation (allocateData zeros memory for us)
  if (instance->_segment.reset) {
    memset(instance->_segment.data, 0, shadow_size);
    instance->_segment.aux0 = 0;
    instance->_segment.aux1 = 0;
    instance->_segment.step = instance->now;
    instance->_segment.reset = false;
  }

  uint8_t *shadow = instance->_segment.data;

// Helper macros for bitmask
#define SHADOW_GET(idx) ((shadow[(idx) >> 3] >> ((idx) & 7)) & 1)
#define SHADOW_SET(idx) (shadow[(idx) >> 3] |= (1 << ((idx) & 7)))
#define SHADOW_CLR(idx) (shadow[(idx) >> 3] &= ~(1 << ((idx) & 7)))

  // State machine: 0=FILLING, 1=HOLD_ON, 2=DISSOLVING, 3=HOLD_OFF
  uint8_t state = instance->_segment.aux0 & 0x03;
  uint16_t pixel_count = instance->_segment.aux1;
  uint32_t state_start = instance->_segment.step;

  // === DISSOLVE CONTROL MAPPING ===
  // Speed controls hold time between fill/dissolve phases
  // Intensity controls dissolve rate (probability-based like WLED)
  uint8_t raw_speed = instance->_segment.speed;
  uint8_t raw_intensity = instance->_segment.intensity;

  // Hold time: Speed 255 = 500ms (fast cycle), Speed 0 = 3000ms (slow cycle)
  uint32_t hold_ms = 500 + ((255 - raw_speed) * 10);

  // Fill threshold: 100% of strip (ensures full fill)
  uint16_t fill_threshold = len;

  // Timeout safety: 30 seconds max for fill phase at very low intensity
  uint32_t fill_timeout = 30000;

  // === STATE MACHINE (manipulates shadow only) ===
  switch (state) {
  case 0: { // FILLING - set random OFF pixels to ON using probability
    // Probability-based approach (WLED-style):
    // At intensity 0: ~0% chance per frame → Very slow fill
    // At intensity 128: ~50% chance per frame → Medium fill
    // At intensity 255: ~100% + bonus → Fast fill
    int pixels_to_spawn = 0;
    if (hw_random8() <= raw_intensity) {
      pixels_to_spawn = 1;
      // At high intensity, chance for extra pixels for faster fill
      if (raw_intensity > 200 && hw_random8() < (raw_intensity - 200) * 2) {
        pixels_to_spawn++;
      }
    }

    for (int n = 0; n < pixels_to_spawn && pixel_count < fill_threshold; n++) {
      // Linear Fallback for Dead Pixels
      int target = hw_random16() % len;
      if (SHADOW_GET(target)) {
        // Random hit an ON pixel - linear scan to find next OFF
        bool found = false;
        for (int scan = 0; scan < len; scan++) {
          int scan_idx = (target + scan) % len;
          if (!SHADOW_GET(scan_idx)) {
            target = scan_idx;
            found = true;
            break;
          }
        }
        if (!found) {
          // Strip is completely full
          pixel_count = len;
          break;
        }
      }
      SHADOW_SET(target);
      pixel_count++;
    }
    // Transition check
    if (pixel_count >= fill_threshold ||
        (instance->now - state_start) > fill_timeout) {
      state = 1;
      state_start = instance->now;
    }
    break;
  }

  case 1: // HOLD_ON - wait, shadow unchanged
    if (instance->now - state_start > hold_ms) {
      state = 2;
      state_start = instance->now;
    }
    break;

  case 2: { // DISSOLVING - set random ON pixels to OFF using probability
    // Use same probability-based approach as filling
    int pixels_to_remove = 0;
    if (hw_random8() <= raw_intensity) {
      pixels_to_remove = 1;
      if (raw_intensity > 200 && hw_random8() < (raw_intensity - 200) * 2) {
        pixels_to_remove++;
      }
    }

    for (int n = 0; n < pixels_to_remove && pixel_count > 0; n++) {
      // Linear Fallback for Dead Pixels
      int target = hw_random16() % len;
      if (!SHADOW_GET(target)) {
        // Random hit an OFF pixel - linear scan to find next ON
        bool found = false;
        for (int scan = 0; scan < len; scan++) {
          int scan_idx = (target + scan) % len;
          if (SHADOW_GET(scan_idx)) {
            target = scan_idx;
            found = true;
            break;
          }
        }
        if (!found) {
          // Strip is completely empty
          pixel_count = 0;
          break;
        }
      }
      SHADOW_CLR(target);
      pixel_count--;
    }
    // Transition check
    if (pixel_count == 0) {
      state = 3;
      state_start = instance->now;
    }
    break;
  }

  case 3: // HOLD_OFF - wait with strip dark
    if (instance->now - state_start > hold_ms) {
      state = 0;
      state_start = instance->now;
      pixel_count = 0;
      memset(shadow, 0, shadow_size); // Clear shadow for fresh start
    }
    break;
  }

  // === RENDER: Draw from shadow state ===
  // === FIX 3: Palette 0 = Rainbow (CHSV hue cycling) ===
  bool use_rainbow = (instance->_segment.palette == 0);
  const uint32_t *active_palette = nullptr;
  if (!use_rainbow) {
    if (instance->_segment.palette == 255) {
      fillSolidPalette(instance->_segment.colors[0]);
      active_palette = PaletteSolid;
    } else {
      active_palette = getPaletteByIndex(instance->_segment.palette);
    }
  }

  for (int i = 0; i < len; i++) {
    if (SHADOW_GET(i)) {
      if (use_rainbow) {
        // Rainbow: hue based on position + time drift
        uint8_t hue = (uint8_t)(i * 5 + (instance->now / 20));
        // Convert HSV to RGB (full saturation, full value)
        CRGBW col;
        // Simple HSV to RGB conversion
        uint8_t region = hue / 43;
        uint8_t remainder = (hue - (region * 43)) * 6;
        uint8_t p = 0;
        uint8_t q = 255 - remainder;
        uint8_t t = remainder;
        switch (region) {
        case 0:
          col.r = 255;
          col.g = t;
          col.b = p;
          break;
        case 1:
          col.r = q;
          col.g = 255;
          col.b = p;
          break;
        case 2:
          col.r = p;
          col.g = 255;
          col.b = t;
          break;
        case 3:
          col.r = p;
          col.g = q;
          col.b = 255;
          break;
        case 4:
          col.r = t;
          col.g = p;
          col.b = 255;
          break;
        default:
          col.r = 255;
          col.g = p;
          col.b = q;
          break;
        }
        col.w = 0;
        instance->_segment.setPixelColor(i, RGBW32(col.r, col.g, col.b, col.w));
      } else {
        uint8_t hue = (i * 255 / len);
        CRGBW col = ColorFromPalette(hue, 255, active_palette);
        instance->_segment.setPixelColor(i, RGBW32(col.r, col.g, col.b, col.w));
      }
    } else {
      instance->_segment.setPixelColor(i, 0);
    }
  }

#undef SHADOW_GET
#undef SHADOW_SET
#undef SHADOW_CLR

  // Save state
  instance->_segment.aux0 = state;
  instance->_segment.aux1 = pixel_count;
  instance->_segment.step = state_start;

  return FRAMETIME;
}

// --- Juggle Effect (ID 64) ---
// Eight colored dots weaving in and out of sync
// Author: FastLED DemoReel100
uint16_t mode_juggle(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Fade to black (trail effect)
  // Fix: Old formula was too fast (short tail) and hit 0 at high intensity
  // (stuck pixels). New formula: Slower fade (longer tail) + Min 1 fade
  // guarantees cleanup.
  uint8_t fadeAmount = (255 - instance->_segment.intensity) / 5;
  if (fadeAmount < 1)
    fadeAmount = 1;

  for (int i = 0; i < len; i++) {
    uint32_t c = instance->_segment.getPixelColor(i);
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;
    r = (r > fadeAmount) ? r - fadeAmount : 0;
    g = (g > fadeAmount) ? g - fadeAmount : 0;
    b = (b > fadeAmount) ? b - fadeAmount : 0;
    instance->_segment.setPixelColor(i, RGBW32(r, g, b, 0));
  }

  // Get palette
  const uint32_t *active_palette;
  if (instance->_segment.palette == 0) {
    active_palette = PaletteRainbow;
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  // 8 bouncing dots
  uint8_t dothue = 0;
  for (int j = 0; j < 8; j++) {
    // Each dot has different speed based on j
    uint16_t bpm = (16 + instance->_segment.speed) * (j + 7);
    int index = beatsin88_t(bpm, 0, len - 1);

    // Get existing color and add to it
    uint32_t existing = instance->_segment.getPixelColor(index);

    CRGBW c;
    if (instance->_segment.palette == 255) {
      uint32_t col = instance->_segment.colors[0];
      c = CRGBW((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF,
                (col >> 24) & 0xFF);
    } else {
      c = ColorFromPalette(dothue, 255, active_palette);
    }

    // Additive blend
    uint8_t er = (existing >> 16) & 0xFF;
    uint8_t eg = (existing >> 8) & 0xFF;
    uint8_t eb = existing & 0xFF;
    uint8_t nr = (er + c.r > 255) ? 255 : er + c.r;
    uint8_t ng = (eg + c.g > 255) ? 255 : eg + c.g;
    uint8_t nb = (eb + c.b > 255) ? 255 : eb + c.b;

    instance->_segment.setPixelColor(index, RGBW32(nr, ng, nb, 0));
    dothue += 32;
  }

  return FRAMETIME;
}

// --- Flow Effect (ID 110) ---
// Best of both worlds from Palette and Spot effects. By Aircoookie
uint16_t mode_flow(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Get palette
  const uint32_t *active_palette;
  if (instance->_segment.palette == 0) {
    active_palette = PaletteRainbow;
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  // === WLED-FAITHFUL TIMING ===
  // WLED: counter = strip.now * ((SEGMENT.speed >> 2) +1); counter = counter >>
  // 8;
  unsigned counter = 0;
  if (instance->_segment.speed != 0) {
    counter = instance->now * ((instance->_segment.speed >> 2) + 1);
    counter = counter >> 8;
  }

  // Calculate zones based on intensity - WLED exact formula
  // WLED: unsigned maxZones = SEGLEN / 6; (no early clamping!)
  int maxZones = len / 6; // Each zone needs at least 6 LEDs
  int zones = (instance->_segment.intensity * maxZones) >> 8;
  if (zones & 0x01)
    zones++; // Must be even
  if (zones < 2)
    zones = 2;
  int zoneLen = len / zones;
  int offset = (len - zones * zoneLen) >> 1;

  // Fill background with counter-colored palette
  uint8_t bgIndex = (uint8_t)(256 - counter);
  CRGBW bgColor = ColorFromPalette(bgIndex, 255, active_palette);
  for (int i = 0; i < len; i++) {
    instance->_segment.setPixelColor(
        i, RGBW32(bgColor.r, bgColor.g, bgColor.b, bgColor.w));
  }

  // Draw each zone
  for (int z = 0; z < zones; z++) {
    int pos = offset + z * zoneLen;
    for (int i = 0; i < zoneLen; i++) {
      uint8_t colorIndex = (i * 255 / zoneLen) - (uint8_t)counter;
      int led = (z & 0x01) ? i : (zoneLen - 1) - i;
      CRGBW c = ColorFromPalette(colorIndex, 255, active_palette);
      instance->_segment.setPixelColor(pos + led, RGBW32(c.r, c.g, c.b, c.w));
    }
  }

  return FRAMETIME;
}

// --- Phased Effect (ID 105) ---
// Sine wave interference pattern. By Andrew Tuline
uint16_t mode_phased(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Get palette
  const uint32_t *active_palette;
  if (instance->_segment.palette == 0) {
    active_palette = PaletteRainbow;
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  // Phase accumulator (stored in step)
  uint16_t allfreq = 16;
  int32_t phase = instance->_segment.step;
  phase += (instance->_segment.speed * 7 / 10) / 4; // Slower phase change
  instance->_segment.step = phase;

  uint8_t cutOff = 255 - instance->_segment.intensity;
  uint8_t modVal = 5;

  uint8_t colorIndex = (instance->now / 64) & 0xFF;

  for (int i = 0; i < len; i++) {
    uint16_t val = (i + 1) * allfreq;
    val += (phase / 256) * ((i % modVal) + 1) / 2;
    uint8_t b = cubicwave8(val & 0xFF);
    b = (b > cutOff) ? (b - cutOff) : 0;

    CRGBW c = ColorFromPalette(colorIndex, b, active_palette);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));

    colorIndex += 256 / len;
    if (len > 256)
      colorIndex++;
  }

  return FRAMETIME;
}

// --- Ripple Effect (ID 79) ---
// Expanding waves from random points (simplified, no allocateData)
// --- Ripple Effect (ID 79) ---
struct RippleState {
  uint16_t age;    // High-res position (radius = age >> 8)
  uint8_t color;   // Color index
  uint16_t center; // Center position
  bool active;     // Active flag
};

uint16_t mode_ripple(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Max ripples
  uint8_t maxRipples = 1 + (instance->_segment.intensity >> 5);
  uint16_t dataSize = sizeof(RippleState) * maxRipples;

  if (!instance->_segment.allocateData(dataSize))
    return mode_static(); // Alloc failed

  RippleState *ripples = (RippleState *)instance->_segment.data;

  // 1. Fade Background
  // WLED Standard Fade (224) - Aggressive but not total wipe.
  // Creates organic "water drop" decay.
  instance->_segment.fadeToBlackBy(224);

  // 2. Spawn Logic
  // Increased density for "Organic" feel (WLED-like)
  // random16(400) provides ~5x more ripples than the previous "Nuclear" setting
  if (random16(400) < instance->_segment.intensity) {
    for (int i = 0; i < maxRipples; i++) {
      if (!ripples[i].active) {
        // Spatial filter: Don't spawn near the last ripple
        uint16_t last_spawn = instance->_segment.aux0;
        uint16_t new_center = 0;
        int attempts = 0;

        // Try to find a distinct spot
        do {
          new_center = random16(len);
          attempts++;
        } while (abs((int)new_center - (int)last_spawn) < (len / 4) &&
                 attempts < 5);

        instance->_segment.aux0 = new_center; // Save history

        ripples[i].active = true;
        ripples[i].age = 0;
        ripples[i].center = new_center;
        ripples[i].color = random8();
        break;
      }
    }
  }

  // 3. Process Ripples
  // Speed Tuning: Fluid motion (Speed + 2)
  uint16_t step = instance->_segment.speed + 2;

  // Calculate Max Age (Lifespan)
  // Ripple should fade out completely by the time it travels 'len' pixels
  uint32_t max_age = (uint32_t)len * 256;

  const uint32_t *active_palette = nullptr;
  if (instance->_segment.palette != 0) {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  } else {
    active_palette = PaletteRainbow;
  }

  for (int i = 0; i < maxRipples; i++) {
    if (!ripples[i].active)
      continue;

    // Lifespan & Kill Check
    if (ripples[i].age > max_age) {
      ripples[i].active = false;
      continue;
    }

    uint16_t radius = ripples[i].age >> 8;

    // Kill if out of bounds (Safety, though max_age should handle it)
    if (radius > len + 6) {
      ripples[i].active = false;
      continue;
    }

    // Energy Decay: Dimmer as it gets older
    // map(age, 0, max_age, 255, 0)
    uint8_t energy = 255 - ((ripples[i].age * 255) / max_age);

    // Get Color
    CRGBW c;
    if (instance->_segment.palette == 255) {
      uint32_t col = instance->_segment.colors[0];
      c = CRGBW((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF,
                (col >> 24) & 0xFF);
    } else {
      c = ColorFromPalette(ripples[i].color, 255, active_palette);
    }

    // Apply Energy Decay to base color
    c.r = scale8(c.r, energy);
    c.g = scale8(c.g, energy);
    c.b = scale8(c.b, energy);
    c.w = scale8(c.w, energy);

    // Render Wavefronts (Center +/- Radius)
    // Body Wave (+/- 6 pixels)
    int centers[2] = {(int)ripples[i].center + radius,
                      (int)ripples[i].center - radius};

    for (int k = 0; k < 2; k++) {
      int wave_center = centers[k];
      int start = wave_center - 6;
      int end = wave_center + 6;

      // Clamp to strip bounds
      if (start < 0)
        start = 0;
      if (end > len)
        end = len;

      for (int pos = start; pos < end; pos++) {
        // Distance from theoretical wave center
        int delta = abs(pos - wave_center);

        // Map distance 0-6 to brightness 255-0
        // Safety: use int16_t to avoid unsigned underflow (uint8_t can't be >
        // 255)
        int16_t ramp = 255 - (delta * 42);
        if (ramp < 0)
          ramp = 0;
        uint8_t bri = cubicwave8((uint8_t)ramp);

        // Apply color (Energy already applied to c)
        CRGBW c_pixel = c;
        c_pixel.r = scale8(c_pixel.r, bri);
        c_pixel.g = scale8(c_pixel.g, bri);
        c_pixel.b = scale8(c_pixel.b, bri);
        c_pixel.w = scale8(c_pixel.w, bri);

        // Additive blend
        uint32_t old = instance->_segment.getPixelColor(pos);
        instance->_segment.setPixelColor(
            pos, RGBW32(qadd8((old >> 16) & 0xFF, c_pixel.r),
                        qadd8((old >> 8) & 0xFF, c_pixel.g),
                        qadd8(old & 0xFF, c_pixel.b),
                        qadd8((old >> 24) & 0xFF, c_pixel.w)));
      }
    }

    ripples[i].age += step;
  }

  return FRAMETIME;
}

// --- Meteor Effect (ID 76) ---
// Meteor with random decay trail
// Simplified version (no allocateData)
uint16_t mode_meteor(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Get palette - WLED default is solid color, not Fire
  const uint32_t *active_palette = nullptr;
  bool use_solid_color = false;

  if (instance->_segment.palette == 0) {
    // WLED default: Use solid color (primary color)
    use_solid_color = true;
  } else if (instance->_segment.palette == 255) {
    use_solid_color = true;
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  // === WLED-FAITHFUL TIMING ===
  // WLED: counter = strip.now * ((SEGMENT.speed >> 2) + 8);
  uint32_t counter = instance->now * ((instance->_segment.speed >> 2) + 8);
  int meteorPos = (counter * len) >> 16;
  meteorPos = meteorPos % len;

  // Meteor size (5% of strip)
  int meteorSize = 1 + len / 20;

  // --- 1. DECAY LOOP (Before Drawing Head) ---
  // WLED uses scale8 (multiplicative) with probability check:
  // if (hw_random8() <= 255 - SEGMENT.intensity) { scale8(trail,
  // 128+random(127)) } High intensity = fewer pixels decay = longer trail Low
  // intensity = more pixels decay = shorter trail
  for (int i = 0; i < len; i++) {
    // Probability check: higher intensity = fewer pixels decay per frame
    if (hw_random8() <= 255 - instance->_segment.intensity) {
      uint32_t c = instance->_segment.getPixelColor(i);
      uint8_t r = (c >> 16) & 0xFF;
      uint8_t g = (c >> 8) & 0xFF;
      uint8_t b = c & 0xFF;

      // Multiplicative decay with random factor
      // Scale factor 200-255 for longer trail (78-100% retention per frame)
      // Original WLED uses 128-255 but runs at higher FPS
      // GAMMA CORRECTION: Adjust retention factor for gamma
      uint8_t raw_factor = 200 + hw_random8(55);
      uint8_t scale_factor = instance->getFadeFactor(raw_factor);
      r = scale8(r, scale_factor);
      g = scale8(g, scale_factor);
      b = scale8(b, scale_factor);

      instance->_segment.setPixelColor(i, RGBW32(r, g, b, 0));
    }
  }

  // --- 2. DRAW METEOR HEAD ---
  // Fix: Ensure head is bright/hot.
  for (int j = 0; j < meteorSize; j++) {
    int index = (meteorPos + j) % len;

    if (use_solid_color) {
      // Use Primary Color
      uint32_t c = instance->_segment.colors[0];
      if (c == 0)
        c = 0xFFFFFF; // Default white
      instance->_segment.setPixelColor(index, c);
    } else {
      // Use Palette - Universal Fix (Dynamic + Energy Boost)
      // Reverted to this state as "Static Peak" was too flat.
      // 1. Dynamic Indexing: Brings back rich colors for Ocean/Rainbow.
      uint8_t colorIndex = (index * 10) + (instance->now >> 4);
      CRGBW c = ColorFromPalette(colorIndex, 255, active_palette);

      // 2. White Energy Boost (Universal)
      // Inject white to guarantee visibility for all palettes.
      c.r = qadd8(c.r, 80);
      c.g = qadd8(c.g, 80);
      c.b = qadd8(c.b, 80);

      instance->_segment.setPixelColor(index, RGBW32(c.r, c.g, c.b, c.w));
    }
  }

  return FRAMETIME;
}

// --- Noise Pal Effect (ID 107) ---
// Slow noise palette by Andrew Tuline. WLED-faithful port.
// Uses true 2D Perlin noise + dynamic palette generation/blending.
uint16_t mode_noisepal(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Allocate space for 2 CRGBPalette16: current (palettes[0]) + target
  // (palettes[1])
  unsigned dataSize = sizeof(CRGBPalette16) * 2; // 2 * 16 * 3 = 96 bytes
  if (!instance->_segment.allocateData(dataSize))
    return mode_static();

  CRGBPalette16 *palettes =
      reinterpret_cast<CRGBPalette16 *>(instance->_segment.data);

  // Scale based on intensity (zoom level) — WLED exact formula
  unsigned scale = 15 + (instance->_segment.intensity >> 2); // 15-78

  // Generate new target palette periodically (4-6.5 seconds based on speed)
  unsigned changePaletteMs = 4000 + instance->_segment.speed * 10;
  if (instance->now - instance->_segment.step > changePaletteMs) {
    instance->_segment.step = instance->now;

    // WLED exact: 4-stop random HSV palette
    uint8_t baseI = random8();
    palettes[1] =
        CRGBPalette16(CHSV(baseI + random8(64), 255, random8(128, 255)),
                      CHSV(baseI + 128, 255, random8(128, 255)),
                      CHSV(baseI + random8(92), 192, random8(128, 255)),
                      CHSV(baseI + random8(92), 255, random8(128, 255)));
  }

  // Smoothly blend current palette toward target — WLED uses 48 steps
  nblendPaletteTowardPalette(palettes[0], palettes[1], 48);

  // If user selected a palette, override the dynamic one
  // If user selected a palette, override the dynamic one
  if (instance->_segment.palette > 0) {
    if (instance->_segment.palette == 255 || instance->_segment.palette == 21) {
      // Handle "Solid" palette: use primary color with brightness variation
      // This creates a "texture" (Dim -> Full -> Dim) so noise is visible
      CRGB c = CRGB(instance->_segment.colors[0]);
      CRGB dim(scale8(c.r, 60), scale8(c.g, 60),
               scale8(c.b, 60)); // ~25% brightness base

      for (int i = 0; i < 16; i++) {
        // Create a triangle wave: 0 (Dim) -> 255 (Full) -> 0 (Dim)
        uint8_t ramp = (i < 8) ? (i * 32) : (255 - (i - 8) * 32);
        palettes[0].entries[i] = blend(dim, c, ramp);
      }
    } else {
      const uint32_t *user_pal = getPaletteByIndex(instance->_segment.palette);
      // Convert uint32_t palette to CRGBPalette16
      for (int i = 0; i < 16; i++) {
        palettes[0].entries[i] = CRGB(user_pal[i]);
      }
    }
  }

  // Render: Perlin noise mapped to palette — WLED exact
  for (int i = 0; i < len; i++) {
    uint8_t index = inoise8(i * scale, instance->_segment.aux0 + i * scale);
    CRGB c = ColorFromPalette(palettes[0], index, 255, LINEARBLEND);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, 0));
  }

  // Organic Y-axis drift — WLED exact
  instance->_segment.aux0 += beatsin8_t(10, 1, 4);

  return FRAMETIME;
}

// --- Chase 2 (ID 28) ---
static uint16_t chase(uint32_t color1, uint32_t color2, uint32_t color3,
                      bool do_palette) {
  uint16_t counter = instance->now * ((instance->_segment.speed >> 2) + 1);
  uint16_t a = (counter * instance->_segment.length()) >> 16;

  unsigned size =
      1 + ((instance->_segment.intensity * instance->_segment.length()) >> 10);

  uint16_t b = a + size;
  if (b > instance->_segment.length())
    b -= instance->_segment.length();
  uint16_t c = b + size;
  if (c > instance->_segment.length())
    c -= instance->_segment.length();

  if (do_palette) {
    for (unsigned i = 0; i < instance->_segment.length(); i++) {
      uint32_t col = instance->_segment.color_from_palette(i, true, true, 0);
      instance->_segment.setPixelColor(i, col);
    }
  } else {
    instance->_segment.fill(color1);
  }

  if (a < b) {
    for (unsigned i = a; i < b; i++)
      instance->_segment.setPixelColor(i, color2);
  } else {
    for (unsigned i = a; i < instance->_segment.length(); i++)
      instance->_segment.setPixelColor(i, color2);
    for (unsigned i = 0; i < b; i++)
      instance->_segment.setPixelColor(i, color2);
  }

  if (b < c) {
    for (unsigned i = b; i < c; i++)
      instance->_segment.setPixelColor(i, color3);
  } else {
    for (unsigned i = b; i < instance->_segment.length(); i++)
      instance->_segment.setPixelColor(i, color3);
    for (unsigned i = 0; i < c; i++)
      instance->_segment.setPixelColor(i, color3);
  }

  return FRAMETIME;
}

uint16_t mode_chase_color(void) {
  return chase(instance->_segment.colors[1],
               (instance->_segment.colors[2]) ? instance->_segment.colors[2]
                                              : instance->_segment.colors[0],
               instance->_segment.colors[0], true);
}

// --- BPM Effect (ID 68) ---
// Colored stripes pulsing at a defined BPM
uint16_t mode_bpm(void) {
  // Tuned Port: Frame-Synced Color Shift
  // Uses aux0 to advance stp by exactly 1 unit per frame.
  // Eliminates time-aliasing vibration. Speed matches WLED (60 vs 51
  // steps/sec).
  if (instance->_segment.call == 0)
    instance->_segment.aux0 = 0;

  uint32_t stp = (instance->_segment.aux0++) & 0xFF;
  uint8_t beat = cfx::beatsin8_t(instance->_segment.speed, 64, 255);
  uint16_t len = instance->_segment.length(); // Cache length

  // Explicit 32-bit math to avoid 16-bit overflow on long strips
  for (unsigned i = 0; i < len; i++) {
    uint32_t col = instance->_segment.color_from_palette(
        stp + ((uint32_t)i * 2), false, true, 0,
        beat - stp + ((uint32_t)i * 10));
    instance->_segment.setPixelColor(i, col);
  }
  return FRAMETIME;
}

// --- Glitter (ID 87) ---
// Two-pass: Inverted Palette Background + Random White Sparks (No Fading)
uint16_t mode_glitter(void) {
  if (!instance)
    return 350;

  // Pass 1: Background - Inverted Palette Fill
  // Directions: "Fill the entire strip with a rainbow gradient... Subtracting
  // time moves it backwards" Use active palette (defaulting to Rainbow if none
  // selected or if default/0 is selected)
  const uint32_t *active_palette =
      (instance->_segment.palette == 0)
          ? getPaletteByIndex(4) // Force Rainbow (ID 4) as default
          : getPaletteByIndex(instance->_segment.palette);

  // Time base for scrolling
  // Speed factor: standard WLED-like scaling
  uint16_t counter =
      (instance->now * ((instance->_segment.speed >> 3) + 1)) & 0xFFFF;

  uint16_t len = instance->_segment.length();

  for (unsigned i = 0; i < len; i++) {
    // Math: colorIndex = (Position - Time) -> Moves "Backwards"
    // (i * 255 / len) scales index to full 0-255 palette range across strip
    uint8_t colorIndex = (i * 255 / len) - (counter >> 8);

    // Render from palette
    CRGBW c = ColorFromPalette(colorIndex, 255, active_palette);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
  }

  // Pass 2: The Glitter (Overlay)
  // "Randomly blast specific pixels with Pure White... if (random8() <
  // intensity)" No fading logic. Next frame overwrites it.
  // User asked for ~10% more glitter.
  // We use (intensity + intensity/8) ~= intensity * 1.125
  if (cfx::hw_random8() <
      (instance->_segment.intensity + (instance->_segment.intensity >> 3))) {
    uint16_t pos = cfx::hw_random16(0, len);
    instance->_segment.setPixelColor(pos, 0xFFFFFFFF); // Pure White (RGBW)
  }

  return FRAMETIME;
}

// --- Tricolor Chase (ID 54) ---
// Simplified to 2-band chase: primary color + palette
uint16_t mode_tricolor_chase(void) {
  uint32_t cycleTime = 50 + ((255 - instance->_segment.speed) << 1);
  uint32_t it = instance->now / cycleTime;
  unsigned width = (1 + (instance->_segment.intensity >> 4)); // 1-16
  unsigned index = it % (width * 2);                          // 2 bands

  for (unsigned i = 0; i < instance->_segment.length(); i++, index++) {
    if (index > (width * 2) - 1)
      index = 0;

    uint32_t color;
    if (index > width - 1)
      color = instance->_segment.color_from_palette(i, true, true,
                                                    1); // palette
    else
      color = instance->_segment.colors[0]; // primary (solid)

    instance->_segment.setPixelColor(instance->_segment.length() - i - 1,
                                     color);
  }
  return FRAMETIME;
}

// --- Sunrise Effect (ID 104) ---
// Gradual sunrise/sunset simulation
uint16_t mode_sunrise(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Get palette (default is Fire/Heat)
  const uint32_t *active_palette;
  if (instance->_segment.palette == 0) {
    active_palette = PaletteHeatColors; // Fire palette for sunrise
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  // Calculate stage (0-65535) based on speed
  uint16_t stage;
  uint8_t speed = instance->_segment.speed;

  if (speed > 120) {
    // Quick breathing mode
    uint32_t counter = (instance->now >> 1) * (((speed - 120) >> 1) + 1);
    stage = triwave16(counter);
  } else if (speed == 0) {
    // Static full sun
    stage = 0xFFFF;
  } else {
    // Slow sunrise/sunset based on time
    uint32_t elapsed = (instance->now - instance->_segment.step) / 100;
    uint8_t durMins = speed;
    if (durMins > 60)
      durMins -= 60;
    uint32_t target = durMins * 600;
    if (elapsed > target)
      elapsed = target;
    stage = (elapsed * 65535) / target;
    if (speed > 60)
      stage = 65535 - stage; // Sunset
  }

  // Draw sun from center outward
  for (int i = 0; i <= len / 2; i++) {
    uint16_t wave = triwave16((i * stage) / len);
    wave = (wave >> 8) + ((wave * instance->_segment.intensity) >> 15);

    uint8_t colorIndex = (wave > 240) ? 240 : wave;
    CRGBW c = ColorFromPalette(colorIndex, 255, active_palette);

    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
    instance->_segment.setPixelColor(len - i - 1, RGBW32(c.r, c.g, c.b, c.w));
  }

  return FRAMETIME;
}

/*
 * Sparkle (ID 20)
 * Random pixels flash the primary color on a darkened background.
 * Refactored: Subtractive Fade (qsub) via getSubFactor to fix "stuck pixels".
 */
uint16_t mode_sparkle(void) {
  // 1. Initialization
  if (instance->_segment.reset) {
    instance->_segment.fill(instance->_segment.colors[1]);
    instance->_segment.reset = false;
  }

  // 2. Timing & Gamma Correct Subtractive Fade
  uint32_t delta = instance->frame_time;

  // Calculate Base Subtraction Amount
  // Speed 0 -> Slow (e.g. 2 units/frame). Speed 255 -> Fast (255 units/frame).
  // Linear mapping often feels better for subtractive fade.
  // We want range approx [2, 255].

  uint16_t sub_base = 2 + instance->_segment.speed;

  // Scale by Delta Time (normalize to 20ms)
  sub_base = (sub_base * delta) / 20;

  // Bounds check before cast
  uint8_t sub_8 = (sub_base > 255) ? 255 : (uint8_t)sub_base;

  // Apply Gamma Correction to Subtraction Factor
  // "High gamma compresses low end, so subtract less"
  uint8_t corrected_sub = instance->getSubFactor(sub_8);

  // 3. Apply Subtractive Fade (Manually)
  // fadeToBlackBy is multiplicative (can get stuck). qsub is subtractive
  // (guarantees zero).
  int len = instance->_segment.length();
  for (int i = 0; i < len; i++) {
    uint32_t c = instance->_segment.getPixelColor(i);
    // Optimize: only write if nonzero
    if (c != 0) {
      uint8_t r = qsub8(CFX_R(c), corrected_sub);
      uint8_t g = qsub8(CFX_G(c), corrected_sub);
      uint8_t b = qsub8(CFX_B(c), corrected_sub);
      uint8_t w = qsub8(CFX_W(c), corrected_sub);
      instance->_segment.setPixelColor(i, RGBW32(r, g, b, w));
    }
  }

  // 4. Spawning
  // Probability adapted for delta time
  uint32_t chance = (instance->_segment.intensity * delta) / 20;
  if (cfx::hw_random16(0, 255) < chance) {
    uint16_t index = cfx::hw_random16(0, len);
    uint32_t color = instance->_segment.colors[0];
    if (instance->_segment.palette != 0 && instance->_segment.palette != 255) {
      uint8_t colorIndex = cfx::hw_random8();
      color = instance->_segment.color_from_palette(colorIndex, true, false, 0,
                                                    255);
    }
    instance->_segment.setPixelColor(index, color);
  }

  return FRAMETIME;
}

/*
 * Flash Sparkle (ID 21) - "Sparkle Dark"
 * Inverted: Background is lit (primary color), sparkles are black (or
 * secondary).
 */
uint16_t mode_flash_sparkle(void) {
  if (instance->_segment.reset) {
    instance->_segment.fill(instance->_segment.colors[0]);
    instance->_segment.reset = false;
  }

  // Effect Logic:
  // Re-fill background every frame. No fade needed (it's "Flash").
  instance->_segment.fill(instance->_segment.colors[0]);

  // Spawning - Time Scaled
  uint32_t delta = instance->frame_time;
  uint32_t threshold = (instance->_segment.speed * delta) / 20;

  if (cfx::hw_random16(0, 255) < threshold) {
    if (cfx::hw_random8() < instance->_segment.intensity) {
      uint16_t index = cfx::hw_random16(0, instance->_segment.length());
      instance->_segment.setPixelColor(index, instance->_segment.colors[1]);
    }
  }

  return FRAMETIME;
}

/*
 * Hyper Sparkle (ID 22) - "Sparkle+"
 * Intense, fast sparkles.
 */
uint16_t mode_hyper_sparkle(void) {
  uint32_t delta = instance->frame_time;

  if (instance->_segment.reset) {
    instance->_segment.fill(instance->_segment.colors[1]);
    instance->_segment.reset = false;
  }

  // Subtractive Fade for Hyper Sparkle too
  // Base subtract: Faster than normal sparkle. Start at 20?
  uint16_t sub_base = 20 + (instance->_segment.speed);

  // Scale / Normalize
  sub_base = (sub_base * delta) / 20;
  uint8_t sub_8 = (sub_base > 255) ? 255 : (uint8_t)sub_base;

  // Gamma Correct
  uint8_t corrected_sub = instance->getSubFactor(sub_8);

  // Apply Loop
  int len = instance->_segment.length();
  for (int i = 0; i < len; i++) {
    uint32_t c = instance->_segment.getPixelColor(i);
    if (c != 0) {
      uint8_t r = qsub8(CFX_R(c), corrected_sub);
      uint8_t g = qsub8(CFX_G(c), corrected_sub);
      uint8_t b = qsub8(CFX_B(c), corrected_sub);
      uint8_t w = qsub8(CFX_W(c), corrected_sub);
      instance->_segment.setPixelColor(i, RGBW32(r, g, b, w));
    }
  }

  // Spawn Logic
  uint16_t max_sparks = (len / 5) + 1;
  uint16_t count = (instance->_segment.intensity * max_sparks) / 255;
  if (count == 0 && instance->_segment.intensity > 0)
    count = 1;

  for (int i = 0; i < count; i++) {
    uint16_t index = cfx::hw_random16(0, len);
    uint32_t color = instance->_segment.colors[0];
    if (instance->_segment.palette != 0 && instance->_segment.palette != 255) {
      color = instance->_segment.color_from_palette(index, true, false, 0, 255);
    }
    instance->_segment.setPixelColor(index, color);
  }

  return FRAMETIME;
}

// --- Rainbow/Colorloop Effects (ID 8, 9) ---
// Ported from WLED FX.cpp

// ID 8: Colorloop - Entire strip cycles through one color
// Intensity controls saturation (blends with white)
uint16_t mode_rainbow(void) {
  if (!instance)
    return 350;

  // === WLED-FAITHFUL TIMING using centralized helper ===
  // Use segment.step as per-instance timing state (avoids static variable
  // cross-contamination)
  auto timing = cfx::calculate_frame_timing(instance->_segment.speed,
                                            instance->_segment.step);

  // Speed controls cycling rate via scaled_now (>>4 for 2x slower)
  uint32_t counter = (timing.scaled_now >> 4) & 0xFF;

  // Get color from palette (Rainbow as default)
  const uint32_t *active_palette =
      (instance->_segment.palette == 0)
          ? getPaletteByIndex(4) // Rainbow palette
          : getPaletteByIndex(instance->_segment.palette);

  CRGBW c = ColorFromPalette(counter, 255, active_palette);

  // Intensity < 128: blend with white (reduce saturation)
  if (instance->_segment.intensity < 128) {
    uint8_t whiteMix = 128 - instance->_segment.intensity;
    // Blend toward white: new = old + (255-old) * mix / 128
    c.r = c.r + (((255 - c.r) * whiteMix) >> 7);
    c.g = c.g + (((255 - c.g) * whiteMix) >> 7);
    c.b = c.b + (((255 - c.b) * whiteMix) >> 7);
  }

  instance->_segment.fill(RGBW32(c.r, c.g, c.b, c.w));
  return FRAMETIME;
}

// ID 9: Rainbow - Per-pixel rainbow across strip
// Intensity controls spatial density (exponential scaling)
uint16_t mode_rainbow_cycle(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();

  // === WLED-FAITHFUL TIMING using centralized helper ===
  // Use segment.step as per-instance timing state (avoids static variable
  // cross-contamination)
  auto timing = cfx::calculate_frame_timing(instance->_segment.speed,
                                            instance->_segment.step);

  // Speed controls animation flow via scaled_now (>>4 for 2x slower)
  uint32_t counter = (timing.scaled_now >> 4) & 0xFF;

  // Intensity controls spatial density (exponential: 16 << (intensity/29))
  uint16_t spatial_mult = 16 << (instance->_segment.intensity / 29);

  // Get palette (Rainbow as default)
  const uint32_t *active_palette =
      (instance->_segment.palette == 0)
          ? getPaletteByIndex(4) // Rainbow palette
          : getPaletteByIndex(instance->_segment.palette);

  for (int i = 0; i < len; i++) {
    uint8_t index = ((i * spatial_mult) / len) + counter;
    CRGBW c = ColorFromPalette(index, 255, active_palette);

    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
  }

  return FRAMETIME;
}

// --- Colortwinkle Effect (ID 74) ---
// Simplified twinkle: fade all toward black, spawn only on dark pixels
// NO allocateData - avoids freeze issue
// Speed = Fade speed, Intensity = Spawn rate
uint16_t mode_colortwinkle(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 0)
    return mode_static();

  // Handle Reset
  if (instance->_segment.reset) {
    instance->_segment.fill(0);
    instance->_segment.reset = false;
  }

  // Speed controls fade rate using scale8 (multiplicative)
  // Lower speed = slower fade (higher retention), higher speed = faster
  // fade
  uint8_t speed = instance->_segment.speed;
  // fade_scale: 248-230 range for calmer twinkle
  // At speed=0: scale=248 (~97% retention = very slow fade, long trails)
  // At speed=128: scale=239 (~94% retention = medium fade)
  // At speed=255: scale=230 (~90% retention = faster fade)
  uint8_t fade_scale = 248 - (speed >> 4); // 248 to ~232 range

  // Get palette - use Rainbow (index 4) as default when palette=0
  const uint32_t *active_palette =
      (instance->_segment.palette == 0)
          ? getPaletteByIndex(4) // Rainbow palette default
          : getPaletteByIndex(instance->_segment.palette);

  // Step 1: Fade ALL pixels toward black using linear subtraction (qsub8)
  // Logic: Linear fade ensures pixels strictly reach zero, avoiding "floor
  // level" artifacts Speed controls fade rate: Speed 0-128 -> fade 8
  // units/frame (Clean fade, avoids floor) Speed >128  -> increases
  // slightly to max ~12 units/frame
  uint8_t fade_amt = 8 + (instance->_segment.speed > 128
                              ? (instance->_segment.speed - 128) >> 5
                              : 0);

  for (int i = 0; i < len; i++) {
    uint32_t cur32 = instance->_segment.getPixelColor(i);
    uint8_t r = (cur32 >> 16) & 0xFF;
    uint8_t g = (cur32 >> 8) & 0xFF;
    uint8_t b = cur32 & 0xFF;

    // Linear subtractive fade
    r = qsub8(r, fade_amt);
    g = qsub8(g, fade_amt);
    b = qsub8(b, fade_amt);

    instance->_segment.setPixelColor(i, RGBW32(r, g, b, 0));
  }

  // Step 2: Spawn new twinkles
  // Map intensity effectively: 0-255 -> spawn chance
  // Boost intensity to match WLED visual density (128 -> ~150)
  // Increase loop count for higher max density (len/40)
  int spawnLoops = (len / 40) + 1;
  uint8_t intensity = qadd8(instance->_segment.intensity, 22);

  for (int j = 0; j < spawnLoops; j++) {
    // Unconditional spawning based on intensity - no "dark pixel" check to
    // avoid deadlocks
    if (hw_random8() <= intensity) {
      int i = hw_random16(0, len);
      CRGBW c = ColorFromPalette(hw_random8(), 255, active_palette);
      instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, 0));
    }
  }

  return FRAMETIME;
}

// --- Scanner Effect (ID 40) ---
// Larson Scanner: linear scanning eye with fading trail
// Speed = Scan frequency, Intensity = Trail length
// dualMode = if true, paint a second eye on opposite side (ID 60)
//
// Based on WLED mode_larson_scanner() by Aircoookie
// Explicit trail rendering — gamma-aware, with direction-change memory
uint16_t mode_scanner_internal(bool dualMode) {
  if (!instance)
    return 350;

  uint16_t len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // State layout:
  //   aux0  = direction (0=forward, 1=backward)
  //   aux1  = current pixel position (internal 0..len-1)
  //   step  = frame counter (sub-pixel mode + old trail fade)
  //   Upper 16 bits of step: old trail fade counter
  //   data[0..3]: old_dir(1), old_pos(2), unused(1)

  // 1. Reset
  if (instance->_segment.reset) {
    instance->_segment.fill(0);
    instance->_segment.aux0 = 0;
    instance->_segment.aux1 = 0;
    instance->_segment.step = 0;
    // Allocate 4 bytes for old trail data
    if (!instance->_segment.allocateData(4))
      return mode_static();
    instance->_segment.data[0] = 0; // old_dir
    instance->_segment.data[1] = 0; // old_pos low
    instance->_segment.data[2] = 0; // old_pos high
    instance->_segment.data[3] = 0; // old trail active (0=no)
    instance->_segment.reset = false;
  }

  // Ensure data is allocated
  if (instance->_segment.data == nullptr)
    return mode_static();

  // 2. Movement: WLED speed mapping
  uint8_t spd = instance->_segment.speed;
  unsigned speed_factor = 96 - ((unsigned)spd * 94 / 255); // 96→2
  unsigned effective_speed = FRAMETIME * speed_factor;
  unsigned pixels = len / effective_speed;

  bool did_advance = false;
  uint16_t frame_count = instance->_segment.step & 0xFFFF;

  if (pixels == 0) {
    unsigned frames_per_pixel = effective_speed / len;
    if (frames_per_pixel == 0)
      frames_per_pixel = 1;
    frame_count++;
    if (frame_count >= frames_per_pixel) {
      frame_count = 0;
      pixels = 1;
      did_advance = true;
    }
    instance->_segment.step =
        (instance->_segment.step & 0xFFFF0000) | frame_count;
  } else {
    did_advance = true;
  }

  if (did_advance) {
    unsigned index = instance->_segment.aux1 + pixels;
    if (index >= (unsigned)len) {
      // Save current trail as "old trail" before reversing
      instance->_segment.data[0] = instance->_segment.aux0;
      instance->_segment.data[1] = instance->_segment.aux1 & 0xFF;
      instance->_segment.data[2] = (instance->_segment.aux1 >> 8) & 0xFF;
      instance->_segment.data[3] = 1; // old trail is active

      // RESET the old trail age counter (upper 16 bits)
      instance->_segment.step &= 0xFFFF;

      // Reverse
      instance->_segment.aux0 = !instance->_segment.aux0;
      instance->_segment.aux1 = 0;
    } else {
      instance->_segment.aux1 = index;
    }
  }

  // 3. Trail length from Intensity
  //    mapped to [3 ... len/3], or infinite if 255
  unsigned trail_len;
  uint8_t intensity = instance->_segment.intensity;
  if (intensity >= 255) {
    trail_len = len;
  } else {
    // scale to max 33% of strip length for better control
    unsigned max_len = (len > 6) ? (len / 3) : len;
    trail_len =
        3 + ((unsigned)intensity * (max_len > 3 ? max_len - 3 : 0)) / 255;
    if (trail_len > (unsigned)len)
      trail_len = len;
  }

  // 4. Clear and render
  instance->_segment.fill(0);

  // Helper: Draw pixel with MAX blending (keep brighter color)
  auto drawPixelMax = [&](unsigned pos, uint32_t c) {
    uint32_t existing = instance->_segment.getPixelColor(pos);
    uint8_t r = CFX_R(c), g = CFX_G(c), b = CFX_B(c), w = CFX_W(c);
    uint8_t er = CFX_R(existing), eg = CFX_G(existing);
    uint8_t eb = CFX_B(existing), ew = CFX_W(existing);
    r = (r > er) ? r : er;
    g = (g > eg) ? g : eg;
    b = (b > eb) ? b : eb;
    w = (w > ew) ? w : ew;
    instance->_segment.setPixelColor(pos, RGBW32(r, g, b, w));
  };

  // Helper lambda to draw a trail at a given position/direction
  auto drawTrail = [&](unsigned headPos, bool dir, unsigned tLen,
                       uint8_t maxBri) {
    for (unsigned t = 0; t < tLen && t <= headPos; t++) {
      unsigned internalPos = headPos - t;
      unsigned displayPos = dir ? internalPos : (len - 1 - internalPos);

      // Quadratic brightness: (1 - t/trail_len)^2 * maxBri
      // Quadratic naturally compensates for gamma (~2.8) reasonably well
      uint8_t bri;
      if (t == 0) {
        bri = maxBri;
      } else {
        unsigned fade = 255 - (t * 255 / tLen);
        // Quadratic: fade^2 / 255, scaled by maxBri
        // GAMMA CORRECTION: Replace x*x with LUT
        // bri = ((fade * fade) >> 8) * maxBri / 255;
        bri = ((uint16_t)instance->applyGamma(fade) * maxBri) >> 8;
        if (bri == 0 && t < tLen && maxBri > 0)
          bri = 1;
      }

      uint32_t c;
      if (instance->_segment.palette == 0 ||
          instance->_segment.palette == 255) {
        c = instance->_segment.colors[0];
      } else {
        // Map spatial position to palette index (0-255)
        // color_from_palette expects 0-255 input
        uint16_t palIndex = (displayPos * 255) / (len - 1);
        c = instance->_segment.color_from_palette(palIndex, true, true, 0);
      }

      uint8_t r = ((CFX_R(c)) * bri) >> 8;
      uint8_t g = ((CFX_G(c)) * bri) >> 8;
      uint8_t b = ((CFX_B(c)) * bri) >> 8;
      uint8_t w = ((CFX_W(c)) * bri) >> 8;
      uint32_t finalColor = RGBW32(r, g, b, w);

      drawPixelMax(displayPos, finalColor);
      if (dualMode) {
        drawPixelMax(len - 1 - displayPos, finalColor);
      }
    }
  };

  // Draw current trail at full brightness
  drawTrail(instance->_segment.aux1, instance->_segment.aux0, trail_len, 255);

  // Draw old trail (from before direction change) with fading brightness
  if (instance->_segment.data[3]) {
    unsigned oldPos = instance->_segment.data[1] |
                      ((unsigned)instance->_segment.data[2] << 8);
    bool oldDir = instance->_segment.data[0];
    // Fade old trail over frames
    uint16_t oldAge = (instance->_segment.step >> 16) & 0xFFFF;
    oldAge++;
    instance->_segment.step =
        (instance->_segment.step & 0xFFFF) | ((uint32_t)oldAge << 16);

    // Dynamic fade duration based on speed:
    // Faster speed = shorter duration (trail must clear faster)
    // Slower speed = longer duration (trail lingers)
    // Formula: (trail_len * speed_factor) / 3 scales perfectly with travel
    // time
    unsigned fadeFrames = (trail_len * speed_factor) / 3;
    if (fadeFrames < 5)
      fadeFrames = 5;

    if (oldAge < fadeFrames) {
      uint8_t oldBri = 255 - (oldAge * 255 / fadeFrames);
      drawTrail(oldPos, oldDir, trail_len, oldBri);
    } else {
      instance->_segment.data[3] = 0; // old trail fully faded
    }
  }

  return FRAMETIME;
}

// Wrapper for single scanner (ID 40)
// Wrapper for single scanner (ID 40)
uint16_t mode_scanner_internal(bool dual);
uint16_t mode_scanner(void) { return mode_scanner_internal(false); }

// Dual Scanner (ID 60)
// Two scanners moving in opposite directions
uint16_t mode_scanner_dual(void) { return mode_scanner_internal(true); }

// (Duplicate scanner implementation removed)

// Mode Table
// --- Service Loop with Switch Dispatch ---
uint16_t mode_bouncing_balls(void);
uint16_t mode_color_wipe(void);
uint16_t mode_color_wipe_random(void);
uint16_t mode_color_sweep(void);
uint16_t mode_strobe(void);

void CFXRunner::service() {
  // CRITICAL FIX: Update global instance pointer to 'this' runner
  // Ensures effect functions operate on the correct strip context
  instance = this;

  // Start frame diagnostics (measures time since last call)
  diagnostics.frame_start();

  now = cfx_millis();

  // Calculate frame time (delta) - using member variables, not static
  frame_time = now - _last_frame;
  _last_frame = now;

  // Increment call counter for effect initialization logic
  _segment.call++;

  // Perform periodic logging if enabled
  diagnostics.maybe_log(_name);

  // --- INTRO LOGIC ---
  if (_state == STATE_INTRO) {
    if (serviceIntro()) {
      _state = STATE_RUNNING;
      // Intro just finished.
      // We let the next loop iteration handle the main effect start to
      // ensure clean state.
    }
    return;
  }

  // Dispatch via Switch for reliability
  switch (_mode) {
  case FX_MODE_RAINBOW: // 8
    mode_rainbow();
    break;
  case FX_MODE_CHASE_COLOR: // 28
    mode_chase_color();
    break;
  case FX_MODE_TRICOLOR_CHASE: // 54
    mode_tricolor_chase();
    break;
  case FX_MODE_BPM: // 68
    mode_bpm();
    break;
  case FX_MODE_GLITTER: // 87
    mode_glitter();
    break;
  case FX_MODE_RAINBOW_CYCLE: // 9
    mode_rainbow_cycle();
    break;
  case FX_MODE_AURORA: // 38
    mode_aurora();
    break;
  case FX_MODE_SCANNER: // 40
    mode_scanner();
    break;
  case FX_MODE_SCANNER_DUAL: // 60
    mode_scanner_dual();
    break;
  case FX_MODE_FIRE_2012: // 66
    mode_fire_2012();
    break;
  case FX_MODE_FIRE_DUAL: // 53
    mode_fire_dual();
    break;
  case FX_MODE_COLORTWINKLE: // 74
    mode_colortwinkle();
    break;
  case FX_MODE_PLASMA: // 97
    mode_plasma();
    break;
  case FX_MODE_OCEAN: // 101 (was Pacifica)
    mode_ocean();
    break;
  case FX_MODE_PRIDE_2015: // 63
    mode_pride_2015();
    break;
  case FX_MODE_BREATH: // 2
    mode_breath();
    break;
  case FX_MODE_DISSOLVE: // 18
    mode_dissolve();
    break;
  case FX_MODE_JUGGLE: // 64
    mode_juggle();
    break;
  case FX_MODE_RIPPLE: // 79
    mode_ripple();
    break;
  case FX_MODE_PHASED: // 105
    mode_phased();
    break;
  case FX_MODE_FLOW: // 110
    mode_flow();
    break;
  case FX_MODE_METEOR: // 76
    mode_meteor();
    break;
  case FX_MODE_SPARKLE: // 20
    mode_sparkle();
    break;
  case FX_MODE_FLASH_SPARKLE: // 21
    mode_flash_sparkle();
    break;
  case FX_MODE_HYPER_SPARKLE: // 22
    mode_hyper_sparkle();
    break;
  case FX_MODE_NOISEPAL: // 107
    mode_noisepal();
    break;
  case FX_MODE_COLOR_WIPE: // 3
    mode_color_wipe();
    break;
  case FX_MODE_COLOR_WIPE_RANDOM: // 4
    mode_color_wipe_random();
    break;
  case FX_MODE_COLOR_SWEEP: // 6
    mode_color_sweep();
    break;
  case FX_MODE_SUNRISE: // 104
    mode_sunrise();
    break;
  case FX_MODE_BOUNCINGBALLS: // 91
    mode_bouncing_balls();
    break;
  case FX_MODE_BLINK: // 1
    mode_blink();
    break;
  case FX_MODE_STROBE: // 23
    mode_strobe();
    break;
  case FX_MODE_STROBE_RAINBOW: // 24
    mode_strobe_rainbow();
    break;
  case FX_MODE_MULTI_STROBE: // 25
    mode_multi_strobe();
    break;
  case FX_MODE_BLINK_RAINBOW: // 26
    mode_blink_rainbow();
    break;
  case FX_MODE_RUNNING_LIGHTS: // 15
    mode_running_lights();
    break;
  case FX_MODE_SAW: // 16
    mode_saw();
    break;
  case FX_MODE_RUNNING_DUAL: // 52
    mode_running_dual();
    break;
  default:
    mode_static();
    break;
  }
}

// --- Bouncing Balls Effect (ID 91) ---
// Real Gravity Physics Engine

#define MAX_BALLS 8

struct BouncingBall {
  float impactVelocity;           // v0 (upward velocity after bounce)
  float height;                   // Current height
  uint32_t clockTimeAtLastBounce; // Timestamp
  float dampening;                // Energy retention (0.0 - 1.0)
};

uint16_t mode_bouncing_balls(void) {
  if (!instance)
    return 350;

  // Allocate State
  if (!instance->_segment.allocateData(sizeof(BouncingBall) * MAX_BALLS)) {
    return mode_static();
  }
  BouncingBall *balls =
      reinterpret_cast<BouncingBall *>(instance->_segment.data);

  // Initialize/Reset
  if (instance->_segment.reset) {
    for (int i = 0; i < MAX_BALLS; i++) {
      balls[i].clockTimeAtLastBounce = instance->now;
      balls[i].height = 0;
      balls[i].impactVelocity = 0; // Will be set by re-injection
      balls[i].dampening = 0.90f;
    }
    instance->_segment.fill(0);
    instance->_segment.reset = false;
  }

  instance->_segment.fadeToBlackBy(
      160); // Faster fade for shorter trails (WLED-like)

  // Physics Constants
  // Gravity -18.0 for snappy "real" feel (less floaty)
  const float GRAVITY = -18.0f;
  const float MAX_HEIGHT = 1.0f; // Normalized top of strip

  // Calculate Launch Velocity needed to reach MAX_HEIGHT
  // v = sqrt(2 * |g| * h)
  // v = sqrt(2 * 18 * 1) = 6.0
  const float V_MAX = 6.0f; // sqrt(36)

  // Controls
  uint8_t numBalls = (instance->_segment.intensity * (MAX_BALLS - 1)) / 255 + 1;

  // Speed -> Simulation Speed (Time Scale)
  // User Feedback: "Speed 25-30 is good".
  // Target: We want 128 to equal ~0.36.
  // 128 / 350.0f ~= 0.365.
  float speedFactor = instance->_segment.speed / 350.0f;

  for (int i = 0; i < numBalls; i++) {
    float time_sec = (instance->now - balls[i].clockTimeAtLastBounce) /
                     1000.0f * speedFactor;

    float h = balls[i].impactVelocity * time_sec +
              0.5f * GRAVITY * time_sec * time_sec;

    if (h <= 0) {
      h = 0;
      balls[i].impactVelocity *= balls[i].dampening;

      // Energy Re-injection
      // Inject energy if velocity drops too low (dead ball)
      if (balls[i].impactVelocity < 2.0f) {
        // Randomize nicely between 80% and 105% of max height energy
        // Range: ~0.8 * 6.0 (4.8) to ~1.05 * 6.0 (6.3)
        // This ensures they reach the top but vary a bit
        float energyMult = 0.8f + ((rand() % 25) / 100.0f);
        balls[i].impactVelocity = V_MAX * energyMult;

        balls[i].dampening = 0.90f + ((rand() % 10) / 100.0f);
      }

      balls[i].clockTimeAtLastBounce = instance->now;
    }
    balls[i].height = h;

    // Draw Ball
    // Map Normalized Height 0.0-1.0 to Strip Length
    int pixel = (int)(h * (instance->_segment.length() - 1));

    // Bounds Check
    if (pixel >= instance->_segment.length())
      pixel = instance->_segment.length() - 1;
    if (pixel < 0)
      pixel = 0;

    // Color Logic
    const uint32_t *active_palette;
    if (instance->_segment.palette == 255 || instance->_segment.palette == 0) {
      // Default (0) or Explicit Solid (255) -> Use Primary Color
      fillSolidPalette(instance->_segment.colors[0]);
      active_palette = PaletteSolid;
    } else {
      active_palette = getPaletteByIndex(instance->_segment.palette);
    }

    CRGBW c = ColorFromPalette(i * (256 / MAX_BALLS), 255, active_palette);
    uint32_t colorInt = RGBW32(c.r, c.g, c.b, c.w);

    uint32_t existing = instance->_segment.getPixelColor(pixel);
    uint8_t r = qadd8((existing >> 16) & 0xFF, (colorInt >> 16) & 0xFF);
    uint8_t g = qadd8((existing >> 8) & 0xFF, (colorInt >> 8) & 0xFF);
    uint8_t b = qadd8(existing & 0xFF, colorInt & 0xFF);

    instance->_segment.setPixelColor(pixel, RGBW32(r, g, b, 0));
  }

  return FRAMETIME;
}

// --- Running Effects (ID 15, 16) ---

/*
 * Running lights effect with smooth sine transition base.
 */
/*
 * Blink/strobe function
 * Alternate between color1 and color2
 * if(strobe == true) then create a strobe effect
 */
uint16_t blink(uint32_t color1, uint32_t color2, bool strobe, bool do_palette) {
  uint32_t cycleTime = (255 - instance->_segment.speed) * 20;
  uint32_t onTime = FRAMETIME;
  if (!strobe)
    onTime += ((cycleTime * instance->_segment.intensity) >> 8);
  cycleTime += FRAMETIME * 2;
  uint32_t it = instance->now / cycleTime;
  uint32_t rem = instance->now % cycleTime;

  bool on = false;
  if (it != instance->_segment.step // new iteration, force on state for one
                                    // frame, even if set time is too brief
      || rem <= onTime) {
    on = true;
  }

  instance->_segment.step = it; // save previous iteration

  uint32_t color = on ? color1 : color2;
  if (color == color1 && do_palette && instance->_segment.palette != 0 &&
      instance->_segment.palette != 255) {
    for (unsigned i = 0; i < instance->_segment.length(); i++) {
      // We use colors[0] vs colors[1] logic above but if do_palette is true,
      // we ignore color1 and use the palette color.
      // WLED logic: SEGMENT.color_from_palette(i, true, PALETTE_SOLID_WRAP, 0)
      // Since we lack simple palette helper in this scope, we use manual:
      const uint32_t *active_palette =
          getPaletteByIndex(instance->_segment.palette);
      // PALETTE_SOLID_WRAP means wrap, we use standard logic
      uint16_t len = instance->_segment.length();
      CRGBW c = ColorFromPalette((i * 255) / len, 255, active_palette);
      instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
    }
  } else {
    instance->_segment.fill(color);
  }

  return FRAMETIME;
}

/*
 * Normal blinking. Intensity sets duty cycle.
 */
uint16_t mode_blink(void) {
  return blink(instance->_segment.colors[0], instance->_segment.colors[1],
               false, true);
}

/*
 * Classic Blink effect. Cycling through the rainbow.
 */
uint16_t mode_blink_rainbow(void) {
  return blink(cfx::color_wheel(instance->_segment.call & 0xFF),
               instance->_segment.colors[1], false, false);
}

/*
 * Classic Strobe effect.
 * Refined to use stateful timing (aux0/aux1) for stability at high speeds.
 */
uint16_t mode_strobe(void) {
  // 1. Initialization
  if (instance->_segment.reset) {
    instance->_segment.aux1 = 1; // Start ON
    instance->_segment.step = instance->now;
    instance->_segment.aux0 = 20; // Initial ON duration
    instance->_segment.reset = false;
  }

  // 2. State Transition
  if (instance->now - instance->_segment.step > instance->_segment.aux0) {
    instance->_segment.aux1 = !instance->_segment.aux1; // Toggle
    instance->_segment.step = instance->now;

    if (instance->_segment.aux1) {
      // Turning ON
      // Strobe ON time: fixed 20ms for crispness
      instance->_segment.aux0 = 20;
    } else {
      // Turning OFF
      // Speed 255 -> 0ms delay (max speed)
      // Speed 0 -> Slow delay (~1000ms)
      // WLED map: 255-speed * multiplier.
      // 255-0 = 255 * 5 = ~1275ms max delay
      uint32_t delay = (255 - instance->_segment.speed) * 5;
      instance->_segment.aux0 = delay;
    }
  }

  // 3. Rendering
  if (instance->_segment.aux1) {
    // ON State
    uint32_t color = instance->_segment.colors[0];

    // Palette handling with "Primary Color" override for Solid/Default
    // Identical to our blink() fix: if Solid/Default, use Primary Color.
    if (instance->_segment.palette != 0 && instance->_segment.palette != 255) {
      const uint32_t *active_palette =
          getPaletteByIndex(instance->_segment.palette);
      uint16_t len = instance->_segment.length();
      for (unsigned i = 0; i < len; i++) {
        uint8_t colorIndex = (i * 255) / len;
        CRGBW c = ColorFromPalette(colorIndex, 255, active_palette);
        instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
      }
    } else {
      instance->_segment.fill(color);
    }
  } else {
    // OFF State
    instance->_segment.fill(instance->_segment.colors[1]); // Background
  }

  return FRAMETIME;
}

/*
 * Classic Strobe effect. Cycling through the rainbow.
 * Refined to use stateful timing (aux0/aux1) for stability at high speeds.
 */
uint16_t mode_strobe_rainbow(void) {
  // 1. Initialization
  if (instance->_segment.reset) {
    instance->_segment.aux1 = 1; // Start ON
    instance->_segment.step = instance->now;
    instance->_segment.aux0 = 20; // Initial ON duration
    instance->_segment.reset = false;
  }

  // 2. State Transition (Identical to mode_strobe)
  if (instance->now - instance->_segment.step > instance->_segment.aux0) {
    instance->_segment.aux1 = !instance->_segment.aux1; // Toggle
    instance->_segment.step = instance->now;

    // ON: 20ms
    instance->_segment.aux0 = 20;
  } else {
    // OFF: Speed dependent
    // SAFETY FIX: Clamp minimum delay to 10ms to prevent power brownouts
    uint32_t delay = (255 - instance->_segment.speed) * 5;
    if (delay < 10)
      delay = 10;
    instance->_segment.aux0 = delay;
  }

  // 3. Rendering
  if (instance->_segment.aux1) {
    // ON State: Rainbow Color
    // Use (instance->now >> 4) for smooth rainbow cycling over time
    uint32_t color = cfx::color_wheel((instance->now >> 4) & 0xFF);
    instance->_segment.fill(color);
  } else {
    // OFF State
    instance->_segment.fill(instance->_segment.colors[1]);
  }

  return FRAMETIME;
}

/*
 * Multi Strobe logic
 * Refined to match stateful structure and include Primary Color fix.
 */
uint16_t mode_multi_strobe(void) {
  // 1. Initialization
  if (instance->_segment.reset) {
    instance->_segment.aux1 = 1000; // Trigger cycle reset
    instance->_segment.aux0 = 0;    // Next event time
    instance->_segment.reset = false;
  }

  // 2. State Logic
  unsigned count = 2 * ((instance->_segment.intensity / 10) + 1);

  // Rethinking Multi-Strobe State Machine for clarity:
  // aux1: Current Flash Count in Burst (0 to count). Even = ON, Odd = OFF.
  // step: Last Switch Time
  // aux0: Current State Duration

  if (instance->now - instance->_segment.step > instance->_segment.aux0) {
    instance->_segment.aux1++;
    instance->_segment.step = instance->now;

    if (instance->_segment.aux1 <= count) {
      // Inside Burst
      if ((instance->_segment.aux1 & 1) == 0) { // 0, 2, 4... -> ON
        instance->_segment.aux0 = 20;
      } else { // 1, 3, 5... -> OFF (Inter-flash delay)
        instance->_segment.aux0 = 50;
      }
    } else {
      // Burst Done -> Start Long Delay

      // Base Delay
      uint32_t delay = 200 + (255 - instance->_segment.speed) * 10;

      // Randomize delay to restore "Multi Strobe" variance
      delay += cfx::hw_random8();

      instance->_segment.aux0 = delay;
      instance->_segment.aux1 = 0xFFFF; // Reset state (roll to 0 next time)
    }
  }

  // 3. Rendering
  // If aux1 is Even and <= count, we are ON.
  bool isOn =
      ((instance->_segment.aux1 & 1) == 0) && (instance->_segment.aux1 < count);

  if (isOn) {
    uint32_t color = instance->_segment.colors[0];
    if (instance->_segment.palette != 0 && instance->_segment.palette != 255) {
      const uint32_t *active_palette =
          getPaletteByIndex(instance->_segment.palette);
      uint16_t len = instance->_segment.length();
      for (unsigned i = 0; i < len; i++) {
        uint8_t colorIndex = (i * 255) / len;
        CRGBW c = ColorFromPalette(colorIndex, 255, active_palette);
        instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
      }
    } else {
      instance->_segment.fill(color);
    }
  } else {
    instance->_segment.fill(instance->_segment.colors[1]);
  }

  return FRAMETIME;
}

static uint16_t running_base(bool saw, bool dual = false) {
  uint16_t len = instance->_segment.length();
  unsigned x_scale = instance->_segment.intensity >> 2;
  uint32_t counter = (instance->now * instance->_segment.speed) >> 9;

  for (unsigned i = 0; i < len; i++) {
    unsigned a = i * x_scale - counter;
    if (saw) {
      a &= 0xFF;
      if (a < 16) {
        a = 192 + a * 8;
      } else {
        a = cfx::map(a, 16, 255, 64, 192);
      }
      a = 255 - a;
    }
    // WLED logic: dual uses sin_gap, single uses sin8
    uint8_t s = dual ? cfx::sin_gap(a) : cfx::sin8(a);

    // Logic: Blend between Background (colors[1]) and Target (Palette/Color)
    // SEGCOLOR(1) is background in WLED.
    uint32_t color1 = instance->_segment.colors[1];

    uint32_t color2;
    if (instance->_segment.palette == 0 || instance->_segment.palette == 255) {
      color2 = instance->_segment.colors[0];
    } else {
      // Palette mode: use palette color for 'i'
      const uint32_t *active_palette =
          getPaletteByIndex(instance->_segment.palette);
      CRGBW c = ColorFromPalette((i * 255) / len, 255, active_palette);
      color2 = RGBW32(c.r, c.g, c.b, c.w);
    }

    uint32_t ca = color_blend(color1, color2, s);

    if (dual) {
      // Wave B: Use +counter to move in opposite direction (Left)
      unsigned b = i * x_scale + counter;
      uint8_t s2 = cfx::sin_gap(b);
      uint32_t color3;
      if (instance->_segment.palette == 0 ||
          instance->_segment.palette == 255) {
        color3 = instance->_segment
                     .colors[0]; // Use Primary color (fix solid palette hole)
      } else {
        const uint32_t *active_palette =
            getPaletteByIndex(instance->_segment.palette);
        CRGBW c = ColorFromPalette((i * 255) / len + 128, 255, active_palette);
        color3 = RGBW32(c.r, c.g, c.b, c.w);
      }
      ca = color_blend(ca, color3, s2);
    }

    instance->_segment.setPixelColor(i, ca);
  }

  return FRAMETIME;
}

uint16_t mode_running_lights(void) { return running_base(false); }

uint16_t mode_running_dual(void) { return running_base(false, true); }

uint16_t mode_saw(void) { return running_base(true); }

// --- Simple Effects Batch (ID 3, 4, 6, 23) ---

// Helper for Wipe/Sweep
// Helper for Wipe/Sweep
uint16_t color_wipe(bool rev, bool useRandomColors) {
  if (!instance)
    return 350;

  // Force Default Palette (0) to Solid (255) for this effect unless Random
  if (!useRandomColors &&
      (instance->_segment.palette == 0 || instance->_segment.palette == 255)) {
    fillSolidPalette(instance->_segment.colors[0]);
  }

  uint32_t cycleTime = 750 + (255 - instance->_segment.speed) * 150;
  uint32_t perc = instance->now % cycleTime;
  uint16_t prog = (perc * 65535) / cycleTime;
  // State Machine for Color Transitions (WLED Logic)
  bool back = (prog > 32767);
  if (back)
    prog -= 32767; // Normalize prog 0..32767

  if (useRandomColors) {
    // Initialization
    if (instance->_segment.call == 0) {
      instance->_segment.aux0 = rand() % 256;
      instance->_segment.aux1 = rand() % 256;
      instance->_segment.step = back ? 1 : 0;
    }

    // Transition Handling: Shift Colors on phase change
    // Wipe A->B leads to B->C
    if (back && instance->_segment.step == 0) {
      // Changed from Front to Back
      instance->_segment.step = 1;
      instance->_segment.aux1 =
          instance->_segment.aux0;            // Old FG becomes New BG
      instance->_segment.aux0 = rand() % 256; // New FG
    } else if (!back && instance->_segment.step == 1) {
      // Changed from Back to Front
      instance->_segment.step = 0;
      instance->_segment.aux1 = instance->_segment.aux0;
      instance->_segment.aux0 = rand() % 256; // New FG
    }
  } else {
    // Non-random state (just track step if needed for other effects)
    instance->_segment.step = back ? 1 : 0;
  }

  uint32_t len = instance->_segment.length();

  // Robust Smoothness Logic (No shimmering)
  // 1. Calculate precise position in "sub-pixels"
  uint32_t totalPos = (uint32_t)prog * len;
  uint16_t ledIndex = totalPos >> 15;

  // rem is fractional progress (0-255)
  uint8_t rem = (totalPos & 0x7FFF) >> 7;

  // 2. Map Intensity to Fade Width (Softness)
  if (instance->_segment.intensity < 255) {
    uint8_t width = instance->_segment.intensity;
    int16_t lower = 128 - (width / 2);
    int16_t upper = 128 + (width / 2);

    if (rem <= lower)
      rem = 0;
    else if (rem >= upper)
      rem = 255;
    else {
      if (upper > lower) {
        rem = (uint8_t)(((uint16_t)(rem - lower) * 255) / (upper - lower));
      } else {
        rem = (rem > 128) ? 255 : 0;
      }
    }
  }

  const uint32_t *active_palette;
  if (useRandomColors) {
    // Random Mode: Always use Rainbow/Wheel logic (Ignore user selection)
    active_palette = PaletteRainbow;
  } else if (instance->_segment.palette == 255) {
    active_palette = PaletteSolid;
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  // Background Color Logic
  uint32_t col1 = 0; // Default to Black/Off
  if (useRandomColors) {
    CRGBW c1 = ColorFromPalette(instance->_segment.aux1, 255, active_palette);
    col1 = RGBW32(c1.r, c1.g, c1.b, c1.w);
  }

  for (int i = 0; i < len; i++) {
    int index = (rev && back) ? len - 1 - i : i;

    // Foreground Color Construction
    uint32_t col0;
    if (useRandomColors) {
      CRGBW c0 = ColorFromPalette(instance->_segment.aux0, 255, active_palette);
      col0 = RGBW32(c0.r, c0.g, c0.b, c0.w);
    } else if (instance->_segment.palette == 255 ||
               (!useRandomColors && instance->_segment.palette == 0)) {
      col0 = instance->_segment.colors[0]; // Solid Color
    } else {
      // FIX: Use Stretched Palette mapping (i * 255 / len) instead of fixed
      // pattern (i * 12) This fixes "holes" and creates a smooth gradient
      // across the strip.
      uint8_t colorIndex = (i * 255) / len;
      CRGBW c0 = ColorFromPalette(colorIndex, 255, active_palette);
      col0 = RGBW32(c0.r, c0.g, c0.b, c0.w);
    }

    // Blend Logic based on Distance from Wipe Front
    // 1 Pixel = 32768 steps.
    // totalPos is the front position.

    uint32_t pixelPos = (uint32_t)i << 15;
    int32_t dist = (int32_t)(totalPos - pixelPos);

    // Fade Width based on Intensity (0 = Sharp, 255 = ~2 Pixels / 65536
    // steps)
    uint32_t fadeWidth = (instance->_segment.intensity << 8) + 1;

    uint8_t blendVal;
    if (dist <= 0) {
      blendVal = 0; // Completely ahead (Background)
    } else if (dist >= fadeWidth) {
      blendVal = 255; // Completely behind (Foreground)
    } else {
      blendVal = (uint8_t)((dist * 255) / fadeWidth);
    }

    // Applying Blend
    // Logic: Always wipe 'fillCol' over 'baseCol'.
    // Standard/Random Wipe: Fill FG (col0) over BG (col1).
    // Sweep Return (rev && back): Fill BG (col1) over FG (col0) -> "Erase"
    // effect.

    uint32_t fillCol = col0;
    uint32_t baseCol = col1;

    if (rev && back) {
      fillCol = col1;
      baseCol = col0;
    }

    // Always blend fillCol over baseCol
    uint32_t finalColor = color_blend(baseCol, fillCol, blendVal);

    instance->_segment.setPixelColor(index, finalColor);
  }
  return FRAMETIME;
}

uint16_t mode_color_wipe(void) { return color_wipe(false, false); }

uint16_t mode_color_wipe_random(void) { return color_wipe(false, true); }

uint16_t mode_color_sweep(void) { return color_wipe(true, false); }

// --- INTRO IMPLEMENTATION ---

void CFXRunner::startIntro(uint8_t mode, float duration_s, uint32_t color) {
  if (mode == INTRO_NONE) {
    _state = STATE_RUNNING;
    return;
  }

  // Debug log
  // ESP_LOGD("wled_intro", "Starting Intro Mode %d, Dur %.1fs, Color
  // 0x%08X", mode, duration_s, color);

  _state = STATE_INTRO;
  _intro_mode = mode;
  _intro_start_time = cfx_millis(); // Force fresh time
  now = _intro_start_time;          // Sync member var for consistency
  // Safety check for duration (avoid div/0)
  if (duration_s < 0.1f)
    duration_s = 0.1f;
  _intro_duration_ms = (uint32_t)(duration_s * 1000.0f);

  _intro_color = color;

  // Clean start
  _segment.fill(0);
}

bool CFXRunner::serviceIntro() {
  uint32_t elapsed = now - _intro_start_time;

  if (elapsed >= _intro_duration_ms) {
    return true; // Intro Done
  }

  // Normalised progress 0.0 to 1.0
  float progress = (float)elapsed / (float)_intro_duration_ms;
  uint16_t len = _segment.length();

  if (_intro_mode == INTRO_WIPE) {
    // Wipe: Fill linear from 0 to end.
    // Respects Reverse flag of the segment (which comes from config)
    uint16_t limit = (uint16_t)(len * progress);

    // Safety clamp
    if (limit > len)
      limit = len;

    for (int i = 0; i < len; i++) {
      // Handle Reverse Logic
      int idx = _segment.mirror ? (len - 1 - i) : i;

      if (i <= limit) {
        _segment.setPixelColor(idx, _intro_color);
      } else {
        // Keep the rest black
        _segment.setPixelColor(idx, 0);
      }
    }
  } else if (_intro_mode == INTRO_FADE) {
    // Fade: Linear brightness increase
    uint8_t brightness = (uint8_t)(255.0f * progress);

    // Scale color components
    uint8_t r = (CFX_R(_intro_color) * brightness) >> 8;
    uint8_t g = (CFX_G(_intro_color) * brightness) >> 8;
    uint8_t b = (CFX_B(_intro_color) * brightness) >> 8;
    uint8_t w = (CFX_W(_intro_color) * brightness) >> 8;

    _segment.fill(RGBW32(r, g, b, w));
  } else if (_intro_mode == INTRO_GLITTER) {
    // Glitter: Accumulate random pixels
    if ((rand() % 100) < 30) {
      uint16_t pos = rand() % len;
      _segment.setPixelColor(pos, _intro_color);
    }
  } else if (_intro_mode == INTRO_CENTER) {
    // Center Wipe: From center to edges
    uint16_t center = len / 2;
    uint16_t limit = (uint16_t)((len / 2) * progress);

    for (int i = 0; i < len; i++) {
      int dist = abs(i - center);
      if (dist <= limit) {
        _segment.setPixelColor(i, _intro_color);
      } else {
        _segment.setPixelColor(i, 0);
      }
    }
  }

  return false; // Still running
}

// Valid Palette Implementation (Moved from line 121)
uint32_t Segment::color_from_palette(uint16_t i, bool mapping, bool wrap,
                                     uint8_t mcol, uint8_t pbri) {
  // Get Palette Data
  const uint32_t *palData = getPaletteByIndex(this->palette);
  if (!palData)
    return 0; // Black if invalid definition

  // Logic:
  // i [0..255] maps to palette [0..15] with interpolation.
  // FastLED/WLED logic: index = i >> 4, blend = i & 15.

  uint8_t index = i >> 4;
  uint8_t blendAmt = (i & 0x0F) << 4; // Scale 0-15 -> 0-240

  uint32_t c1 = palData[index];
  uint32_t c2 = palData[(index + 1) & 0x0F]; // Wrap 16->0

  uint32_t color = color_blend(c1, c2, blendAmt);

  // Apply brightness scaling (critical for BPM pulsing effect)
  if (pbri < 255) {
    uint8_t r = ((color >> 16) & 0xFF) * pbri / 255;
    uint8_t g = ((color >> 8) & 0xFF) * pbri / 255;
    uint8_t b = (color & 0xFF) * pbri / 255;
    return RGBW32(r, g, b, 0);
  }
  return color;
}
