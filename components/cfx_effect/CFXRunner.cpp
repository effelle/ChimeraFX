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
#include <cstdint>
#include <vector>

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
uint16_t mode_exploding_fireworks(void);
uint16_t mode_popcorn(void);
uint16_t mode_drip(void);
uint16_t mode_dropping_time(void);
uint16_t mode_heartbeat_center(void);
uint16_t mode_kaleidos(void);
uint16_t mode_follow_me(void);
uint16_t mode_follow_us(void);
uint16_t mode_cfx_horizon_sweep(void);

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

  if (instance && instance->target_light) {
    esphome::Color esphome_color(CFX_R(c), CFX_G(c), CFX_B(c), CFX_W(c));
    int light_size = instance->target_light->size();

    // Map usage to global buffer - apply true symmetrical mirror if enabled
    if (mirror) {
      int left_index = start + n;
      int right_index = stop - 1 - n;

      if (left_index >= 0 && left_index < light_size) {
        (*instance->target_light)[left_index] = esphome_color;
      }
      if (right_index >= 0 && right_index < light_size) {
        (*instance->target_light)[right_index] = esphome_color;
      }
    } else {
      int global_index = start + n;
      if (global_index >= 0 && global_index < light_size) {
        (*instance->target_light)[global_index] = esphome_color;
      }
    }
  }
}

uint32_t Segment::getPixelColor(int n) {
  if (n < 0 || n >= length())
    return 0;

  int global_index = start + n; // Always read from left side

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

  int len = physicalLength();
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

  int len = physicalLength();
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

void Segment::blur(uint8_t blur_amount) {
  if (!instance || !instance->target_light)
    return;

  uint8_t keep = 255 - blur_amount;
  uint8_t seep = blur_amount >> 1;

  // Create a temp buffer to avoid propagating changes directionally
  // For small strips this is fine. For large strips, we might want to optimize
  // by just keeping "previous" pixel value.
  // WLED approach: blur1d modifies in-place but effectively propagates.
  // Let's stick to simple in-place for now (WLED compat).

  int len = physicalLength();
  int light_size = instance->target_light->size();
  int global_start = start;
  esphome::light::AddressableLight &light = *instance->target_light;

  for (int i = 0; i < len; i++) {
    int global_index = global_start + i;
    if (global_index >= light_size)
      continue;

    // Get current color
    esphome::Color c = light[global_index].get();

    // Get neighbors (clamped to segment)
    // Left
    esphome::Color left = c;
    if (i > 0) {
      if ((global_index - 1) >= 0)
        left = light[global_index - 1].get();
    }

    // Right
    esphome::Color right = c;
    if (i < len - 1) {
      if ((global_index + 1) < light_size)
        right = light[global_index + 1].get();
    }

    // Blur Kernel: (C*keep + (L+R)*seep) / 256
    uint8_t r =
        ((uint16_t)c.r * keep + (uint16_t)(left.r + right.r) * seep) >> 8;
    uint8_t g =
        ((uint16_t)c.g * keep + (uint16_t)(left.g + right.g) * seep) >> 8;
    uint8_t b =
        ((uint16_t)c.b * keep + (uint16_t)(left.b + right.b) * seep) >> 8;
    uint8_t w =
        ((uint16_t)c.w * keep + (uint16_t)(left.w + right.w) * seep) >> 8;

    light[global_index] = esphome::Color(r, g, b, w);
  }
}

void Segment::subtractive_fade_val(uint8_t fade_amt) {
  if (!instance || !instance->target_light)
    return;
  int len = physicalLength();
  int light_size = instance->target_light->size();
  int global_start = start;
  esphome::light::AddressableLight &light = *instance->target_light;

  for (int i = 0; i < len; i++) {
    int global_index = global_start + i;
    if (global_index >= light_size)
      continue;

    esphome::Color c = light[global_index].get();
    uint8_t r = (c.r > fade_amt) ? (c.r - fade_amt) : 0;
    uint8_t g = (c.g > fade_amt) ? (c.g - fade_amt) : 0;
    uint8_t b = (c.b > fade_amt) ? (c.b - fade_amt) : 0;
    uint8_t w = (c.w > fade_amt) ? (c.w - fade_amt) : 0;

    light[global_index] = esphome::Color(r, g, b, w);
  }
}

void Segment::fade_out_smooth(uint8_t fade_amt) {
  // 1. Subtract (guarantee 0 floor)
  subtractive_fade_val(fade_amt);

  // 2. Blur (spread energy / anti-alias the fade)
  // Heuristic: Blur amount related to fade amount?
  // User asked for "standardized". Let's pick a good default.
  // blur(32) is a good balance between spreading and preserving detail.
  blur(32);
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

// Math Helpers - use cfx:: namespace from cfx_utils.h
using cfx::beatsin8;
using cfx::cfx_constrain;
using cfx::cfx_map;
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
// Palette 10: Ocean (formerly Pacifica) - Deep ocean blues with white crests
static const uint32_t PaletteOcean[16] CFX_PROGMEM = {
    0x001040, 0x002050, 0x003060, 0x004080, 0x0050A0, 0x0064B4,
    0x148CF0, 0x28C8FF, 0x50DCFF, 0x96E6FF, 0xC8F0FF, 0xC8F0FF,
    0x96E6FF, 0x28C8FF, 0x0050A0, 0x001040};

// Palette 11: HeatColors - For Sunrise effect (black â†’ red â†’ orange â†’
// yellow â†’ white)
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
  case 254:
    // Smart Random (generated on switch)
    // Needs instance to access the buffer
    if (instance)
      return instance->_currentRandomPaletteBuffer;
    return PaletteRainbow;
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
static CRGBW ColorFromPalette(const uint32_t *palette, uint8_t index,
                              uint8_t brightness) {
  uint8_t i = index >> 4;   // 0-15
  uint8_t f = index & 0x0F; // Fraction 0-15

  // Wrap around - read from Flash using compatibility macro
  uint32_t c1 = cfx_pgm_read_dword(&palette[i]);
  uint32_t c2 = cfx_pgm_read_dword(&palette[(i + 1) & 15]);

  // Lerp RGBW
  uint8_t w1 = (c1 >> 24) & 0xFF;
  uint8_t r1 = (c1 >> 16) & 0xFF;
  uint8_t g1 = (c1 >> 8) & 0xFF;
  uint8_t b1 = c1 & 0xFF;

  uint8_t w2 = (c2 >> 24) & 0xFF;
  uint8_t r2 = (c2 >> 16) & 0xFF;
  uint8_t g2 = (c2 >> 8) & 0xFF;
  uint8_t b2 = c2 & 0xFF;

  // Safety: cast to int to avoid signed truncation when r2 < r1
  uint8_t w = (uint8_t)std::max(0, (int)w1 + ((((int)w2 - (int)w1) * f) >> 4));
  uint8_t r = (uint8_t)std::max(0, (int)r1 + ((((int)r2 - (int)r1) * f) >> 4));
  uint8_t g = (uint8_t)std::max(0, (int)g1 + ((((int)g2 - (int)g1) * f) >> 4));
  uint8_t b = (uint8_t)std::max(0, (int)b1 + ((((int)b2 - (int)b1) * f) >> 4));

  // Apply brightness
  w = (w * brightness) >> 8;
  r = (r * brightness) >> 8;
  g = (g * brightness) >> 8;
  b = (b * brightness) >> 8;

  return CRGBW(r, g, b, w);
}

void CFXRunner::generateRandomPalette() {
  // Use cfx namespace for random helpers
  uint8_t baseHue = cfx::hw_random8();
  // Select Strategy: 0=Analogous, 1=Neon, 2=Texture
  uint8_t strategy = cfx::hw_random8(3); // 0, 1, 2

  DEBUGFX_PRINTF("Generating Random Palette: BaseHue=%d Strategy=%d", baseHue,
                 strategy);

  for (int i = 0; i < 16; i++) {
    CHSV color;
    // For offset calculations
    int16_t h_calc;

    if (strategy == 0) {
      // Mode A: Analogous (Nature)
      // BaseHue +/- 20 drift
      // random 0-40 -> -20 to +20
      int16_t drift = (int16_t)cfx::hw_random8(41) - 20;
      h_calc = baseHue + drift;
      // Wrap hue to 0-255
      uint8_t h = (uint8_t)(h_calc & 0xFF);

      // Saturation high but vary slightly for organic feel
      uint8_t s = cfx::hw_random8(200, 255); // 200-254
      uint8_t v = 255;
      color = CHSV(h, s, v);

    } else if (strategy == 1) {
      // Mode B: Neon (Vaporwave)
      // High Saturation. Base Hue + complementary accent (180 deg) at specific
      // indices. Complementary at 0, 4, 8, 12? Or random. Let's make it 25%
      // chance of accent
      if ((i % 4) == 0) {
        // Accent: Complementary + High Sat
        color = CHSV(baseHue + 128, 255, 255);
      } else {
        // Base: BaseHue +/- 15
        int16_t drift = (int16_t)cfx::hw_random8(31) - 15;
        h_calc = baseHue + drift;
        color = CHSV((uint8_t)(h_calc & 0xFF), 245, 255);
      }

    } else {
      // Mode C: Texture (Monochromatic)
      // Hue fixed. Vary Value and Saturation heavily.
      // Good for metallic, plasma, fire-like single color.
      uint8_t h = baseHue;
      uint8_t s = cfx::hw_random8(100, 255); // 100-254
      uint8_t v = cfx::hw_random8(50, 255);  // 50-254
      color = CHSV(h, s, v);
    }

    // Convert to RGB
    CRGB rgb;
    hsv2rgb_rainbow(color, rgb);

    // Store in internal FastLED palette (for potential future use)
    _currentRandomPalette[i] = rgb;

    // Convert to uint32_t buffer (0x00RRGGBB) for CFXRunner compatibility
    // W channel is 0 for palettes generally
    _currentRandomPaletteBuffer[i] = RGBW32(rgb.r, rgb.g, rgb.b, 0);
  }
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
      CRGBW c = ColorFromPalette(active_palette, colorIndex, 255);
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

uint16_t mode_cfx_horizon_sweep(void) {
  if (!instance)
    return FRAMETIME;

  if (instance->_segment.palette != 255 && instance->_segment.palette != 0) {
    uint16_t len = instance->_segment.length();
    const uint32_t *active_palette =
        getPaletteByIndex(instance->_segment.palette);

    for (int i = 0; i < len; i++) {
      uint8_t colorIndex = (i * 255) / (len > 1 ? len - 1 : 1);
      CRGBW c = ColorFromPalette(active_palette, colorIndex, 255);
      instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
    }
  } else {
    instance->_segment.fill(instance->_segment.colors[0]);
  }

  return FRAMETIME;
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
          CRGBW color = ColorFromPalette(active_palette, colorIndex, 255);
          waves[i].init(instance->_segment.length(), color);
        }
      }
    } else {
      // Dead slot. If it's within the active_count, start a new wave
      if (i < active_count) {
        uint8_t colorIndex = rand() % 256;
        const uint32_t *active_palette =
            getPaletteByIndex(instance->_segment.palette);
        CRGBW color = ColorFromPalette(active_palette, colorIndex, 255);
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

// --- Plasma Effect (ID 101) ---
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
    CRGBW c = ColorFromPalette(active_palette, smoothIndex, brightness);
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
    CRGBW c = ColorFromPalette(active_palette, hue8, 255);

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
    uint8_t fgR, fgG, fgB, fgW;
    uint8_t bgR, bgG, bgB, bgW;

    // Use solid color when palette is 255 (Solid) OR 0 (Default/None)
    // This fixes the issue where Default palette ignored the user's color.
    if (instance->_segment.palette == 255 || instance->_segment.palette == 0) {
      // Use user's base color as foreground
      fgR = (baseColor >> 16) & 0xFF;
      fgG = (baseColor >> 8) & 0xFF;
      fgB = baseColor & 0xFF;
      fgW = (baseColor >> 24) & 0xFF;
    } else {
      // Use palette color as foreground (0-19)
      const uint32_t *active_palette =
          getPaletteByIndex(instance->_segment.palette);
      CRGBW c = ColorFromPalette(active_palette, (i * 256 / len), 255);
      fgR = c.r;
      fgG = c.g;
      fgB = c.b;
      fgW = c.w;
    }

    // Calculate background color from foreground (21% brightness)
    // This ensures that even with palettes, the "off" state matches the "on"
    // color.
    bgR = (fgR * 54) >> 8;
    bgG = (fgG * 54) >> 8;
    bgB = (fgB * 54) >> 8;
    bgW = (fgW * 54) >> 8;

    // Blend: result = background + (foreground - background) * lum / 255
    uint8_t r = bgR + (((int16_t)(fgR - bgR) * lum) >> 8);
    uint8_t g = bgG + (((int16_t)(fgG - bgG) * lum) >> 8);
    uint8_t b = bgB + (((int16_t)(fgB - bgB) * lum) >> 8);
    uint8_t w = bgW + (((int16_t)(fgW - bgW) * lum) >> 8);

    instance->_segment.setPixelColor(i, RGBW32(r, g, b, w));
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
    // At intensity 0: ~0% chance per frame â†’ Very slow fill
    // At intensity 128: ~50% chance per frame â†’ Medium fill
    // At intensity 255: ~100% + bonus â†’ Fast fill
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
        CRGBW col = ColorFromPalette(active_palette, hue, 255);
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

  // Use standardized subtractive_fade_val: identical math, centralized.
  instance->_segment.subtractive_fade_val(fadeAmount);

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
      c = ColorFromPalette(active_palette, dothue, 255);
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
  CRGBW bgColor = ColorFromPalette(active_palette, bgIndex, 255);
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
      CRGBW c = ColorFromPalette(active_palette, colorIndex, 255);
      instance->_segment.setPixelColor(pos + led, RGBW32(c.r, c.g, c.b, c.w));
    }
  }

  return FRAMETIME;
}

// --- Phased Effect (ID 105) ---
// Continuous Moiré interference pattern using opposing sub-pixel sine waves
uint16_t mode_phased(void) {
  if (!instance)
    return 350;

  uint16_t len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  uint8_t speed = instance->_segment.speed;
  uint8_t intensity = instance->_segment.intensity;

  // Accumulate phase using 32-bit integer for perfect sub-pixel smoothness.
  // speed maps to wave velocity.
  uint32_t phase_speed = 30 + (speed * 3);
  instance->_segment.step += phase_speed;
  uint32_t t = instance->_segment.step;

  // Intensity controls the spatial frequency (how tightly packed the nodes are)
  // Maps 0-255 to 1..10 sine waves across the total strip length
  uint32_t num_waves = 1 + (intensity / 28);

  // Calculate 16-bit phase delta per pixel so the math is continuous across any
  // length
  uint32_t phase_step = (num_waves * 65536) / len;

  const uint32_t *active_palette =
      instance->_segment.palette == 0
          ? PaletteRainbow
          : getPaletteByIndex(instance->_segment.palette);

  // Determine starting color index (drifts slowly over time)
  uint8_t color_idx_start = (instance->now >> 6) & 0xFF;

  for (int i = 0; i < len; i++) {
    // Spatial phase (0-65535 across the strip length)
    uint32_t spatial_phase = i * phase_step;

    // Wave A: Moves Forward
    uint16_t w_a_phase = (spatial_phase + (t << 1)) & 0xFFFF;
    uint8_t w_a = cfx::sin8(w_a_phase >> 8); // 0-255

    // Wave B: Moves Backward at a slightly offset velocity
    uint16_t w_b_phase = (spatial_phase - (t + (t >> 2))) & 0xFFFF;
    uint8_t w_b = cfx::sin8(w_b_phase >> 8); // 0-255

    // Multiply the two waves to create a true Moiré interference pattern
    // The nodes where the waves collide and overlap perfectly are bright.
    uint8_t moire = cfx::scale8(w_a, w_b);

    // Increase contrast so the nodes pop brightly over a dark background
    uint16_t bri = moire * 3;
    if (bri > 255)
      bri = 255;

    // Standard Phased color mapping (spread the palette seamlessly across the
    // strip)
    uint8_t colorIndex = color_idx_start + ((i * 255) / len);

    CRGBW c = ColorFromPalette(active_palette, colorIndex, (uint8_t)bri);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
  }

  return FRAMETIME;
}

// --- Ripple Effect (ID 79) ---
// Ported from WLED FX.cpp mode_ripple
// Modified for Delta-Time smoothness

struct RippleState {
  uint16_t age; // 0 to 65535 (Lifetime)
  uint16_t pos; // Center
  uint8_t color;
  bool active;
};

// WLED helpers
// triwave8: 0->255->0 triangle wave
static inline uint8_t triwave8(uint8_t in) {
  if (in & 0x80) {
    in = 255 - in;
  }
  uint8_t out = in << 1;
  return out;
}

uint16_t mode_ripple(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  uint32_t delta = instance->frame_time;
  if (delta < 1)
    delta = 1;

  // Max ripples calculation
  uint8_t maxRipples = 1 + (instance->_segment.length() >> 2);
  if (maxRipples > 100)
    maxRipples = 100; // Increased cap from 20 to 100

  uint16_t dataSize = sizeof(RippleState) * maxRipples;

  if (!instance->_segment.allocateData(dataSize))
    return mode_static();

  RippleState *ripples = (RippleState *)instance->_segment.data;

  // Fade Background
  // REVERT: Back to 224 per user request (Fixed Floor Brightness Bug)
  instance->_segment.fadeToBlackBy(224);

  // Spawn Logic
  // Time-Based Probability to Fix Gaps
  // We want ~4 ripples/sec at Max Intensity (255) regardless of FPS.
  // Threshold = Intensity * Delta.
  // Boosted 3x per user request to eliminate gaps at lower intensities.
  if (random16(65535) <= (uint32_t)(instance->_segment.intensity * delta * 3)) {
    for (int i = 0; i < maxRipples; i++) {
      if (!ripples[i].active) {
        ripples[i].active = true;
        ripples[i].age = 0;
        ripples[i].pos = random16(len);
        ripples[i].color = random8();
        break;
      }
    }
  }

  // Process logic
  // WLED Decay Logic: decay = (speed >> 4) + 1
  // Lifespan (frames) = 255 / decay
  // Lifespan (ms) = Frames * 25ms (approx WLED tick)
  uint8_t decay = (instance->_segment.speed >> 4) + 1;
  uint32_t lifespan_ms = (255 * 25) / decay;

  // age step = (65535 * delta) / lifespan
  uint32_t age_step = (65535 * delta) / lifespan_ms;
  if (age_step < 1)
    age_step = 1;

  const uint32_t *active_palette = nullptr;
  if (instance->_segment.palette != 0) {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  } else {
    active_palette = PaletteRainbow;
  }

  for (int i = 0; i < maxRipples; i++) {
    if (!ripples[i].active)
      continue;

    // Age update
    if ((uint32_t)ripples[i].age + age_step > 65535) {
      ripples[i].active = false;
      continue; // Expired
    }
    ripples[i].age += age_step;

    // Map Age (0-65535) to WLED State (0-255)
    uint8_t ripplestate = ripples[i].age >> 8;

    // WLED Math:
    uint8_t wled_speed = instance->_segment.speed;
    unsigned rippledecay = (wled_speed >> 4) + 1;

    // High Precision Propagation (Safe Mode)
    // We want to calculate the WLED propagation value but using the full
    // resolution of 'age' instead of the quantized 'ripplestate'. WLED: prop =
    // (state / decay) * (speed + 1). (Approx) Ours: prop = (age * (speed + 1))
    // / decay / 256. This gives us the same scale as WLED but updates every
    // frame.

    // 1. Calculate raw value (Age * Speed) / Decay
    uint32_t prop_raw =
        ((uint32_t)ripples[i].age * (uint32_t)(wled_speed + 1)) / rippledecay;

    // 2. Shift down by 8 to match WLED's 8.8 fixed point scale
    // (Because Age is 256x larger than State).
    unsigned propagation = prop_raw >> 8;

    int propI = propagation >> 8;
    unsigned propF = propagation & 0xFF;

    // Amplitude Logic (Reverted to Safe WLED Logic)
    // Amplitude Logic (Smooth High-Precision)
    // Fixes "Blinking" issue caused by stepping ripplestate (age >> 8).
    // Previous logic was: triwave8((age >> 8) * 8). This caused jumps of 16
    // units in brightness. New logic: Use age (0-65535) directly for smooth
    // ramp-up and decay. Ramp up finishes at age ~4369 (equivalent to state
    // 17).
    unsigned amp;
    if (ripples[i].age < 4369) {
      // Linear Fade In (0 to 255 over 4369 ticks)
      amp = (ripples[i].age * 255) / 4369;
    } else {
      // Linear Fade Out (255 to 2 over remaining life)
      // map(age, 4369, 65535, 255, 2)
      // slope = (2 - 255) / (65535 - 4369) = -253 / 61166
      // val = 255 - ((age - 4369) * 253) / 61166
      amp = 255 - ((uint32_t)(ripples[i].age - 4369) * 253) / 61166;
    }

    // Rendering: 2 wavefronts
    int left = ripples[i].pos - propI - 1;
    int right = ripples[i].pos + propI + 2;

    uint32_t col;
    if (instance->_segment.palette == 255) {
      uint32_t c = instance->_segment.colors[0];
      col = c;
    } else {
      CRGBW c = ColorFromPalette(active_palette, ripples[i].color, 255);
      col = RGBW32(c.r, c.g, c.b, c.w);
    }

    // Loop 6 pixels (Restored Geometry - "Thicker Ripples")
    // User requested filling the "space in the middle".
    //
    // Fix for "3-4 leds -> Space -> 2 leds" issue:
    // This was caused by Step 48 wrapping (6 * 48 = 288 > 256). It created a
    // trough.
    //
    // New Logic: **Step 32** (Widened Wave).
    // Span: 6 * 32 = 192. Fits comfortably within one 256-unit wave cycle.
    // Result: One continuous, thick ripple. No gap.
    for (int v = 0; v < 6; v++) {
      // Phase Matching Fix:
      // Spatial Step is 32. Scale propF (0-255) to 0-32.
      // 256 / 8 = 32. So shift right by 3.
      uint8_t phase_shift = propF >> 3;

      // Use sin8 for smooth organic wave
      uint8_t wave = sin8(phase_shift + (v * 32));
      uint8_t mag = scale8(wave, amp);

      // Gamma Disabled per previous fix

      if (mag > 0) {
        // Helper lambda for MAX blending to prevent "dimming" flickers
        auto apply_max_pixel = [&](int pos, uint32_t color, uint8_t magnitude) {
          CRGBW c_new(color);
          // Scale by magnitude immediately
          c_new.r = scale8(c_new.r, magnitude);
          c_new.g = scale8(c_new.g, magnitude);
          c_new.b = scale8(c_new.b, magnitude);
          c_new.w = scale8(c_new.w, magnitude);

          uint32_t existing_int = instance->_segment.getPixelColor(pos);
          CRGBW c_exist(existing_int);

          // MAX Blending: Keep the brightest channel values
          // This prevents the "tail" of a wave from darkening the "trail" of
          // another.
          c_exist.r = (c_new.r > c_exist.r) ? c_new.r : c_exist.r;
          c_exist.g = (c_new.g > c_exist.g) ? c_new.g : c_exist.g;
          c_exist.b = (c_new.b > c_exist.b) ? c_new.b : c_exist.b;
          c_exist.w = (c_new.w > c_exist.w) ? c_new.w : c_exist.w;

          instance->_segment.setPixelColor(
              pos, RGBW32(c_exist.r, c_exist.g, c_exist.b, c_exist.w));
        };

        // Render Left
        int pLeft = left + v;
        if (pLeft >= 0 && pLeft < len) {
          apply_max_pixel(pLeft, col, mag);
        }

        // Render Right
        int pRight = right - v;
        if (pRight >= 0 && pRight < len) {
          apply_max_pixel(pRight, col, mag);
        }
      }
    }
  }

  // Use standardized Segment::blur — identical WLED kernel, centralized.
  // blur(40): tuned for Ripple (smooth trail without excessive smearing).
  instance->_segment.blur(40);

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
      uint8_t w = (c >> 24) & 0xFF;

      // Multiplicative decay with random factor
      // Scale factor 200-255 for longer trail (78-100% retention per frame)
      // Original WLED uses 128-255 but runs at higher FPS
      // GAMMA CORRECTION: Adjust retention factor for gamma
      uint8_t raw_factor = 200 + hw_random8(55);
      uint8_t scale_factor = instance->getFadeFactor(raw_factor);
      r = scale8(r, scale_factor);
      g = scale8(g, scale_factor);
      b = scale8(b, scale_factor);
      w = scale8(w, scale_factor);

      instance->_segment.setPixelColor(i, RGBW32(r, g, b, w));
    }
  }

  // Floor-sweeper: removes any sub-threshold residue that scale8 can't clear.
  // Value of 1 is minimal — won't affect the visible trail, only kills pixels
  // stuck at 1-2 brightness from asymptotic multiplicative fade.
  instance->_segment.subtractive_fade_val(1);

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
      CRGBW c = ColorFromPalette(active_palette, colorIndex, 255);

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

  // Scale based on intensity (zoom level) â€” WLED exact formula
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

  // Smoothly blend current palette toward target â€” WLED uses 48 steps
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

  // Render: Perlin noise mapped to palette â€” WLED exact
  for (int i = 0; i < len; i++) {
    uint8_t index = inoise8(i * scale, instance->_segment.aux0 + i * scale);
    CRGB c = ColorFromPalette(palettes[0], index, 255, LINEARBLEND);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, 0));
  }

  // Organic Y-axis drift â€” WLED exact
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
  bool do_palette =
      (instance->_segment.palette != 255 && instance->_segment.palette != 0);

  return chase(instance->_segment.colors[1],
               (instance->_segment.colors[2]) ? instance->_segment.colors[2]
                                              : instance->_segment.colors[0],
               instance->_segment.colors[0], do_palette);
}

// --- BPM Effect (ID 68) ---
// Rhythmic pulsing bands of light synchronized to a precision global master
// beat.
uint16_t mode_bpm(void) {
  if (!instance)
    return 350;

  uint16_t len = instance->_segment.length();
  if (len == 0)
    return mode_static();

  uint8_t speed = instance->_segment.speed;
  uint8_t intensity = instance->_segment.intensity;

  // 1. GLOBAL PRECISION BEAT ENGINE
  // User speed slider maps from ~30 BPM to ~150 BPM
  uint16_t bpm = 30 + ((speed * 120) >> 8);

  // Generate a synchronous 8-bit master beat envelope (0 to 255)
  // beatsin8_t provides a mathematically perfect sine oscillator synced to
  // cfx_millis()
  uint8_t global_beat_env = cfx::beatsin8_t(bpm, 0, 255);

  // 2. KINETIC BURST SHAPING
  // Rather than a soft sine wave, we want a punchy "surge" that feels musical.
  // We sharpen the sine wave into a spiky pulse by squaring it.
  uint8_t sharp_beat = cfx::scale8(global_beat_env, global_beat_env);
  sharp_beat =
      cfx::scale8(sharp_beat, sharp_beat); // 4th power for tight spikes

  // 3. SPATIAL INTERFERENCE
  // The intensity slider controls the density of the outward bands
  uint16_t wave_scale = 10 + (intensity >> 2); // 10 to 73

  // Accumulate position in the segment's `step` variable for high precision
  // drift. The drift speed bursts massively during the peak of the beat.
  uint32_t now = cfx_millis();
  uint32_t drift_speed = 50 + (sharp_beat * 3);
  instance->_segment.step += drift_speed;
  uint32_t spatial_offset = instance->_segment.step >> 6;

  int center = len / 2;
  const uint32_t *pal = getPaletteByIndex(instance->_segment.palette);
  bool is_solid = (instance->_segment.palette == 255);
  uint32_t solid_color = is_solid ? instance->_segment.colors[0] : 0;

  for (int i = 0; i < len; i++) {
    // Calculate distance from center for symmetrical burst
    int dist = abs(i - center);

    // Generate traveling sine waves pushing outward from the center
    uint16_t wave_phase = (dist * wave_scale) - spatial_offset;
    uint8_t wave_val = cfx::sin8(wave_phase & 0xFF);

    // MULTIPLY the wave by the master beat envelope. This completely prevents
    // 8-bit wrap tearing. Base brightness is 40 (never fully dark) + (wave *
    // surge pulse)
    uint8_t pixel_bri = 40 + cfx::scale8(wave_val, sharp_beat);

    // Base color shifts symmetrically outward over time
    uint8_t color_idx = (dist * 2) - (now >> 6);

    uint32_t c;
    if (is_solid) {
      c = solid_color;
    } else {
      CRGBW pal_c = ColorFromPalette(pal, color_idx, 255);
      c = RGBW32(pal_c.r, pal_c.g, pal_c.b, pal_c.w);
    }

    // Apply the newly calculated brightness envelope
    uint8_t r = cfx::scale8((c >> 16) & 0xFF, pixel_bri);
    uint8_t g = cfx::scale8((c >> 8) & 0xFF, pixel_bri);
    uint8_t b = cfx::scale8(c & 0xFF, pixel_bri);
    uint8_t w = cfx::scale8((c >> 24) & 0xFF, pixel_bri);

    instance->_segment.setPixelColor(i, RGBW32(r, g, b, w));
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
    CRGBW c = ColorFromPalette(active_palette, colorIndex, 255);
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
  // Use exact speed match from mode_chase but with continuous 64-bit bounds
  // to prevent 16-bit truncation offset jumping
  uint32_t speed_factor = (instance->_segment.speed >> 2) + 1;
  uint32_t a =
      ((uint64_t)instance->now * speed_factor * instance->_segment.length()) >>
      16;

  unsigned width = (1 + (instance->_segment.intensity >> 4)); // 1-16
  unsigned index = a % (width * 2);                           // 2 bands

  for (unsigned i = 0; i < instance->_segment.length(); i++, index++) {
    if (index > (width * 2) - 1)
      index = 0;

    uint32_t color;
    if (index > width - 1) {
      if (instance->_segment.palette == 255 ||
          instance->_segment.palette == 0) {
        color = instance->_segment
                    .colors[1]; // Use secondary (often black)
                                // as background instead of Solid Foreground
      } else {
        color = instance->_segment.color_from_palette(i, true, true, 1);
      }
    } else {
      color = instance->_segment.colors[0]; // primary (solid)
    }

    instance->_segment.setPixelColor(instance->_segment.length() - i - 1,
                                     color);
  }
  return FRAMETIME;
}

// --- Percent Effect (ID 98) ---
// Linear meter/progress bar based on Intensity (0-255 mapped to 0-100%)
// Palette support: Solid (default), Rainbow, etc.
uint16_t mode_percent(void) {
  uint16_t len = instance->_segment.length();
  uint8_t percent = instance->_segment.intensity;
  // Map 0-255 to 0-len
  uint16_t lit_len = (uint32_t)percent * len / 255;

  const uint32_t *active_palette =
      (instance->_segment.palette == 0)
          ? PaletteSolid // Default to Solid
          : getPaletteByIndex(instance->_segment.palette);

  // Behavior:
  // If palette is Solid (255), use Primary Color.
  // If palette is Rainbow/etc, use gradient.

  if (instance->_segment.palette == 0 || instance->_segment.palette == 255) {
    fillSolidPalette(instance->_segment.colors[0]);
    active_palette = PaletteSolid;
  }

  for (int i = 0; i < len; i++) {
    if (i < lit_len) {
      // Lit portion
      // Map palette to the *lit* length? Or whole length?
      // "Meter" usually implies the color matches the position.
      // Let's map palette to the WHOLE length, so green is always at 0, red
      // always at 100 (if using heatmap)
      CRGBW c = ColorFromPalette(active_palette, (i * 255) / len, 255);
      instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
    } else {
      // Unlit portion
      instance->_segment.setPixelColor(i, 0);
    }
  }

  // Speed > 0: Add a subtle breathing effect to the lit portion
  if (instance->_segment.speed > 0) {
    uint8_t bri = beatsin88_t(instance->_segment.speed << 8, 200, 255);
    for (int i = 0; i < lit_len; i++) {
      uint32_t c = instance->_segment.getPixelColor(i);
      // Scale brightness
      uint8_t r = scale8(CFX_R(c), bri);
      uint8_t g = scale8(CFX_G(c), bri);
      uint8_t b = scale8(CFX_B(c), bri);
      uint8_t w = scale8(CFX_W(c), bri);
      instance->_segment.setPixelColor(i, RGBW32(r, g, b, w));
    }
  }

  return FRAMETIME;
}

// --- Percent Center Effect (ID 152) ---
// Bi-directional meter from center based on Intensity
uint16_t mode_percent_center(void) {
  uint16_t len = instance->_segment.length();
  uint16_t center = len / 2;
  uint8_t percent = instance->_segment.intensity;

  // Map 0-255 to 0-center (radius)
  uint16_t lit_radius = (uint32_t)percent * center / 255;

  const uint32_t *active_palette =
      (instance->_segment.palette == 0)
          ? PaletteSolid
          : getPaletteByIndex(instance->_segment.palette);

  if (instance->_segment.palette == 0 || instance->_segment.palette == 255) {
    fillSolidPalette(instance->_segment.colors[0]);
    active_palette = PaletteSolid;
  }

  for (int i = 0; i < len; i++) {
    int dist = abs(i - center);
    if (dist <= lit_radius) {
      // Lit
      // Map palette from center (0) to edge (255)
      // Or strip-linear? Strip-linear looks more Percent-like usually.
      // Let's do strip-linear so it looks like a single bar revealed from
      // center.
      CRGBW c = ColorFromPalette(active_palette, (i * 255) / len, 255);
      instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
    } else {
      instance->_segment.setPixelColor(i, 0);
    }
  }

  // Breathing
  if (instance->_segment.speed > 0) {
    uint8_t bri = beatsin88_t(instance->_segment.speed << 8, 200, 255);
    for (int i = 0; i < len; i++) {
      if (abs(i - center) <= lit_radius) {
        uint32_t c = instance->_segment.getPixelColor(i);
        uint8_t r = scale8(CFX_R(c), bri);
        uint8_t g = scale8(CFX_G(c), bri);
        uint8_t b = scale8(CFX_B(c), bri);
        uint8_t w = scale8(CFX_W(c), bri);
        instance->_segment.setPixelColor(i, RGBW32(r, g, b, w));
      }
    }
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
    CRGBW c = ColorFromPalette(active_palette, colorIndex, 255);

    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
    instance->_segment.setPixelColor(len - i - 1, RGBW32(c.r, c.g, c.b, c.w));
  }

  return FRAMETIME;
}

/*
 * Sparkle (ID 20)
 * Random pixels flash the primary color on a darkened background.
 * Refactored: Hybrid Fade (Exponential + Subtractive) & Tuned Density.
 * Matches WLED's "snappy" feel but fixes the stuck-pixel floor issue.
 */
uint16_t mode_sparkle(void) {
  // 1. Initialization
  if (instance->_segment.reset) {
    instance->_segment.fill(instance->_segment.colors[1]);
    instance->_segment.reset = false;
  }

  // 2. Timing & Hybrid Fade
  uint32_t delta = instance->frame_time;

  // A) Exponential Fade (The "Snappy" WLED look)
  // User: "Still too slow blinking time".
  // Divisor 32 -> 12 (Much faster, snappier).
  uint16_t fade_amt = (instance->_segment.speed * delta) / 12;

  // Correction: Instant fade at high speeds for effect
  if (instance->_segment.speed > 230)
    fade_amt = 255;

  // Ensure we don't fade TOO slowly at low speeds (stuck pixel risk).
  // Calculate retention.
  uint8_t retention = 255 - (fade_amt > 255 ? 255 : (uint8_t)fade_amt);

  // Apply Gamma Correction to Retention
  uint8_t corrected_retention = instance->getFadeFactor(retention);

  // Final Fade Amount
  uint8_t final_fade = 255 - corrected_retention;

  // Floor Fix: At low speeds, fade_amt is tiny. Even with gamma correction,
  // final_fade can be so small that pixels accumulate faster than they drain.
  // Enforce a minimum fade that guarantees visible drain at all speeds.
  uint8_t min_fade;
  if (instance->_segment.speed == 0)
    min_fade = 0; // Speed 0 = frozen, no fade
  else if (instance->_segment.speed <= 34)
    min_fade = 20; // Strong minimum: pixel drains in ~13 frames
  else if (instance->_segment.speed <= 100)
    min_fade = 8;
  else
    min_fade = 1;

  if (final_fade < min_fade)
    final_fade = min_fade;

  instance->_segment.fadeToBlackBy(final_fade);

  // B) Subtractive Kicker (The "Floor Fix")
  // Scale kicker inversely with speed to sweep residual floor brightness.
  uint8_t sub_kicker;
  if (instance->_segment.speed <= 34)
    sub_kicker = 12; // Aggressive sweep at very low speeds
  else if (instance->_segment.speed < 100)
    sub_kicker = 3;
  else
    sub_kicker = 2;

  int len = instance->_segment.length();
  // Use standardized subtractive_fade_val: identical math, centralized.
  instance->_segment.subtractive_fade_val(sub_kicker);

  // 3. Spawning
  // User: "Double density at 128".
  // Previous divisor 20 -> 10 (doubles the chance at same intensity).
  uint32_t chance = ((instance->_segment.intensity >> 2) * delta) / 10;

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
 * Inverted: Background is lit (primary color or full palette), sparkles are
 * black (or secondary). Intensity controls sparkle density.
 */
uint16_t mode_flash_sparkle(void) {
  int len = instance->_segment.length();

  if (instance->_segment.reset) {
    instance->_segment.fill(instance->_segment.colors[0]);
    instance->_segment.reset = false;
  }

  // Paint background every frame.
  // Solid palette: flat fill with primary color.
  // Any other palette: map the full palette across the strip length.
  if (instance->_segment.palette == 0 || instance->_segment.palette == 255) {
    instance->_segment.fill(instance->_segment.colors[0]);
  } else {
    const uint32_t *pal = getPaletteByIndex(instance->_segment.palette);
    for (int i = 0; i < len; i++) {
      // Map pixel position to palette index 0-255
      uint8_t palIdx = (uint8_t)((i * 255) / (len - 1 > 0 ? len - 1 : 1));
      CRGBW c = ColorFromPalette(pal, palIdx, 255);
      instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
    }
  }

  // Spawning - Intensity is the primary density driver
  // random8() < intensity: 0=never, 128=50%, 255=always
  if (cfx::hw_random8() < instance->_segment.intensity) {
    uint16_t index = cfx::hw_random16(0, len);
    instance->_segment.setPixelColor(index, instance->_segment.colors[1]);
  }

  return FRAMETIME;
}

/*
 * Hyper Sparkle (ID 22) - "Sparkle+"
 * Intense, fast sparkles.
 * Matches Sparkle logic but Higher Density/Speed.
 */
uint16_t mode_hyper_sparkle(void) {
  uint32_t delta = instance->frame_time;

  if (instance->_segment.reset) {
    instance->_segment.fill(instance->_segment.colors[1]);
    instance->_segment.reset = false;
  }

  // Hybrid Fade for Hyper Sparkle
  // Much Faster fade
  uint16_t fade_base = 30 + (instance->_segment.speed); // Start high
  fade_base = (fade_base * delta) / 20;
  if (fade_base > 255)
    fade_base = 255;

  uint8_t retention = 255 - (uint8_t)fade_base;
  uint8_t final_fade = 255 - instance->getFadeFactor(retention);

  instance->_segment.fadeToBlackBy(final_fade);

  // Subtractive Kicker (Stronger here, but scaled at very low speeds)
  uint8_t sub_kicker;
  if (instance->_segment.speed < 17)
    sub_kicker = 8; // Strong kick at very low speeds
  else if (instance->_segment.speed < 50)
    sub_kicker = 6;
  else
    sub_kicker = 4;
  int len = instance->_segment.length();
  // Use standardized subtractive_fade_val: identical math, centralized.
  instance->_segment.subtractive_fade_val(sub_kicker);

  // Spawn Logic
  // Higher density than Sparkle
  uint16_t max_sparks = (len / 4) + 1;
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
struct EnergySpark {
  int16_t pos;
  uint8_t level; // 0-255 (0 = dead)
  bool building; // true = surge, false = discharge
};

#define MAX_ENERGY_SPARKS 10
struct EnergyData {
  uint32_t accumulator;
  uint32_t last_millis;
  EnergySpark sparks[MAX_ENERGY_SPARKS];
};

// --- Energy Effect (ID 158) ---
// Progress bar that unmasks a live rainbow animation with a white leading tip.
// Phase 1: Agitation Engine - Noise-driven speed fluctuations.
// Phase 2: Energy Spikes - Localized white-hot eruptions during high chaos.
// Phase 3: Contrast & Size Refinement - Hue-gating and 5-LED bloom.
// Phase 4: Scaling & Exit Refinement - Proportional blooms and linear exit.
uint16_t mode_energy(void) {
  if (!instance)
    return 350;
  uint16_t len = instance->_segment.length();

  // State Management
  EnergyData *data = (EnergyData *)instance->_segment.data;
  if (!data || instance->_segment.reset) {
    if (!instance->_segment.allocateData(sizeof(EnergyData)))
      return 350;
    data = (EnergyData *)instance->_segment.data;
    data->last_millis = instance->now;
    data->accumulator = 0;
    for (int s = 0; s < MAX_ENERGY_SPARKS; s++)
      data->sparks[s].level = 0;
  }

  uint32_t dt = instance->now - data->last_millis;
  data->last_millis = instance->now;

  if (instance->_segment.reset) {
    instance->_segment.step = instance->now;
    instance->_segment.reset = false;
  }

  uint32_t duration = (257 - instance->_segment.speed) * 15;
  uint32_t elapsed = instance->now - instance->_segment.step;

  // --- Step 3: Linear Exit Logic (Phase 4) ---
  // Extend duration so the white head (4px) fully clears the end.
  uint32_t head_len = 4;
  uint32_t extra_time = (head_len * duration) / (len ? len : 1);
  uint32_t total_duration = duration + extra_time;

  bool finished = (elapsed >= total_duration);
  if (finished)
    elapsed = total_duration;

  // --- Step 1: Agitation Engine (Chaos Contrast FIX) ---
  uint8_t raw_noise = cfx::inoise8(instance->now >> 3, 42);
  uint32_t chaos = (uint32_t)raw_noise * raw_noise; // 0..65025
  uint32_t chaos_mult = cfx::cfx_map(chaos, 0, 65025, 50, 1280);
  uint32_t speed_factor = (instance->_segment.speed * chaos_mult) >> 8;
  if (speed_factor < 16)
    speed_factor = 16;
  data->accumulator += (dt * speed_factor);

  uint16_t progress = (elapsed * len) / (duration ? duration : 1);
  uint8_t counter = (data->accumulator >> 11) & 0xFF;
  uint16_t spatial_mult = 16 << (instance->_segment.intensity / 29);

  // --- Step 2: Energy Spikes (Localized Eruptions) ---
  // Trigger spikes during agitation (raw_noise > 140)
  // Phase 5: Lock ignition until the introductory wipe is finished
  if (finished && raw_noise > 140 && cfx::hw_random8() < 64) {
    for (int s = 0; s < MAX_ENERGY_SPARKS; s++) {
      if (data->sparks[s].level == 0) {
        int16_t pos = cfx::hw_random16() % (len ? len : 1);
        uint8_t hue = ((pos * spatial_mult) / (len ? len : 1)) + counter;
        if (hue > 40 && hue < 150)
          break;
        data->sparks[s].pos = pos;
        data->sparks[s].level = 10;
        data->sparks[s].building = true;
        break;
      }
    }
  }

  // Update Spikes
  for (int s = 0; s < MAX_ENERGY_SPARKS; s++) {
    if (data->sparks[s].level == 0)
      continue;
    if (data->sparks[s].building) {
      uint16_t next = data->sparks[s].level + (dt / 2);
      if (next >= 255) {
        data->sparks[s].level = 255;
        data->sparks[s].building = false;
      } else {
        data->sparks[s].level = (uint8_t)next;
      }
    } else {
      uint16_t sub = (dt / 4);
      if (data->sparks[s].level <= sub)
        data->sparks[s].level = 0;
      else
        data->sparks[s].level -= (uint8_t)sub;
    }
  }

  // Force Rainbow Palette
  const uint32_t *active_palette = getPaletteByIndex(4);

  // --- Phase 4: Proportional Bloom Logic ---
  uint16_t spark_radius = (len / 60);
  if (spark_radius < 2)
    spark_radius = 2; // Min 5 LED total
  if (spark_radius > 5)
    spark_radius = 5; // Max 11 LED total

  for (int i = 0; i < len; i++) {
    uint32_t rainbow_32 = 0;
    if (i < (int)progress - (int)head_len || finished) {
      uint8_t index = ((i * spatial_mult) / (len ? len : 1)) + counter;
      // --- Phase 4: 80% Background Dimming ---
      CRGBW c = ColorFromPalette(active_palette, index, 205);
      rainbow_32 = RGBW32(c.r, c.g, c.b, c.w);
    } else if (i <= (int)progress) {
      rainbow_32 = RGBW32(255, 255, 255, 255);
    }

    // Blend Spikes (additive brightness with proportional bloom)
    uint16_t spike_bri = 0;
    for (int s = 0; s < MAX_ENERGY_SPARKS; s++) {
      if (data->sparks[s].level > 0) {
        int distance = std::abs(data->sparks[s].pos - i);
        if (distance <= (int)spark_radius - 1) {
          spike_bri = std::max(spike_bri, (uint16_t)data->sparks[s].level);
        } else if (distance == (int)spark_radius) {
          uint16_t bloom = data->sparks[s].level >> 1;
          spike_bri = std::max(spike_bri, bloom);
        }
      }
    }

    if (spike_bri > 0) {
      CRGBW final_c = color_add(
          CRGBW(rainbow_32), CRGBW(spike_bri, spike_bri, spike_bri, spike_bri));
      instance->_segment.setPixelColor(
          i, RGBW32(final_c.r, final_c.g, final_c.b, final_c.w));
    } else {
      instance->_segment.setPixelColor(i, rainbow_32);
    }
  }
  return FRAMETIME;
}

// --- Chaos Theory Effect (ID 159) ---
// Organic, noise-driven evolution of the Energy effect.
// Features:
// 1. Embedded Glitter Intro: Replaces cursor with a sparkling birth.
// 2. Bidirectional Flux: Smooth 16-bit noise drives speed and direction.
// 3. Smart Palette: Fully integrated with ID 254.
// 4. Energy Spikes: Retained for visual texture.

struct ChaosData {
  uint32_t accumulator;
  uint32_t last_millis;
  EnergySpark sparks[MAX_ENERGY_SPARKS];
  // Intro State
  uint32_t intro_start;
  bool intro_done;
};

uint16_t mode_chaos_theory(void) {
  if (!instance)
    return 350;

  uint16_t len = instance->_segment.length();

  // State Management
  ChaosData *data = (ChaosData *)instance->_segment.data;
  if (!data || instance->_segment.reset) {
    if (!instance->_segment.allocateData(sizeof(ChaosData)))
      return 350;
    data = (ChaosData *)instance->_segment.data;
    data->last_millis = instance->now;
    data->accumulator = 0;
    data->intro_start = instance->now;
    data->intro_done = false;
    for (int s = 0; s < MAX_ENERGY_SPARKS; s++)
      data->sparks[s].level = 0;

    // On reset, fill black to start fresh for intro
    instance->_segment.fill(0);
    instance->_segment.reset = false;
  }

  uint32_t dt = instance->now - data->last_millis;
  data->last_millis = instance->now;

  // --- Phase 1: Embedded Glitter Intro ---
  if (!data->intro_done) {
    uint32_t intro_elapsed = instance->now - data->intro_start;
    const uint32_t INTRO_DURATION = 1500; // 1.5s build-up

    if (intro_elapsed >= INTRO_DURATION) {
      data->intro_done = true;
      // Flash white to signify "Birth of Chaos"
      instance->_segment.fill(RGBW32(255, 255, 255, 255));
      return FRAMETIME;
    }

    // Fade existing sparks slightly (leave trails)
    instance->_segment.fadeToBlackBy(40);

    // Spawn glitter based on progress (accelerating density)
    // Progress 0.0 -> 1.0 (approximated 0-255)
    uint8_t progress = (intro_elapsed * 255) / INTRO_DURATION;

    // Density increases with progress
    // Scale spawn count by length to ensure density on long strips
    // Base: at least 1 pixel. Max: len / 10 pixels per frame.
    int max_spawn = (len / 10) + 1;
    long spawn_count = (long(progress) * max_spawn) / 255;

    // Always spawn at least one in the second half
    if (spawn_count == 0 && progress > 128)
      spawn_count = 1;

    for (int k = 0; k < spawn_count; k++) {
      // Random position
      uint16_t pos = cfx::hw_random16() % (len ? len : 1);
      // Sparkling white
      instance->_segment.setPixelColor(pos, RGBW32(255, 255, 255, 255));
    }

    return FRAMETIME;
  }

  // --- Phase 2: The Chaos Engine (Running State) ---

  // 1. Agitation Engine (Literal Port from Energy ID 158)
  uint8_t raw_noise = cfx::inoise8(instance->now >> 3, 42);
  uint32_t chaos = (uint32_t)raw_noise * raw_noise; // 0..65025
  uint32_t chaos_mult = cfx::cfx_map(chaos, 0, 65025, 50, 1280);
  uint32_t speed_factor = (instance->_segment.speed * chaos_mult) >> 8;
  if (speed_factor < 16)
    speed_factor = 16;
  data->accumulator += (dt * speed_factor);

  // FIX (Iteration 11): Noise-Driven Pixel Scatter
  // Modulating spatial_mult caused strobing (accordion stretch).
  // Now we lock scale to Energy, and scatter pixels when chaos is high.

  // 1. Restore exact Energy Speed Shift
  uint8_t counter = (data->accumulator >> 11) & 0xFF;

  // 2. Restore exact Energy Intensity Math (Zoom)
  uint16_t spatial_mult = 16 << (instance->_segment.intensity / 29);

  // 3. Noise-Driven Index Scattering (Twinkle Chaos overlay)
  // Low noise = 0 scatter (Linear Scrolling).
  // High noise = Pixel indices drift randomly (Twinkle Chaos).
  uint8_t scatter_range = 0;
  if (raw_noise > 128) {
    scatter_range = cfx::cfx_map(raw_noise, 128, 255, 0, 80);
  }

  // 2. Render Background
  const uint32_t *active_palette = instance->_currentRandomPaletteBuffer;
  if (active_palette[0] == 0 && active_palette[15] == 0) {
    instance->generateRandomPalette();
  }

  for (int i = 0; i < len; i++) {
    // Map position to 0-255 using exact Energy math
    uint8_t index = ((i * spatial_mult) / (len ? len : 1)) + counter;

    // Apply twinkle scatter if active
    if (scatter_range > 0) {
      index += cfx::hw_random8(scatter_range) - (scatter_range >> 1);
    }

    // Use 205 (80%) brightness to match Energy's background depth exactly
    CRGBW c = ColorFromPalette(active_palette, index, 205);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
  }

  // --- Phase 3: Energy Spikes (Synchronized Chaos) ---
  // In the original, spikes were purely random during high agitation.
  // Here, we inject the GLOBAL PRECISION BEAT ENGINE to give the chaos a
  // structural, musical pulse.
  uint16_t bpm = 30 + ((instance->_segment.speed * 120) >> 8);
  uint8_t global_beat_env = cfx::beatsin8_t(bpm, 0, 255);

  // Sharpen the beat into an explosive trigger
  uint8_t sharp_beat = cfx::scale8(global_beat_env, global_beat_env);
  sharp_beat = cfx::scale8(sharp_beat, sharp_beat);

  // Trigger explosive spikes ONLY when the noise field is agitated AND the
  // global beat strikes. The higher the beat peak, the higher the probability
  // of spawning a spark.
  if (raw_noise > 120 && sharp_beat > 128 &&
      cfx::hw_random8() < (sharp_beat >> 1)) {
    for (int s = 0; s < MAX_ENERGY_SPARKS; s++) {
      if (data->sparks[s].level == 0) {
        data->sparks[s].pos = cfx::hw_random16() % (len ? len : 1);
        data->sparks[s].level = 255; // Maximum bright explosion on the beat
        data->sparks[s].building = false; // Instant pop, then fade
        break;
      }
    }
  }

  // Provide bloom range
  uint16_t spark_radius = (len / 60) + 1;
  if (spark_radius > 4)
    spark_radius = 4;

  // Update Spikes
  for (int s = 0; s < MAX_ENERGY_SPARKS; s++) {
    if (data->sparks[s].level > 0) {
      // Fade
      uint8_t fade = 5; // Fixed fade rate
      if (data->sparks[s].level <= fade)
        data->sparks[s].level = 0;
      else
        data->sparks[s].level -= fade;
    }
  }

  // Draw Spikes (Additive Blend)
  for (int s = 0; s < MAX_ENERGY_SPARKS; s++) {
    if (data->sparks[s].level > 0) {
      int center = data->sparks[s].pos;
      uint8_t bri = data->sparks[s].level;

      // Helper to add brightness smoothly by blending toward white
      auto add_brightness = [&](int pos, uint8_t amount) {
        if (pos >= 0 && pos < len) {
          uint32_t existing = instance->_segment.getPixelColor(pos);

          // Blend from the existing color towards pure white,
          // keeping the background visible through the spike edge
          uint32_t final = color_blend(existing, (uint32_t)0xFFFFFFFF, amount);

          instance->_segment.setPixelColor(pos, final);
        }
      };

      add_brightness(center, bri);

      for (int r = 1; r <= spark_radius; r++) {
        uint8_t dim = bri >> r;
        if (dim == 0)
          continue;
        add_brightness(center - r, dim);
        add_brightness(center + r, dim);
      }
    }
  }

  return FRAMETIME;
}

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

  CRGBW c = ColorFromPalette(active_palette, counter, 255);

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
    CRGBW c = ColorFromPalette(active_palette, index, 255);

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

  // Use standardized subtractive_fade_val: identical math, centralized.
  instance->_segment.subtractive_fade_val(fade_amt);

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
      CRGBW c = ColorFromPalette(active_palette, hw_random8(), 255);
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
// Explicit trail rendering â€” gamma-aware, with direction-change memory
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
  unsigned speed_factor = 96 - ((unsigned)spd * 94 / 255); // 96â†’2
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
uint16_t mode_color_sweep(void);
uint16_t mode_strobe(void);
uint16_t mode_percent(void);
uint16_t mode_percent_center(void);
uint16_t mode_fluid_rain(void);

// --- Heartbeat Effect (ID 100) ---
// Replicates WLED logic with framerate-independent decay and gamma correction
uint16_t mode_heartbeat(void) {
  if (!instance)
    return 350;

  // BPM: 40 + (speed / 8) -> Range 40-71 BPM
  unsigned bpm = 40 + (instance->_segment.speed >> 3);
  // Time per beat (ms)
  uint32_t msPerBeat = (60000L / bpm);
  // Second beat timing (approx 1/3 of beat)
  uint32_t secondBeat = (msPerBeat / 3);

  // State:
  // aux0: Beat Phase (0=Main Beat waiting, 1=Second Beat waiting)
  // aux1: Brightness/Decay State (High = Dark, Low = Bright)
  // step: Last Beat Time

  // Reset logic
  if (instance->_segment.reset) {
    instance->_segment.aux1 = 0; // Start Dark
    instance->_segment.aux0 = 0;
    instance->_segment.step = instance->now;
    instance->_segment.reset = false;
  }

  uint32_t beatTimer = instance->now - instance->_segment.step;

  // 1. Beat Logic
  if ((beatTimer > secondBeat) && !instance->_segment.aux0) {
    // Trigger Second Beat ("dup")
    instance->_segment.aux1 = UINT16_MAX;
    instance->_segment.aux0 = 1;
  }

  if (beatTimer > msPerBeat) {
    // Trigger Main Beat ("lub")
    instance->_segment.aux1 = UINT16_MAX;
    instance->_segment.aux0 = 0;
    // Account for drift
    instance->_segment.step = instance->now;
  }

  // 2. Linear Decay (Framerate Independent)
  // WLED Factor: F = 2042 / (2048 + intensity)
  // We apply F ^ (delta / 24ms)

  uint32_t delta = instance->frame_time;
  if (delta < 1)
    delta = 1;

  // Base WLED factor per ~24ms frame
  // 2042/2048 = 0.99707
  // 2042/2303 = 0.8866 (at max intensity)
  float wled_factor = 2042.0f / (2048.0f + instance->_segment.intensity);

  // Adjust for actual delta (Target 42FPS = ~24ms)
  float time_ratio = (float)delta / 24.0f;

  // Clamp ratio safety
  if (time_ratio > 10.0f)
    time_ratio = 10.0f;

  float decay = powf(wled_factor, time_ratio);

  instance->_segment.aux1 = (uint16_t)((float)instance->_segment.aux1 * decay);

  // 3. Rendering
  // Pulse Amount (Linear): 0 (Back) -> 255 (Pulse)
  uint8_t pulse_amt = (instance->_segment.aux1 >> 8);

  // GAMMA CORRECTION:
  // Apply gamma to the visual pulse brightness to ensure natural fade
  uint8_t gamma_pulse = instance->applyGamma(pulse_amt);

  // Blend Factor: 0 = Pulse, 255 = Back
  uint8_t blend = 255 - gamma_pulse;

  // Colors
  uint32_t colorBg = instance->_segment.colors[1]; // SEGCOLOR(1)

  uint16_t len = instance->_segment.length();

  for (int i = 0; i < len; i++) {
    uint32_t colorPulse;
    if (instance->_segment.palette == 0 || instance->_segment.palette == 255) {
      colorPulse = instance->_segment.colors[0];
    } else {
      // Palette mapping
      const uint32_t *active_palette =
          getPaletteByIndex(instance->_segment.palette);
      CRGBW c = ColorFromPalette(active_palette, (i * 255) / len, 255);
      colorPulse = RGBW32(c.r, c.g, c.b, c.w);
    }

    uint32_t finalColor = color_blend(colorPulse, colorBg, blend);
    instance->_segment.setPixelColor(i, finalColor);
  }

  return FRAMETIME;
}

void CFXRunner::service() {
  // CRITICAL FIX: Update global instance pointer to 'this' runner
  // Ensures effect functions operate on the correct strip context
  instance = this;

  // Globally initialize PaletteSolid with the latest selected color.
  // Any effect resolving getPaletteByIndex(255) needs this freshly populated,
  // especially for Pure W channel support in legacy C routines like
  // ColorFromPalette
  fillSolidPalette(_segment.colors[0]);

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
  case FX_MODE_FIRE_DUAL: // 153
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
  case FX_MODE_HEARTBEAT: // 100
    mode_heartbeat();
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
  case FX_MODE_HEARTBEAT_CENTER: // 154
    mode_heartbeat_center();
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
  case FX_MODE_PERCENT: // 98
    mode_percent();
    break;
  case FX_MODE_PERCENT_CENTER: // 152
    mode_percent_center();
    break;
  case FX_MODE_EXPLODING_FIREWORKS: // 90
    mode_exploding_fireworks();
    break;
  case FX_MODE_POPCORN: // 95
    mode_popcorn();
    break;
  case FX_MODE_DRIP: // 96
    mode_drip();
    break;
  case FX_MODE_DROPPING_TIME: // 151
    mode_dropping_time();
    break;
  case FX_MODE_KALEIDOS: // 155
    mode_kaleidos();
    break;
  case FX_MODE_FOLLOW_ME: // 156
    mode_follow_me();
    break;
  case FX_MODE_FOLLOW_US: // 157
    mode_follow_us();
    break;
  case FX_MODE_ENERGY: // 158
    mode_energy();
    break;
  case FX_MODE_CHAOS_THEORY: // 159
    mode_chaos_theory();
    break;
  case FX_MODE_FLUID_RAIN: // 160
    mode_fluid_rain();
    break;
  case FX_MODE_HORIZON_SWEEP: // 161
    mode_cfx_horizon_sweep();
    break;
  default:
    mode_static();
    break;
  }
}

// --- Physics Effects (ID 90, 95, 96) ---

// Shared Particle Struct for Fireworks, Popcorn, Drip
struct Spark {
  float pos;
  float vel;
  uint16_t col;     // Brightness/Color
  uint8_t colIndex; // State or color index
  Spark()
      : pos(0.0f), vel(0.0f), col(0), colIndex(0) {} // Add default constructor
};

/*
 * Exploding Fireworks (ID 90)
 * Ported from WLED (Aircoookie/Blazoncek)
 * Optimized for 1D Strips (No 2D support)
 */
/*
 * Exploding Fireworks (ID 90)
 * Ported from WLED (Aircoookie/Blazoncek)
 * Optimized for 1D Strips (No 2D support)
 */
uint16_t mode_exploding_fireworks(void) {
  uint16_t len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Allocate Data
  // WLED Logic: 5 + (rows*cols)/2, maxed at FAIR_DATA
  const uint16_t MAX_SPARKS = 64;
  uint16_t numSparks = std::min((uint16_t)(5 + (len >> 1)), MAX_SPARKS);

  // Data layout: [Spark array...] [float dying_gravity]
  size_t dataSize = sizeof(Spark) * numSparks;

  if (!instance->_segment.allocateData(dataSize + sizeof(float)))
    return mode_static();

  Spark *sparks = reinterpret_cast<Spark *>(instance->_segment.data);
  float *dying_gravity =
      reinterpret_cast<float *>(instance->_segment.data + dataSize);
  Spark *flare = sparks; // First spark is the rocket flare

  // Initialization / Resize handling
  if (dataSize != instance->_segment.aux1) {
    *dying_gravity = 0.0f;
    instance->_segment.aux0 = 0;        // State: 0=Init Flare
    instance->_segment.aux1 = dataSize; // Size tracker
  }

  // Fade out canvas (Trail effect)
  // Use the standardized helper which combines subtractive fade (floor
  // clearing) and blur (trail smoothing). 10 is the subtractive amount (approx
  // 4% per frame).
  instance->_segment.fade_out_smooth(10);

  // Physics
  // Gravity: WLED 0.0004 + speed/800000.
  // Map speed 0-255 to reasonable gravity.
  float gravity = -0.0004f - (instance->_segment.speed / 800000.0f);
  gravity *= len; // Scale by strip length

  if (instance->_segment.aux0 < 2) {    // STATE: FLARE LAUNCH
    if (instance->_segment.aux0 == 0) { // Init Flare
      flare->pos = 0;
      flare->vel = 0;
      // WLED Peak Height
      float peakHeight = (75 + cfx::hw_random8(180)) * (len - 1) / 255.0f;
      flare->vel = sqrtf(-2.0f * gravity * peakHeight);
      flare->col = 255; // Max brightness
      instance->_segment.aux0 = 1;
    }

    // Process Flare Physics
    if (flare->vel > 12 * gravity) { // Still rising (gravity is negative)
      // Draw Flare
      int pos = (int)flare->pos;
      if (pos >= 0 && pos < len) {
        instance->_segment.setPixelColor(
            pos, RGBW32(flare->col, flare->col, flare->col, 0));
      }

      flare->pos += flare->vel;
      flare->pos = cfx_constrain(flare->pos, 0.0f, (float)len - 1.0f);
      flare->vel += gravity;
      flare->col = qsub8(flare->col, 2); // Dim slightly
    } else {
      instance->_segment.aux0 = 2; // Trigger Explosion
    }

  } else if (instance->_segment.aux0 < 4) { // STATE: EXPLOSION

    // Initialize Sparks (Debris)
    if (instance->_segment.aux0 == 2) {
      // Explosion Logic
      // Use nSparks logic from WLED (approximate for 1D)
      int nSparks = flare->pos + cfx::hw_random8(4);
      nSparks = std::max(nSparks, 4);
      nSparks = std::min(nSparks, (int)numSparks);

      for (int i = 1; i < nSparks; i++) {
        sparks[i].pos = flare->pos;
        // WLED Velocity Logic:
        // (random(20001)/10000 - 0.9) covers range -0.9 to 1.1
        // Then multiplied by negative gravity * 50 to scale to strip
        // size/physics
        // INTENSITY CONTROL: Scale velocity by intensity/128 (128=Native,
        // 255=2x, 64=0.5x)
        float intensityScale = instance->_segment.intensity / 128.0f;
        if (intensityScale < 0.1f)
          intensityScale = 0.1f; // Prevent 0 velocity

        sparks[i].vel = (float(cfx::hw_random16(0, 20001)) / 10000.0f) - 0.9f;
        sparks[i].vel *= -gravity * 50.0f *
                         intensityScale; // Velocity scaling + Intensity scaling

        // Heat initialization (WLED uses extended range for heat)
        sparks[i].col = 345;

        // Random color index
        sparks[i].colIndex = cfx::hw_random8();
      }
      // Known spark[1] keeps the explosion alive
      sparks[1].col = 345;

      *dying_gravity = gravity / 2;
      instance->_segment.aux0 = 3;
    }

    // Process Sparks
    // Check if "known spark" (index 1) is still burnt out
    if (sparks[1].col > 4) {
      // Iterate over all active sparks.
      // Note: We don't track nSparks locally between frames, so we iterate
      // numSparks but only process those with heat > 0.
      for (int i = 1; i < numSparks; i++) {
        if (sparks[i].col > 0) {
          sparks[i].pos += sparks[i].vel;
          sparks[i].vel += *dying_gravity;

          if (sparks[i].col > 3)
            sparks[i].col -= 4; // Cooling
          else
            sparks[i].col = 0;

          if (sparks[i].pos >= 0 && sparks[i].pos < len) {
            // WLED Heat->Color Logic
            uint16_t prog = sparks[i].col;
            uint32_t spColor;

            // Resolve palette color
            // If default palette (0), use Rainbow (ID 4) logic
            uint8_t palId = instance->_segment.palette;
            if (palId == 0)
              palId = 4; // Default to Rainbow

            const uint32_t *pal = getPaletteByIndex(palId);
            CRGBW c = ColorFromPalette(pal, sparks[i].colIndex, 255);
            spColor = RGBW32(c.r, c.g, c.b, c.w);

            CRGBW finalColor = CRGBW(0, 0, 0, 0);

            if (prog > 300) { // White hot (fade from white to spark color)
              // Blend White -> Color
              // prog 345 -> 300 map to 255 -> 0 blend amount?
              // WLED: color_blend(spColor, WHITE, (prog-300)*5)
              // (345-300)*5 = 225. So high heat = mostly white.
              finalColor = CRGBW(color_blend(
                  spColor, RGBW32(255, 255, 255, 255), (prog - 300) * 5));
            } else if (prog > 45) { // Fade from color to black
              // WLED: color_blend(BLACK, spColor, prog - 45)
              // (300-45) = 255 (full color). (46-45) = 1 (mostly black).
              int blendAmt = cfx_constrain((int)prog - 45, 0, 255);
              finalColor = CRGBW(color_blend(0, spColor, blendAmt));

              // WLED adds specific cooling to G/B channels for fire look?
              // int cooling = (300 - prog) >> 5;
              // We'll skip that subtle detail for 1D optimization unless
              // needed.
            }

            instance->_segment.setPixelColor(
                (int)sparks[i].pos,
                RGBW32(finalColor.r, finalColor.g, finalColor.b, finalColor.w));
          }
        }
      }
      *dying_gravity *=
          0.8f; // Air resistance (WLED uses 0.8f, we were using 0.9f)
    } else {
      // Burnt out
      instance->_segment.aux0 = 6 + cfx::hw_random8(10); // Wait frames
    }

  } else { // STATE: COOLDOWN/RESET
    instance->_segment.aux0--;
    if (instance->_segment.aux0 < 4) {
      instance->_segment.aux0 = 0; // Reset to Flare
    }
  }

  return FRAMETIME;
}

/*
 * Popcorn (ID 95)
 * Ported from WLED
 */
uint16_t mode_popcorn(void) {
  uint16_t len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // WLED: max 21 kernels per segment (ESP8266)
  const int MAX_POPCORN = 24;
  if (!instance->_segment.allocateData(sizeof(Spark) * MAX_POPCORN))
    return mode_static();

  Spark *popcorn = reinterpret_cast<Spark *>(instance->_segment.data);

  // Background
  instance->_segment.fill(instance->_segment.colors[1]); // Secondary

  float gravity = -0.0001f - (instance->_segment.speed / 200000.0f);
  gravity *= len;

  // WLED Density fix: ~1:1 with 83 intensity
  // WLED used `intensity` directly?
  // WLED structure: loop MAX_POPCORN. if active update. else `if (random8() <
  // 255)`. Wait, WLED `mode_popcorn` spawns based on simple chance? Actually,
  // WLED default particle count logic might be sparser. We'll scale density
  // down. If user says "83 to have 1:1", it means 128 (default) is 1.5x too
  // dense. Let's scale intensity by 0.65?

  // Scaling density by 0.5 for further reduction
  uint8_t effective_intensity = scale8(instance->_segment.intensity, 128);
  int numPopcorn = effective_intensity * MAX_POPCORN / 255;
  if (numPopcorn == 0)
    numPopcorn = 1;

  for (int i = 0; i < numPopcorn; i++) {
    if (popcorn[i].pos >= 0.0f) { // Active
      popcorn[i].pos += popcorn[i].vel;
      popcorn[i].vel += gravity;
    } else {                       // Inactive - Pop?
      if (cfx::hw_random8() < 5) { // Pop Chance
        popcorn[i].pos = 0.01f;

        // Initial Velocity calculation
        unsigned peakHeight = 128 + cfx::hw_random8(128);
        peakHeight = (peakHeight * (len - 1)) >> 8;
        popcorn[i].vel = sqrtf(-2.0f * gravity * peakHeight);

        if (instance->_segment.palette == 0) {
          popcorn[i].colIndex = cfx::hw_random8(0, 3); // Pick simple colors?
        } else {
          popcorn[i].colIndex = cfx::hw_random8();
        }
      }
    }

    // Draw
    if (popcorn[i].pos >= 0.0f) {
      int idx = (int)popcorn[i].pos;
      if (idx < len) {
        uint32_t col;
        if (instance->_segment.palette == 0 ||
            instance->_segment.palette == 255) {
          // Default (0) or Solid (255): Use Primary Color
          col = instance->_segment.colors[0];
        } else {
          const uint32_t *pal = getPaletteByIndex(instance->_segment.palette);
          CRGBW c = ColorFromPalette(pal, popcorn[i].colIndex, 255);
          col = RGBW32(c.r, c.g, c.b, c.w);
        }
        instance->_segment.setPixelColor(idx, col);
      }
    }
  }

  return FRAMETIME;
}

/*
 * Drip (ID 96)
 * Ported from WLED
 */
// --- Dropping Time Effect (ID 151) ---
// Fills the strip with water over a set duration (Timer).
// Speed 0-255 maps to 1 minute - 60 minutes.

struct DroppingTimeState {
  uint32_t startTime;
  uint16_t filledPixels;
  uint32_t lastDropTime;

  // We need to track multiple drops:
  // 1. The "Filling" drop (the one that will raise the level)
  // 2. "Dummy" drops (visual only)
  // Reusing Spark struct logic
  Spark fillingDrop;
  Spark dummyDrops[2]; // Max 2 dummy drops active

  bool fillingDropActive;

  void init() {
    startTime = 0;
    filledPixels = 0;
    lastDropTime = 0;
    fillingDropActive = false;
    fillingDrop = Spark();
    for (int i = 0; i < 2; i++)
      dummyDrops[i] = Spark();
  }
};

uint16_t mode_dropping_time(void) {
  uint16_t len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  if (!instance->_segment.allocateData(sizeof(DroppingTimeState))) {
    ESP_LOGE("CFX", "DroppingTime: Alloc failed!");
    return mode_static();
  }

  DroppingTimeState *state =
      reinterpret_cast<DroppingTimeState *>(instance->_segment.data);

  // Initialize or Reset
  if (instance->_segment.reset) {
    ESP_LOGD("CFX", "DroppingTime: RESET");
    state->init();
    state->startTime = instance->now;
    instance->_segment.fill(0); // Start black
    instance->_segment.reset = false;
  }

  // Debug Log
  /*
  static uint32_t last_log = 0;
  if (instance->now - last_log > 2000) {
    last_log = instance->now;
    // Recalc duration for log
    uint32_t duration_min = 1 + (instance->_segment.speed * 59 / 255);
    uint32_t duration_ms = duration_min * 60 * 1000;
    uint32_t elapsed = instance->now - state->startTime;
    ESP_LOGD("CFX", "DT: Elapsed %u/%u ms, Filled %u", elapsed, duration_ms,
             state->filledPixels);
  }
  */

  // 1. Calculate Time & Progress
  // Speed 0   -> 1 minute
  // Speed 255 -> 60 minutes
  // Mapping: Duration (min) = 1 + (Speed * 59 / 255)
  uint32_t duration_min = 1 + (instance->_segment.speed * 59 / 255);
  uint32_t duration_ms = duration_min * 60 * 1000;

  uint32_t elapsed = instance->now - state->startTime;
  if (elapsed > duration_ms)
    elapsed = duration_ms;

  // Calculate Target Level based on Time
  // We want the level to rise *smoothly* or *stepwise*?
  // User said: "Every drop reduced by one the strip length... At 100% all lit
  // and animation stop." This implies the level is the visual representation of
  // time.

  uint16_t targetLevel = (uint16_t)((float)elapsed / duration_ms * len);
  if (targetLevel > len)
    targetLevel = len;

  // If we are done, just fill and return
  if (elapsed >= duration_ms) {
    // Show full ocean
    // ... (ocean logic for full strip)
    // For now, just a full color or ocean static
    // Reuse ocean logic but for full strip
  }

  // 2. Drop Logic
  // Gravity Physics
  uint8_t wled_speed = 83; // Standard gravity
  float gravity = -0.0005f - (wled_speed / 50000.0f);
  gravity *= (len - 1);

  // A. Filling Drop
  // We need to spawn a drop such that it hits the WATER LEVEL exactly when the
  // level needs to increment? Or simpler: We just spawn drops periodically.
  // When one hits, if it's time, we raise the level. User's request: "Every
  // drop... leaving the leds lit." So the drop CAUSES the fill.

  // Let's reverse it:
  // Calculate when the NEXT pixel should be filled.
  // NextFillTime = (PixelIndex + 1) * (Duration / Len)
  // We need to spawn the drop so it ARRIVES at NextFillTime.
  // FallTime = sqrt(2 * dist / |g|)
  // dist = (len-1) - currentLevel
  // SpawnTime = NextFillTime - FallTime.

  if (state->filledPixels < len) {
    uint32_t msPerPixel = duration_ms / len;
    uint32_t nextPixelTime = (state->filledPixels + 1) * msPerPixel;

    // Distance to fall: From Top (len-1) to Water Surface (filledPixels)
    float dist = (len - 1) - state->filledPixels;
    if (dist < 0)
      dist = 0;

    // Time to fall (frames? ms?). Gravity is in units/frame^2?
    // In Drip effect: pos += vel; vel += gravity.
    // Distance d = 0.5 * g * t^2 -> t = sqrt(2d/g) (in frames)
    // Convert frames to ms (approx 15ms/frame default, but variable)
    // Let's use a rough estimate or just spawn it slightly ahead.

    // Heuristic: Just spawn it when we are close.
    // Better: Interval based.
    // If we simply spawn drops at `msPerPixel` interval, they will arrive at
    // roughly the right rate. Let's try that for robustness.

    if (!state->fillingDropActive) {
      // Check if it's time to spawn the next filling drop
      // We want the drop to LAND when the timer reaches the next pixel.
      // So we spawn it `FallTime` *before* that.
      // Approx Fall Time (ms) ~ sqrt(2 * dist / 0.0005) * 15ms
      // G_eff = |gravity| = 0.0005 * len roughly.
      // Let's just spawn it if (NextPixelTime - Now) < ExpectedFallDuration

      float estFallFrames = sqrtf(2.0f * dist / (-gravity));
      uint32_t estFallMs = estFallFrames * 15; // Approx

      // Add a buffer so it doesn't arrive too late
      if (elapsed + estFallMs >= nextPixelTime) {
        // Spawn!
        state->fillingDropActive = true;
        state->fillingDrop.pos = len - 1;
        state->fillingDrop.vel = 0;
        state->fillingDrop.col = 255;
        state->fillingDrop.colIndex = 2; // Falling
      }
    }
  }

  // Update Filling Drop
  if (state->fillingDropActive) {
    state->fillingDrop.vel += gravity;
    state->fillingDrop.pos += state->fillingDrop.vel;

    // Hit Water Level?
    if (state->fillingDrop.pos <= state->filledPixels) {
      state->fillingDropActive = false;
      state->filledPixels++;
      if (state->filledPixels > len)
        state->filledPixels = len;
    }
  } else {
    // Failsafe / Catch-up Logic
    if (targetLevel > state->filledPixels) {
      state->filledPixels = targetLevel;
    }
  }

  // If Duration ended, force full fill
  if (elapsed >= duration_ms) {
    state->filledPixels = len;
  }

  // --- 3. RENDER PHASE (Always Run) ---

  // A. Clear Air (everything above water level)
  for (int i = state->filledPixels; i < len; i++) {
    instance->_segment.setPixelColor(i, 0);
  }

  // B. Draw Water (Ocean Logic)
  // Bidirectional waves for organic "sloshing" effect
  // Explicitly calculated to ensure opposing direction
  uint32_t ms = instance->now;
  uint8_t pal = instance->_segment.palette;
  if (pal == 0 || pal == 255)
    pal = 11; // Default to Ocean, prevent solid colors
  const uint32_t *active_palette = getPaletteByIndex(pal);

  // Time bases for waves (sawtooth 0-255)
  // Wave 1: Moves RIGHT (x - t)
  uint8_t t1 = beat8(15);
  // Wave 2: Moves LEFT (x + t)
  uint8_t t2 = beat8(18);

  for (int i = 0; i < state->filledPixels; i++) {
    // x coordinates (scaled)
    // larger multiplier = narrower waves
    uint8_t x1 = i * 4;
    uint8_t x2 = i * 7;

    // Wave 1: Right moving -> sin(x - t)
    uint8_t wave1 = sin8(x1 - t1);

    // Wave 2: Left moving -> sin(x + t)
    uint8_t wave2 = sin8(x2 + t2);

    // Combine (Average)
    uint8_t index = (wave1 + wave2) / 2;

    CRGBW c = ColorFromPalette(active_palette, index, 255);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
  }

  // C. Draw Drops (Filling Drop)
  if (state->fillingDropActive) {
    int pos = (int)state->fillingDrop.pos;
    if (pos >= state->filledPixels && pos < len) {
      instance->_segment.setPixelColor(pos, 0xFFFFFF); // Head
    }
    for (int t = 1; t <= 4; t++) {
      int tPos = pos + t;
      if (tPos >= state->filledPixels && tPos < len) {
        instance->_segment.setPixelColor(
            tPos, color_blend(0xFFFFFF, 0, 255 - (64 * t)));
      }
    }
  }

  // D. Draw Drops (Dummy Drops)
  // Ambient Update (Non-counting drops for visual texture on long timers)
  for (int i = 0; i < 2; i++) {
    if (state->dummyDrops[i].colIndex == 0) {
      // Inactive - Try to spawn
      // Requires at least 15 pixels of free fall space to be worth it
      if (len - state->filledPixels > 15 && cfx::hw_random16(0, 300) == 0) {
        state->dummyDrops[i].pos = len - 1;
        state->dummyDrops[i].vel = 0;
        state->dummyDrops[i].colIndex = 1; // Active
        state->dummyDrops[i].col =
            150 + cfx::hw_random8(100); // Random brightness
      }
    } else {
      // Active - Update Physics
      state->dummyDrops[i].vel += gravity;
      state->dummyDrops[i].pos += state->dummyDrops[i].vel;

      // Hit Water Level? -> Deactivate silently without incrementing level
      if (state->dummyDrops[i].pos <= state->filledPixels) {
        state->dummyDrops[i].colIndex = 0;
      }
    }
  }

  // Draw Dummy Drops (Alpha Trail)
  for (int i = 0; i < 2; i++) {
    if (state->dummyDrops[i].colIndex != 0) {
      int pos = (int)state->dummyDrops[i].pos;
      if (pos >= state->filledPixels && pos < len) {

        // Use Palette for Dummy Drop so it matches the Ocean/Selected palette
        // Brightness is stored in .col (150-250 range)
        uint8_t pal_index = (pos * 255) / len;

        // Determine active palette (lock out Solid/Picker color to preserve
        // water illusion)
        uint8_t pal = instance->_segment.palette;
        if (pal == 0 || pal == 255)
          pal = 11; // Default to Ocean
        const uint32_t *active_palette = getPaletteByIndex(pal);

        CRGBW c = ColorFromPalette(active_palette, pal_index,
                                   state->dummyDrops[i].col);
        uint32_t head_col = RGBW32(c.r, c.g, c.b, c.w);

        // Draw Head
        instance->_segment.setPixelColor(pos, head_col);

        // Draw Faded Trail (matching palette at previous positions)
        for (int t = 1; t <= 3; t++) {
          int tPos = pos + t;
          if (tPos >= state->filledPixels && tPos < len) {
            uint8_t trail_pal_index = (tPos * 255) / len;
            uint8_t trail_bri =
                std::max(0, (int)state->dummyDrops[i].col - (75 * t));
            CRGBW tc =
                ColorFromPalette(active_palette, trail_pal_index, trail_bri);
            instance->_segment.setPixelColor(tPos,
                                             RGBW32(tc.r, tc.g, tc.b, tc.w));
          }
        }
      }
    }
  }

  return FRAMETIME;
}

uint16_t mode_drip(void) {
  uint16_t len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  const int MAX_DROPS = 4;
  if (!instance->_segment.allocateData(sizeof(Spark) * MAX_DROPS))
    return mode_static();
  Spark *drops = reinterpret_cast<Spark *>(instance->_segment.data);

  instance->_segment.fill(instance->_segment.colors[1]);

  int numDrops = 1 + (instance->_segment.intensity >> 6); // 1..4

  // Speed Fix: 128 -> 83 scaling
  // WLED internal speed 83 is standard 1D physics.
  // If we receive 128, map it down.
  uint8_t wled_speed = scale8(instance->_segment.speed, 166); // 128 -> ~83

  float gravity = -0.0005f - (wled_speed / 50000.0f);
  gravity *= (len - 1);

  for (int j = 0; j < numDrops; j++) {
    if (drops[j].colIndex == 0) { // Init
      drops[j].pos = len - 1;
      drops[j].vel = 0;
      drops[j].col = 0;      // Brightness/Size measure
      drops[j].colIndex = 1; // State: 1=Forming
    }

    // Source (Tap)
    // Draw source pixel at top
    // WLED uses "sourcedrop" brightness logic.

    if (drops[j].colIndex == 1) { // Forming
      // Swelling
      drops[j].col += cfx::cfx_map(instance->_segment.speed, 0, 255, 1, 6);
      if (drops[j].col > 255)
        drops[j].col = 255;

      // Draw swelling drop at the top (len-1)
      // WLED logic: Source brightness increases.
      // Palette support (like Popcorn): use palette color if active
      uint32_t col;
      if (instance->_segment.palette == 0 ||
          instance->_segment.palette == 255) {
        col = instance->_segment.colors[0];
      } else {
        const uint32_t *pal = getPaletteByIndex(instance->_segment.palette);
        CRGBW c = ColorFromPalette(pal, (uint8_t)(j * 64), 255);
        col = RGBW32(c.r, c.g, c.b, c.w);
      }
      // Blend black -> color based on 'col' (0-255)
      // Using color_blend(0, col, brightness)
      // Note: color_blend blend param: 0=color1, 255=color2.
      // So color_blend(0, col, drops[j].col) blends from Black(0) to Color.
      instance->_segment.setPixelColor(
          len - 1, color_blend(0, col, (uint8_t)drops[j].col));

      // Random Fall Trigger
      // Chance increased by swelling size
      if (cfx::hw_random8() < drops[j].col / 20) {
        drops[j].colIndex = 2; // Fall State
        drops[j].col = 255;    // Full brightness for falling
      }
    }

    if (drops[j].colIndex > 1) { // Falling
      if (drops[j].pos > 0) {
        drops[j].pos += drops[j].vel;
        if (drops[j].pos < 0)
          drops[j].pos = 0;
        drops[j].vel += gravity;

        // Draw falling drop with TAIL
        // Simple trail logic: pos, pos-direction, ...
        int pos = (int)drops[j].pos;
        // Palette support (like Popcorn)
        uint32_t col;
        if (instance->_segment.palette == 0 ||
            instance->_segment.palette == 255) {
          col = instance->_segment.colors[0];
        } else {
          const uint32_t *pal = getPaletteByIndex(instance->_segment.palette);
          CRGBW c = ColorFromPalette(pal, (uint8_t)(j * 64), 255);
          col = RGBW32(c.r, c.g, c.b, c.w);
        }

        if (pos >= 0 && pos < len)
          instance->_segment.setPixelColor(pos, col);

        // Tail Logic: Only when Falling (vel < 0) AND in initial Drop phase
        // (colIndex == 2) User: "another led bounce 6 led backward with a lower
        // brightness without tail" So ONLY draw tail if falling.
        if (drops[j].colIndex == 2 && drops[j].vel < 0) {
          // Falling: Moves towards 0. Tail is at pos+1, pos+2...
          // Increased tail length to 6 pixels
          for (int t = 1; t <= 6; t++) {
            int tPos = pos + t;
            if (tPos >= 0 && tPos < len) {
              // Faint tail: Brighter fade to ensure visibility
              // Old: 64 >> (t-1) was too dim (Starts at 25%).
              // New: 255 >> t.
              // t=1: 128 (50%)
              // t=2: 64 (25%)
              // t=3: 32 (12.5%)
              // t=4: 16
              // t=5: 8
              // t=6: 4
              uint8_t dim = 255 >> t;

              instance->_segment.setPixelColor(tPos,
                                               color_blend(col, 0, 255 - dim));
            }
          }
        }

        // Bounce Logic
        if (drops[j].colIndex > 2) { // Bouncing
          // Splash on floor (stay on the last led) applies when bouncing
          // Draw the static drop at the bottom with lower brightness
          uint32_t dimCol = color_blend(col, 0, 150); // Lower brightness
          instance->_segment.setPixelColor(0, dimCol);

          // Draw the bouncing particle (already drawn by the main pos logic
          // above if pos > 0) But we want it to be dimmer too. The main logic
          // above draws 'col' at 'pos'. We need to overwrite it with dimCol if
          // we are bouncing.
          if (pos >= 0 && pos < len) {
            instance->_segment.setPixelColor(pos, dimCol);
          }
        }

      } else {                       // Hit Bottom
        if (drops[j].colIndex > 2) { // Already bouncing and hit bottom again
          drops[j].colIndex = 0;     // Reset / Disappear
        } else {
          // Init Bounce
          // Math for exactly 7 LEDs high: v = sqrt(2 * |g| * h)
          // gravity is negative, so |g| = -gravity.
          // h = 7.0f
          drops[j].vel = sqrtf(-2.0f * gravity * 7.0f);
          drops[j].pos =
              0.1f; // Lift slightly so it doesn't immediately hit 0 again
          drops[j].colIndex = 5; // Bouncing state
        }
      }
    }
  }

  return FRAMETIME;
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

  // fadeToBlackBy(160): aggressive multiplicative fade (37% retention/frame).
  // Clears a 255 pixel in ~4 frames = virtually no tail. No grey floor risk
  // at this fade strength, so subtractive_fade_val not needed here.
  instance->_segment.fadeToBlackBy(160);

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

    CRGBW c = ColorFromPalette(active_palette, i * (256 / MAX_BALLS), 255);
    uint32_t colorInt = RGBW32(c.r, c.g, c.b, c.w);

    uint32_t existing = instance->_segment.getPixelColor(pixel);
    uint8_t r = qadd8((existing >> 16) & 0xFF, (colorInt >> 16) & 0xFF);
    uint8_t g = qadd8((existing >> 8) & 0xFF, (colorInt >> 8) & 0xFF);
    uint8_t b = qadd8(existing & 0xFF, colorInt & 0xFF);
    uint8_t w = qadd8((existing >> 24) & 0xFF, (colorInt >> 24) & 0xFF);

    instance->_segment.setPixelColor(pixel, RGBW32(r, g, b, w));
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
      CRGBW c = ColorFromPalette(active_palette, (i * 255) / len, 255);
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
        CRGBW c = ColorFromPalette(active_palette, colorIndex, 255);
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
        CRGBW c = ColorFromPalette(active_palette, colorIndex, 255);
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
        a = cfx::cfx_map(a, 16, 255, 64, 192);
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
      CRGBW c = ColorFromPalette(active_palette, (i * 255) / len, 255);
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
        CRGBW c = ColorFromPalette(active_palette, (i * 255) / len + 128, 255);
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
    CRGBW c1 = ColorFromPalette(active_palette, instance->_segment.aux1, 255);
    col1 = RGBW32(c1.r, c1.g, c1.b, c1.w);
  }

  for (int i = 0; i < len; i++) {
    int index = (rev && back) ? len - 1 - i : i;

    // Foreground Color Construction
    uint32_t col0;
    if (useRandomColors) {
      CRGBW c0 = ColorFromPalette(active_palette, instance->_segment.aux0, 255);
      col0 = RGBW32(c0.r, c0.g, c0.b, c0.w);
    } else if (instance->_segment.palette == 255 ||
               (!useRandomColors && instance->_segment.palette == 0)) {
      col0 = instance->_segment.colors[0]; // Solid Color
    } else {
      // FIX: Use Stretched Palette mapping (i * 255 / len) instead of fixed
      // pattern (i * 12) This fixes "holes" and creates a smooth gradient
      // across the strip.
      uint8_t colorIndex = (i * 255) / len;
      CRGBW c0 = ColorFromPalette(active_palette, colorIndex, 255);
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

// --- Heartbeat Center Effect (ID 154) ---
// Same logic as Heartbeat, but mapping pulse to width from center
// --- Heartbeat Center Effect (ID 154) ---
// Same logic as Heartbeat, but mapping pulse to width from center
uint16_t mode_heartbeat_center(void) {
  if (!instance)
    return 350;

  // BPM: 40 + (speed / 8) -> Range 40-71 BPM
  unsigned bpm = 40 + (instance->_segment.speed >> 3);
  // Time per beat (ms)
  uint32_t msPerBeat = (60000L / bpm);
  // Second beat timing (approx 1/3 of beat)
  uint32_t secondBeat = (msPerBeat / 3);

  // State reuse: aux0 (phase), aux1 (amplitude), step (last beat time)
  if (instance->_segment.reset) {
    instance->_segment.aux1 = 0;
    instance->_segment.aux0 = 0;
    instance->_segment.step = instance->now;
    instance->_segment.reset = false;
  }

  uint32_t beatTimer = instance->now - instance->_segment.step;

  // 1. Beat Logic
  if ((beatTimer > secondBeat) && !instance->_segment.aux0) {
    instance->_segment.aux1 = UINT16_MAX; // Trigger Second Beat "dup"
    instance->_segment.aux0 = 1;
  }

  if (beatTimer > msPerBeat) {
    instance->_segment.aux1 = UINT16_MAX; // Trigger Main Beat "lub"
    instance->_segment.aux0 = 0;
    instance->_segment.step = instance->now;
  }

  // 2. Decay Logic (Framerate Independent)
  uint32_t delta = instance->frame_time;
  if (delta < 1)
    delta = 1;

  // TUNING: Reduced base factor from 2042 to 2020 to make decay faster
  // This addresses the "flat" feeling by creating more contrast/disconnect
  // between beats Old: 2042.0f / ... New: 2020.0f / ... (Faster decay)
  float wled_factor = 2020.0f / (2048.0f + instance->_segment.intensity);
  float time_ratio = (float)delta / 24.0f;
  if (time_ratio > 10.0f)
    time_ratio = 10.0f;
  float decay = powf(wled_factor, time_ratio);

  instance->_segment.aux1 = (uint16_t)((float)instance->_segment.aux1 * decay);

  // 3. Rendering (Soft Edge / Gradient)
  uint8_t pulse_amt = (instance->_segment.aux1 >> 8);
  uint8_t effective_val = instance->applyGamma(pulse_amt);

  uint16_t len = instance->_segment.length();

  // Dynamic Radius
  // Scale max_radius HIGHER than the strip length to ensure the fade doesn't
  // cutoff the edges at full brightness. Using 'len' instead of 'len/2' gives a
  // 2x overshoot. This means at max pulse, the "zero point" is far outside the
  // strip.
  uint32_t max_radius = len;
  uint32_t current_radius = (max_radius * effective_val) / 255;

  // Ensure a minimum radius so it doesn't disappear completely (Dry off fix)
  if (current_radius < 2)
    current_radius = 2;

  // Master Brightness for the peak center pixel
  uint8_t peak_brightness = effective_val;

  uint32_t color = instance->_segment.colors[0];
  bool mirror = instance->_segment.mirror;
  uint16_t center = len / 2;

  for (int i = 0; i < len; i++) {
    int dist;
    if (mirror) {
      // Distance from nearest edge
      int dist1 = i;
      int dist2 = (len - 1) - i;
      dist = (dist1 < dist2) ? dist1 : dist2;
    } else {
      // Standard: Distance from Center
      dist = abs(i - center);
    }

    if (dist < current_radius) {
      // Logic:
      // max_radius = 0 brightness.
      // 0 distance = peak_brightness.
      // Linear falloff.

      uint32_t falloff = ((current_radius - dist) * 255) / current_radius;
      uint8_t pixel_scale = (falloff * peak_brightness) / 255;

      // Render
      uint32_t pixel_color = color;
      if (instance->_segment.palette != 0 &&
          instance->_segment.palette != 255) {
        const uint32_t *active_palette =
            getPaletteByIndex(instance->_segment.palette);
        CRGBW c = ColorFromPalette(active_palette, (i * 255) / len, 255);
        pixel_color = RGBW32(c.r, c.g, c.b, c.w);
      }

      // Apply Brightness Scaling
      if (pixel_scale < 255) {
        uint8_t r = ((pixel_color >> 16) & 0xFF) * pixel_scale / 255;
        uint8_t g = ((pixel_color >> 8) & 0xFF) * pixel_scale / 255;
        uint8_t b = (pixel_color & 0xFF) * pixel_scale / 255;
        uint8_t w = ((pixel_color >> 24) & 0xFF) * pixel_scale / 255;
        pixel_color = RGBW32(r, g, b, w);
      }
      instance->_segment.setPixelColor(i, pixel_color);

    } else {
      instance->_segment.setPixelColor(i, 0);
    }
  }

  return FRAMETIME;
}

/*
 * Kaleidos (ID 155)
 * Enhanced N-Way Symmetrical Mirroring Effect
 * Divides the strip into 2/4/6/8 mirrored segments that dynamically "breathe"
 * in size. Even segments render forward, odd segments render backward. Adds
 * prism glints at the symmetry bounds to enhance the kaleidoscope illusion.
 * Uses a pure sub-pixel triangle wave phase engine to guarantee flawless
 * scrolling mirrors.
 */
uint16_t mode_kaleidos(void) {
  if (!instance)
    return FRAMETIME;

  uint16_t len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  uint32_t ms = cfx_millis();
  // Speed 0 = very slow, Speed 255 = fast
  uint32_t cycle_time = (ms * (uint32_t)(instance->_segment.speed + 1)) >> 9;

  // === Dynamic Symmetry Engine ===
  // Map intensity 0-255 to 1-4, then double to guarantee even: 2, 4, 6, 8
  uint8_t half_segs = 1 + (instance->_segment.intensity >> 6);
  if (half_segs > 4)
    half_segs = 4; // Safety clamp for 255
  uint32_t num_segments = half_segs * 2;

  // Measure phase per pixel so that num_segments fit exactly across 'len'
  // 1 segment = 65536 phase units. Total phase across strip = num_segments *
  // 65536.
  uint32_t total_base_phase = num_segments * 65536;

  // Dynamic Breathing
  // The size of the mirrors slowly expands and contracts.
  uint8_t breath_phase = (ms >> 6) & 0xFF; // Slow wave
  // Map sine to 0.7 - 1.3 (+/- 30% width variation)
  float breath_factor = 0.7f + (cfx::sin8(breath_phase) * 0.6f / 255.0f);

  // Phase step per pixel
  uint32_t total_dynamic_phase = (uint32_t)(total_base_phase * breath_factor);
  uint32_t phase_step = total_dynamic_phase / len;

  // === Palette ===
  const uint32_t *palette = getPaletteByIndex(instance->_segment.palette);
  // Handle solid color palette
  if (instance->_segment.palette == 255 || instance->_segment.palette == 21) {
    fillSolidPalette(instance->_segment.colors[0]);
  }

  // === Render Loop ===
  // Glint settings: glint is ~1.5 pixels wide
  uint32_t glint_radius = phase_step + (phase_step >> 1);

  for (int i = 0; i < len; i++) {
    uint32_t spatial_phase = i * phase_step;

    // Triangle wave fold
    uint16_t cycle = (spatial_phase >> 16);
    uint16_t fraction = spatial_phase & 0xFFFF;

    uint16_t folded_phase;
    if (cycle & 0x01) {
      // Odd cycle: backward phase
      folded_phase = 0xFFFF - fraction;
    } else {
      // Even cycle: forward phase
      folded_phase = fraction;
    }

    // Convert 0-65535 spatial phase to 0-255 color index
    uint8_t color_index = (uint8_t)((folded_phase >> 8) + cycle_time);

    // Get base kaleidoscope color
    CRGBW c = ColorFromPalette(palette, color_index, 255);

    // === Prism Glints (Option B) ===
    // Distance to nearest symmetry bound (0 or 65536 equivalent in fraction)
    uint16_t dist_to_bound = (fraction < 32768) ? fraction : (65535 - fraction);

    if (dist_to_bound < glint_radius) {
      // Identify the specific mirror seam to give each seam an independent
      // shimmer phase
      uint16_t seam_id = (spatial_phase + 32768) >> 16;
      uint8_t shimmer =
          cfx::sin8((ms >> 2) + (seam_id * 64)); // Independent seam shimmer

      // Falloff the glint based on exact sub-pixel distance so it doesn't jump
      uint8_t sub_shimmer =
          (shimmer * (glint_radius - dist_to_bound)) / glint_radius;

      // Additive blend white glint
      uint16_t r = c.r + sub_shimmer;
      uint16_t g = c.g + sub_shimmer;
      uint16_t b = c.b + sub_shimmer;
      uint16_t w = c.w + sub_shimmer;

      c.r = (r > 255) ? 255 : r;
      c.g = (g > 255) ? 255 : g;
      c.b = (b > 255) ? 255 : b;
      c.w = (w > 255) ? 255 : w;
    }

    // Draw
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
  }

  return FRAMETIME;
}

/*
 * Follow Me (ID 156)
 * Linear scanner with attention-grabbing strobe.
 * State Machine: STROBE_START â†’ MOVING â†’ STROBE_END â†’ RESTART
 * The cursor strobes at the start, travels with a fading trail,
 * strobes at the end, then fades out and restarts.
 */

// State constants
#define FM_PULSE_START 0
#define FM_MOVING 1
#define FM_STROBE_END 2
#define FM_RESTART 3

struct FollowMeData {
  float pos;                  // Current head position (sub-pixel)
  uint8_t state;              // Current state machine state
  uint32_t state_start_ms;    // Timestamp when current state began
  uint8_t restart_brightness; // For fade-out in RESTART state
};

uint16_t mode_follow_me(void) {
  if (!instance)
    return FRAMETIME;

  uint16_t len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // === Allocate State ===
  if (!instance->_segment.allocateData(sizeof(FollowMeData)))
    return mode_static();

  FollowMeData *fm = reinterpret_cast<FollowMeData *>(instance->_segment.data);

  // === Init on Reset ===
  if (instance->_segment.reset) {
    fm->pos = 0.0f;
    fm->state = FM_PULSE_START;
    fm->state_start_ms = cfx_millis();
    fm->restart_brightness = 255;
    instance->_segment.reset = false;
  }

  uint32_t now = cfx_millis();

  // === Cursor Size ===
  // ~10 pixels, but scale for short strips (min 3)
  int cursor_size = std::max(3, std::min(10, (int)len / 10));

  // === Palette (Ignored per request) ===
  fillSolidPalette(instance->_segment.colors[0]);
  const uint32_t *palette = getPaletteByIndex(255); // Solid Palette via ID 255

  // === Trail Fade (Subtractive) ===
  // Fixes persistence issue: Ensure we always subtract enough to clear the
  // floor. Intensity controls fade rate: High = slow fade (long trail), Low =
  // fast fade. We use scale8 to dim the whole strip, then subtract a constant
  // to kill low-level noise.

  // === Trail Fade (Manual Loop) ===
  // Replaces fadeToBlackBy to avoid gamma/floor issues.
  // 1. Scale (Dimming): Exponential decay. High intensity = slow fade.
  // 2. Subtract (Floor Cleaning): Hard subtraction to force zero.
  uint8_t scale = 255 - (instance->_segment.intensity >> 1); // 128..255
  // Increased subtraction for low intensity to fix persistent floor bug.
  // 60..89: sub 4. < 60: sub 6. <= 15: sub 8 (Very Aggressive). > 90: sub 2.
  uint8_t sub_val = (instance->_segment.intensity <= 15)  ? 8
                    : (instance->_segment.intensity < 60) ? 6
                    : (instance->_segment.intensity < 90) ? 4
                                                          : 2;

  int start = instance->_segment.start;
  int stop = instance->_segment.stop;
  esphome::light::AddressableLight &light = *instance->target_light;

  for (int i = start; i < stop; i++) {
    if (i < light.size()) {
      esphome::Color c = light[i].get();

      // 1. Scale
      c.r = cfx::scale8(c.r, scale);
      c.g = cfx::scale8(c.g, scale);
      c.b = cfx::scale8(c.b, scale);
      c.w = cfx::scale8(c.w, scale);

      // 2. Subtract (Floor Cleaning)
      c.r = (c.r > sub_val) ? (c.r - sub_val) : 0;
      c.g = (c.g > sub_val) ? (c.g - sub_val) : 0;
      c.b = (c.b > sub_val) ? (c.b - sub_val) : 0;
      c.w = (c.w > sub_val) ? (c.w - sub_val) : 0;

      // 3. Hard Cutoff (Final Cleanup)
      // Increased threshold to 20 for absolute clearance of low-level noise.
      if (c.r < 20)
        c.r = 0;
      if (c.g < 20)
        c.g = 0;
      if (c.b < 20)
        c.b = 0;
      if (c.w < 20)
        c.w = 0;

      light[i] = c;
    }
  }

  // === Strobe Frequency ===
  // Real Strobe: Short ON time, Long OFF time.
  // 4Hz (250ms period), 40ms ON pulse.
  // This guarantees >4 flashes in 1.5s (1500/250 = 6 flashes).
  const uint32_t STROBE_PERIOD_MS = 250;
  const uint32_t STROBE_ON_MS = 40;
  const uint32_t PULSE_DURATION_MS = 2000;
  const uint32_t STROBE_DURATION_MS = 1500;
  const uint32_t RESTART_DURATION_MS = 500;

  // === State Machine ===
  switch (fm->state) {

  case FM_PULSE_START: {
    // Breathing/pulsing cursor at position 0 (same as Follow Us FU_PULSE)
    uint8_t bri = beatsin8(60, 50, 255);
    for (int j = 0; j < cursor_size && j < len; j++) {
      uint8_t ci = (j * 255) / cursor_size;
      CRGBW c = ColorFromPalette(palette, ci, bri);
      instance->_segment.setPixelColor(j, RGBW32(c.r, c.g, c.b, c.w));
    }

    // Transition: After pulse duration, start moving
    if (now - fm->state_start_ms > PULSE_DURATION_MS) {
      fm->state = FM_MOVING;
      fm->pos = 0.0f;
      fm->state_start_ms = now;
    }
    break;
  }

  case FM_MOVING: {
    // === Movement Speed ===
    // Speed 0 = very slow (~0.2 px/frame), Speed 255 = fast (~6 px/frame)
    // Increased max speed by 50% (user request)
    float speed_factor = 0.2f + (instance->_segment.speed * 5.7f / 255.0f);
    fm->pos += speed_factor;

    int head = (int)fm->pos;
    int end_pos = len - cursor_size;

    // Draw cursor block
    for (int j = 0; j < cursor_size; j++) {
      int px = head + j;
      if (px >= 0 && px < len) {
        // Gradient across cursor for a polished look
        // Uses Solid Palette (forced above), so it's just brightness gradient
        // logic over solid color But palette is forced to solid, so
        // ColorFromPalette returns the solid color (with brightness scaled by
        // index if it was a gradient palette, but here it's solid). Actually,
        // ColorFromPalette with PaletteSolid returns the color regardless of
        // index (usually). Let's rely on standard behavior.
        uint8_t ci = (j * 255) / cursor_size;
        CRGBW c = ColorFromPalette(palette, ci, 255);
        instance->_segment.setPixelColor(px, RGBW32(c.r, c.g, c.b, c.w));
      }
    }

    // Transition: Cursor reached the end
    if (head >= end_pos) {
      fm->pos = (float)end_pos;
      fm->state = FM_STROBE_END;
      fm->state_start_ms = now;
    }
    break;
  }

  case FM_STROBE_END: {
    // Strobe the cursor at the END of the strip
    int end_start = len - cursor_size;
    if (end_start < 0)
      end_start = 0;

    bool strobe_on = (now % STROBE_PERIOD_MS) < STROBE_ON_MS;
    if (strobe_on) {
      for (int j = 0; j < cursor_size; j++) {
        int px = end_start + j;
        if (px < len) {
          // Use Palette Color (Solid) exclusively
          uint8_t ci = (j * 255) / cursor_size;
          CRGBW c = ColorFromPalette(palette, ci, 255);
          instance->_segment.setPixelColor(px, RGBW32(c.r, c.g, c.b, c.w));
        }
      }
    }

    // Transition: After strobe, start restart (fade out)
    if (now - fm->state_start_ms > STROBE_DURATION_MS) {
      fm->state = FM_RESTART;
      fm->state_start_ms = now;
      fm->restart_brightness = 255;
    }
    break;
  }

  case FM_RESTART: {
    // Gentle fade-out of whatever remains, then restart
    // fadeToBlackBy is already running at the top of the function.
    // We just wait for a short period for the strip to go dark.
    if (now - fm->state_start_ms > RESTART_DURATION_MS) {
      // Clear strip fully
      instance->_segment.fill(0);
      fm->state = FM_PULSE_START;
      fm->pos = 0.0f;
      fm->state_start_ms = now;
    }
    break;
  }

  } // switch

  return FRAMETIME;
}

/*
 * Follow Us (ID 157)
 * Multi-cursor variant of Follow Me.
 * Narrative:
 * 1. Pulse: Cursor appears at start and pulses for ~2s.
 * 2. Run: Splits into 3 parts that run sequentially to the other side.
 * 3. Reassemble: Parts arrive at staggered positions, reforming the cursor.
 * 4. Finale: The reassembled cursor strobes, then fades away.
 * 5. Restart: Brief blackout, then loop.
 *
 * Part 0 (lead) -> targets len - part_size        (rightmost)
 * Part 1         -> targets len - 2*part_size      (middle)
 * Part 2 (tail)  -> targets len - 3*part_size      (leftmost)
 * When all arrive, they form one contiguous 9-pixel block at the end.
 */

#define FU_PULSE 0
#define FU_RUN 1
#define FU_JOIN 2
#define FU_FINALE 3
#define FU_RESTART 4

struct CursorPart {
  float pos;
  bool active;
  bool arrived;
};

struct FollowUsData {
  uint8_t state;
  uint32_t state_start_ms;
  CursorPart parts[3];
};

uint16_t mode_follow_us(void) {
  if (!instance)
    return FRAMETIME;

  uint16_t len = instance->_segment.length();
  if (len <= 9)
    return mode_static();

  // === Allocate State ===
  if (!instance->_segment.allocateData(sizeof(FollowUsData)))
    return mode_static();

  FollowUsData *fu = reinterpret_cast<FollowUsData *>(instance->_segment.data);

  // === Init on Reset ===
  if (instance->_segment.reset) {
    fu->state = FU_PULSE;
    fu->state_start_ms = cfx_millis();
    for (int i = 0; i < 3; i++) {
      fu->parts[i].pos = (float)(i * 3); // Start positions: 0, 3, 6
      fu->parts[i].active = false;
      fu->parts[i].arrived = false;
    }
    instance->_segment.reset = false;
  }

  uint32_t now = cfx_millis();

  // === Cursor Config ===
  const int part_size = 3;
  const int num_parts = 3;
  const int cursor_total = part_size * num_parts; // 9 pixels
  // Intensity slider controls gap between sub-cursor launches (0=tight,
  // 255=wide)
  const int run_gap = 4 + (instance->_segment.intensity * 36 / 255);

  // Launch order: 2 -> 1 -> 0 (back of cursor peels off first)
  // Arrival targets: part 2 arrives rightmost, part 0 arrives leftmost
  // so they reassemble into the original 9px block at the end.
  int targets[3];
  targets[2] = len - part_size;     // part 2 (first to launch) -> rightmost
  targets[1] = len - 2 * part_size; // part 1 -> middle
  targets[0] = len - 3 * part_size; // part 0 (last to launch)  -> leftmost

  // === Solid Color (No Palette) ===
  uint32_t color0 = instance->_segment.colors[0];
  CRGBW solid_color(color0);

  // === Trail Fade (Background Cleanup) ===
  // Removed explicit trail fade here. The global fadeToBlackBy handles it.

  // === Timing Constants ===
  const uint32_t PULSE_DURATION_MS = 2000;
  const uint32_t JOIN_DELAY_MS = 600; // pause after all parts arrive
  const uint32_t STROBE_PERIOD_MS = 250;
  const uint32_t STROBE_ON_MS = 40;
  const uint32_t FINALE_DURATION_MS = 1500;
  const uint32_t RESTART_DURATION_MS = 500;

  // === Helper: Draw a part at position ===
  auto draw_part = [&](int pos, uint8_t bri) {
    for (int k = 0; k < part_size; k++) {
      int px = pos + k;
      if (px >= 0 && px < len) {
        instance->_segment.setPixelColor(
            px, RGBW32(cfx::scale8(solid_color.r, bri),
                       cfx::scale8(solid_color.g, bri),
                       cfx::scale8(solid_color.b, bri),
                       cfx::scale8(solid_color.w, bri)));
      }
    }
  };

  // === State Machine ===
  switch (fu->state) {

  case FU_PULSE: {
    // Pulsing cursor at start (all 3 parts together = 9px block)
    uint8_t bri = beatsin8(60, 50, 255);
    for (int k = 0; k < cursor_total && k < len; k++) {
      instance->_segment.setPixelColor(k,
                                       RGBW32(cfx::scale8(solid_color.r, bri),
                                              cfx::scale8(solid_color.g, bri),
                                              cfx::scale8(solid_color.b, bri),
                                              cfx::scale8(solid_color.w, bri)));
    }

    if (now - fu->state_start_ms > PULSE_DURATION_MS) {
      fu->state = FU_RUN;
      fu->state_start_ms = now;
      // Launch order: 2 -> 1 -> 0 (back peels off first)
      // Part 2 launches immediately; parts 1 & 0 wait.
      fu->parts[0].active = false;
      fu->parts[0].pos = 0.0f; // front, launches last
      fu->parts[0].arrived = false;
      fu->parts[1].active = false;
      fu->parts[1].pos = (float)part_size; // middle, launches second
      fu->parts[1].arrived = false;
      fu->parts[2].active = true;
      fu->parts[2].pos = (float)(2 * part_size); // back, launches first
      fu->parts[2].arrived = false;
    }
    break;
  }

  case FU_RUN: {
    // Hard clear every frame — no tails
    instance->_segment.fill(0);

    // Speed: maps slider 0-255 to 0.3 - 4.0 px/frame
    float base_speed = 0.3f + (instance->_segment.speed * 3.7f / 255.0f);

    for (int i = 0; i < num_parts; i++) {
      // Move active, non-arrived parts
      if (fu->parts[i].active && !fu->parts[i].arrived) {
        fu->parts[i].pos += base_speed;
        if (fu->parts[i].pos >= (float)targets[i]) {
          fu->parts[i].pos = (float)targets[i];
          fu->parts[i].arrived = true;
        }
      }

      // Trigger chain: 2 triggers 1, then 1 triggers 0
      // Each part triggers the one with index-1 (moving toward front)
      if (i > 0 && fu->parts[i].active && !fu->parts[i - 1].active) {
        // Trigger when this part has moved gap pixels ahead of part i-1's start
        float launch_threshold = (float)(i * part_size + run_gap);
        if (fu->parts[i].pos > launch_threshold) {
          fu->parts[i - 1].active = true;
          fu->parts[i - 1].pos = (float)((i - 1) * part_size);
        }
      }

      // Draw ALL parts: active ones at current pos, inactive ones at start pos
      draw_part((int)fu->parts[i].pos, 255);
    }

    // All arrived? -> JOIN delay before strobe
    if (fu->parts[0].arrived && fu->parts[1].arrived && fu->parts[2].arrived) {
      fu->state = FU_JOIN;
      fu->state_start_ms = now;
    }
    break;
  }

  case FU_JOIN: {
    // Brief pause after all parts arrive — hold the reassembled cursor steady
    for (int i = 0; i < num_parts; i++) {
      draw_part(targets[i], 255);
    }
    if (now - fu->state_start_ms > JOIN_DELAY_MS) {
      fu->state = FU_FINALE;
      fu->state_start_ms = now;
    }
    break;
  }

  case FU_FINALE: {
    // Strobe the reassembled cursor at the end
    bool strobe_on = (now % STROBE_PERIOD_MS) < STROBE_ON_MS;
    if (strobe_on) {
      for (int i = 0; i < num_parts; i++) {
        draw_part(targets[i], 255);
      }
    } else {
      instance->_segment.fill(0); // Hard off between strobes
    }

    if (now - fu->state_start_ms > FINALE_DURATION_MS) {
      fu->state = FU_RESTART;
      fu->state_start_ms = now;
    }
    break;
  }

  case FU_RESTART: {
    // Let the fade clean up, then restart
    if (now - fu->state_start_ms > RESTART_DURATION_MS) {
      instance->_segment.fill(0);
      fu->state = FU_PULSE;
      fu->state_start_ms = now;
      for (int i = 0; i < 3; i++) {
        fu->parts[i].pos = (float)(i * part_size);
        fu->parts[i].active = false;
        fu->parts[i].arrived = false;
      }
    }
    break;
  }

  } // switch

  return FRAMETIME;
}

// --- Fluid Rain (ID 160) ---
// Subtle moving water surface + physical drop sequences (fall -> impact ->
// ripple) Zero allocated buffers — safe for multi-strip operation
#define FLUID_RAIN_NUM_DROPS 5
uint16_t mode_fluid_rain(void) {
  if (!instance)
    return FRAMETIME;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  uint8_t speed = instance->_segment.speed;
  uint8_t intensity = instance->_segment.intensity;

  // Parameter mapping:
  // User prefers Speed ~70 and Intensity ~40 as the "default" look.
  // WLED defaults are 128. We scale 128 down to 70/40 via math.
  // 128 * 140 / 256 ≈ 70
  uint32_t eff_speed = (speed * 140) >> 8;

  // Time base (slowed way down for ultimate smoothness)
  uint32_t now = cfx_millis();
  uint32_t t = (now * (eff_speed + 1) * 200) >> 17;

  // === SUBTLE BACKGROUND WATER SURFACE ===
  uint16_t wave1 = t;
  uint16_t wave2 = -(t * 2);

  // === RAIN DROP SYSTEM ===
  // Cycle has 2 phases:
  // 1. Impact flash (brief white flash at center)
  // 2. Expanding ripple (grows outward, then fades)
  // Intensity mapping: 128 * 60 / 256 = 30. 300 - 30 = 270 cycle length.
  uint16_t cycle_len = 300 - ((intensity * 60) >> 8); // 300 to 240 units total

  // Palette
  const uint32_t *pal = getPaletteByIndex(instance->_segment.palette);
  bool is_solid = (instance->_segment.palette == 255);
  uint32_t solid_color = is_solid ? instance->_segment.colors[0] : 0;

  // Pre-compute drop states
  struct {
    int center;    // Impact location in 16-bit sub-pixel space (pixel * 256)
    uint8_t phase; // 1=impact, 2=ripple spreading, 3=fade
    uint16_t ripple_rad; // Current radius in 16-bit sub-pixel space
    uint8_t bright;      // Current brightness of the active element
  } drops[FLUID_RAIN_NUM_DROPS];

  for (int d = 0; d < FLUID_RAIN_NUM_DROPS; d++) {
    uint32_t drop_t = t + d * (cycle_len / FLUID_RAIN_NUM_DROPS);
    uint16_t c_phase = drop_t % cycle_len;
    uint16_t cycle_num = drop_t / cycle_len;

    // Impact center: random per cycle. Stored as sub-pixel position (pixel *
    // 256)
    uint16_t center_pixel =
        (sin8((uint8_t)(cycle_num * 37 + d * 73)) * (len - 14)) >> 8;
    center_pixel += 7;                   // Generous margin
    drops[d].center = center_pixel << 8; // Convert to sub-pixel space

    // Phase timing thresholds
    uint16_t t_ripple = cycle_len / 5; // 20% time spent as impact flash
    uint16_t t_fade = cycle_len - (cycle_len / 3); // Fade during last 33%

    if (c_phase < t_ripple) {
      // 1. IMPACT: bright flash at center that quickly dims
      drops[d].phase = 1;
      drops[d].bright = 255 - (255 * c_phase / t_ripple);

    } else {
      // 2 & 3. RIPPLE: expanding ring
      drops[d].phase = (c_phase < t_fade) ? 2 : 3;

      // Radius grows over time, smoothly due to sub-pixel math.
      // Every step of c_phase expands radius by a fractional amount
      uint16_t time_in_ripple = c_phase - t_ripple;

      // Maximum desired radius before fade out (e.g. 15 pixels)
      // distance = (time / duration) * max_distance * 256
      uint16_t expansion_duration = cycle_len - t_ripple;
      drops[d].ripple_rad = (time_in_ripple * 15 * 256) / expansion_duration;

      // Brightness envelope
      if (drops[d].phase == 2) {
        drops[d].bright = 220; // Strong ripple
      } else {
        // Fade out
        uint16_t time_in_fade = c_phase - t_fade;
        uint16_t fade_duration = cycle_len - t_fade;
        drops[d].bright = 220 - (220 * time_in_fade / fade_duration);
      }
    }
  }

  // === SINGLE RENDER LOOP ===
  for (int i = 0; i < len; i++) {
    uint16_t spatial = i * 256;

    // Background: subtle water surface
    uint8_t w1 = sin8(((spatial >> 1) + wave1) >> 8);
    uint8_t w2 = sin8(((spatial >> 2) + wave2) >> 8);
    uint8_t base = ((uint16_t)w1 + (uint16_t)w2) >> 3; // 0-48
    uint32_t c;

    // Check for drop interactions
    uint8_t white_add = 0; // Pure white flash (impacts)
    uint8_t color_add = 0; // Colored ripple

    // i in sub-pixel space for accurate math
    int i_sub = i << 8;

    for (int d = 0; d < FLUID_RAIN_NUM_DROPS; d++) {
      int dist = abs(i_sub - drops[d].center) >> 8; // Integer pixel distance
      if (drops[d].phase == 1) {
        // Impact flash (sharp point at center)
        if (dist == 0)
          white_add = qadd8(white_add, drops[d].bright);
        else if (dist == 1)
          white_add = qadd8(white_add, drops[d].bright >> 1);
      } else {
        // While the ripple expands, keep a persistent white dot at the center
        // so the user knows where the ripple originated.
        if (dist == 0) {
          white_add = qadd8(white_add, drops[d].bright);
        } else if (dist == 1) {
          white_add = qadd8(white_add, drops[d].bright >> 2);
        }

        // ANTI-ALIASED RIPPLE RING
        // Distance from center to current pixel (in sub-pixels)
        int dist_sub = abs(i_sub - drops[d].center);

        // Distance from pixel to the exactly ideal ring radius (in sub-pixels)
        int ring_dist_sub = abs(dist_sub - drops[d].ripple_rad);

        // WIDENED RIPPLE: Render a ring 4.0 pixels wide (1024 subpixels) for
        // smooth blending If ring_dist_sub is 0, brightness is 100%. If
        // ring_dist_sub is 1024 (4 pixels wide total), brightness is 0%.
        if (ring_dist_sub < 1024) {
          // Inverse linear falloff from center of the ring
          uint8_t intensity_scale = 255 - (ring_dist_sub >> 2);
          uint8_t pixel_bri = (drops[d].bright * intensity_scale) >> 8;
          color_add = qadd8(color_add, pixel_bri);
        }
      }
    }

    // Minimum floor + palette mapping for the base water + ripple
    uint8_t pal_index = base;
    pal_index = qadd8(pal_index, color_add);
    pal_index = (pal_index < 12) ? 12 : pal_index;

    // Get color from palette
    if (is_solid) {
      CRGBW sc(solid_color);
      c = RGBW32((sc.r * pal_index) >> 8, (sc.g * pal_index) >> 8,
                 (sc.b * pal_index) >> 8, (sc.w * pal_index) >> 8);
    } else {
      CRGBW cWLED = ColorFromPalette(pal, pal_index, 255);

      // Inject the pure white impacts ON TOP of the palette color
      if (white_add > 0) {
        // Add white directly to RGB channels
        uint8_t r = qadd8(cWLED.r, white_add);
        uint8_t g = qadd8(cWLED.g, white_add);
        uint8_t b = qadd8(cWLED.b, white_add);
        // And use the white channel if available
        uint8_t w = qadd8(cWLED.w, white_add);
        c = RGBW32(r, g, b, w);
      } else {
        c = RGBW32(cWLED.r, cWLED.g, cWLED.b, cWLED.w);
      }
    }

    instance->_segment.setPixelColor(i, c);
  }

  return FRAMETIME;
}

// Valid Palette Implementation (Moved from line 121)
uint32_t Segment::color_from_palette(uint16_t i, bool mapping, bool wrap,
                                     uint8_t mcol, uint8_t pbri) {
  // Ensure the Solid Palette cache is populated properly with the W-channel
  // included
  if (this->palette == 255 || this->palette == 0) {
    fillSolidPalette(this->colors[0]);
  }

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
    uint8_t w = ((color >> 24) & 0xFF) * pbri / 255;
    return RGBW32(r, g, b, w);
  }
  return color;
}
