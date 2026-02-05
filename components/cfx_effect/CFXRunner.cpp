/*
 * Copyright (c) 2026 Federico Leoni
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

// Static instance pointer
CFXRunner *instance = nullptr;

// Global time provider for FastLED timing functions
uint32_t get_millis() { return instance ? instance->now : cfx_millis(); }

// Constructor
CFXRunner::CFXRunner(esphome::light::AddressableLight *light) {
  target_light = light;
  instance = this;
  _mode = FX_MODE_STATIC;
  frame_time = 0;

  // Initialize Segment defaults
  _segment.start = 0;
  _segment.stop = light->size();
  _segment.mode = FX_MODE_STATIC;
  _segment.speed = DEFAULT_SPEED;
  _segment.intensity = DEFAULT_INTENSITY;
  _segment.palette = 0;
  _segment.colors[0] = DEFAULT_COLOR; // Orange default
}

// --- helper functions ---

// Simple time-based service loop - Moved to end of file to see effect functions

// --- Segment Implementation ---

// Set Pixel Color (Direct write to ESPHome buffer)
void Segment::setPixelColor(int n, uint32_t c) {
  if (n < 0 || n >= length())
    return;

  // Map usage to global buffer - apply mirror (inversion) if enabled
  int global_index = mirror ? (stop - 1 - n) : (start + n);

  // Check bounds against light size
  // Optimization: Cache instance and light locally if possible, but for single
  // pixel set this is acceptable
  if (instance && instance->target_light && global_index >= 0 &&
      global_index < instance->target_light->size()) {
    esphome::Color esphome_color(R(c), G(c), B(c), W(c));
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
  esphome::Color esphome_color(R(c), G(c), B(c), W(c));

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

  int len = length();
  int light_size = instance->target_light->size();
  int global_start = start;
  esphome::light::AddressableLight &light = *instance->target_light;

  for (int i = 0; i < len; i++) {
    int global_index = global_start + i;
    if (global_index < light_size) {
      // Read directly from ESPHome buffer for speed
      esphome::Color c = light[global_index].get();
      c.r = (c.r * (255 - fadeBy)) >> 8;
      c.g = (c.g * (255 - fadeBy)) >> 8;
      c.b = (c.b * (255 - fadeBy)) >> 8;
      c.w = (c.w * (255 - fadeBy)) >> 8;
      light[global_index] = c;
    }
  }
}

// Stub removed. Implementation moved to end of file to access static palette
// tables.

// --- Effect Implementations ---

// --- Aurora Effect Dependencies ---

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
// PALETTE SYSTEM - 21 Palettes stored in Flash (PROGMEM)
// Uses CFX_PROGMEM for ESP-IDF/Arduino compatibility (~1.3KB RAM saved)
// ============================================================================

// Palette 0: Aurora - Green/Teal/Cyan gradient (ideal for Aurora effect)
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

// Palette 2: Ocean - Deep to light blues
static const uint32_t PaletteOcean[16] CFX_PROGMEM = {
    0x000032, 0x000050, 0x001464, 0x003296, 0x0050C8, 0x0078DC,
    0x0096FF, 0x32C8FF, 0x64DCFF, 0x96F0FF, 0x64DCFF, 0x32C8FF,
    0x0096FF, 0x0064C8, 0x003296, 0x001464};

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

// Palette 10: Pacifica - Deep ocean blues with white crests
static const uint32_t PalettePacifica[16] CFX_PROGMEM = {
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
    return PaletteOcean;
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
    return PalettePacifica;
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

  uint8_t r = r1 + (((r2 - r1) * f) >> 4);
  uint8_t g = g1 + (((g2 - g1) * f) >> 4);
  uint8_t b = b1 + (((b2 - b1) * f) >> 4);

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
    // FIX: Range must be <= 100 to avoid overflowing uint16_t basealpha with
    // AW_SCALE High floor > 50 reduces voids.
    basealpha = hw_random8(50, 100) * AW_SCALE / 100;
    age = 0;
    width =
        hw_random16(segment_length / 20, segment_length / W_WIDTH_FACTOR) + 1;
    center = (((uint32_t)hw_random8(101) << AW_SHIFT) / 100) * segment_length;
    goingleft = hw_random8() & 0x01;

    // Save random speed variance (10-31)
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

    // Calculate step: variance * constants * speed
    // FIX: Re-introduced Divisor (4) to slow down effect to proper visual speed
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

uint16_t mode_blink(void) {
  if (!instance)
    return 350;

  // BLINK (ID 1)
  // Simple periodic On/Off effect.
  // Speed sets frequency. Intensity sets duty cycle.

  // Remap speed to 0-210 range to prevent irregular beats at high frequencies
  // as per user request (limits max frequency to avoid aliasing artifacts).
  uint16_t speed = (instance->_segment.speed * 210) / 255;
  uint16_t intensity = instance->_segment.intensity;

  // Frequency range:
  // Speed 0: 2000ms period
  // Speed 255 (mapped to 210): 2000 - (210 * 7) = 530ms period (~2Hz)
  uint32_t cycleTime = 2000 - (speed * 7);

  uint32_t prog = instance->now % cycleTime;

  // Duty cycle threshold:
  // Inteinsity 0: Short blip (10%)
  // Intensity 128: Square wave (50%)
  // Intensity 255: Mostly on (90%)
  uint32_t threshold = (cycleTime * (intensity + 25)) / 300;
  // Map 0..255 -> ~8..93% duty roughly.

  bool on = (prog < threshold);

  if (on) {
    if (instance->_segment.palette == 0 || instance->_segment.palette == 255) {
      instance->_segment.fill(instance->_segment.colors[0]);
    } else {
      const uint32_t *active_palette =
          getPaletteByIndex(instance->_segment.palette);
      // Map entire palette to strip for "On" state
      uint16_t len = instance->_segment.length();
      for (int i = 0; i < len; i++) {
        uint8_t colorIndex = (i * 255) / len;
        CRGBW c = ColorFromPalette(colorIndex, 255, active_palette);
        instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
      }
    }
  } else {
    instance->_segment.fill(instance->_segment.colors[1]); // Background
  }

  return FRAMETIME;
}

uint16_t mode_aurora(void) {
  AuroraWave *waves;

  // DEBUG: Log removed per user request

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

  // FIX: Allocation: ALWAYS allocate max count to prevent reallocation/reset
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

      waves[i].update(instance->_segment.length(), instance->_segment.speed);

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

    instance->_segment.setPixelColor(
        i, RGBW32(mixedRgb.r, mixedRgb.g, mixedRgb.b, mixedRgb.w));
  }

  return FRAMETIME;
}

// ============================================================================
// AURORA NATIVE - Proof of Concept for native WLED timing
// Uses speed accumulator pattern, no frame rate compensation hacks
// ============================================================================

// Native wave struct matching WLED original formula (no 0.66 speed scaling)
struct AuroraWaveNative {
  int32_t center;
  uint32_t ageFactor_cached;
  uint16_t ttl;
  uint16_t age;
  uint16_t width;
  uint16_t basealpha;
  uint16_t speed_factor; // Matches WLED's calculation
  int16_t wave_start;
  int16_t wave_end;
  bool goingleft;
  bool alive;
  CRGBW basecolor;

  void init(uint32_t segment_length, CRGBW color) {
    ttl = hw_random16(500, 1501);
    basecolor = color;
    basealpha =
        hw_random8(50, 100) * AW_SCALE / 100; // High floor reduces voids
    age = 0;
    width =
        hw_random16(segment_length / 20, segment_length / W_WIDTH_FACTOR) + 1;
    center = (((uint32_t)hw_random8(101) << AW_SHIFT) / 100) * segment_length;
    goingleft = hw_random8() & 0x01;
    // WLED original formula: no extra divisors
    speed_factor = (((uint32_t)hw_random8(10, 31) * W_MAX_SPEED) << AW_SHIFT) /
                   (100 * 255);
    alive = true;
  }

  void updateCachedValues() {
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

  // NATIVE: Uses raw speed like WLED original (no 0.66 scaling hack)
  void update(uint32_t segment_length, uint32_t speed) {
    int32_t step = speed_factor * speed;
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

// Static state for Aurora Native
static uint32_t aurora_native_speed_accum = 0;

uint16_t mode_aurora_native(void) {
  AuroraWaveNative *waves;

  // === SPEED ACCUMULATOR (same pattern as PacificaNative) ===
  uint8_t speed = instance->_segment.speed;
  uint32_t should_update = 0;
  if (speed > 0) {
    aurora_native_speed_accum += speed;
    should_update = aurora_native_speed_accum >> 8;
    aurora_native_speed_accum &= 0xFF;
  }

  // Intensity mapping (same as original)
  uint8_t selector = instance->_segment.intensity;
  uint8_t internal_intensity;
  if (selector <= 128) {
    internal_intensity = (uint32_t)selector * 175 / 128;
  } else {
    internal_intensity = 175 + ((uint32_t)(selector - 128) * 80 / 127);
  }

  int active_count = 2 + ((internal_intensity * (W_MAX_COUNT - 2)) / 255);
  instance->_segment.aux1 = active_count;

  if (!instance->_segment.allocateData(sizeof(AuroraWaveNative) *
                                       W_MAX_COUNT)) {
    return mode_static();
  }

  if (instance->_segment.reset) {
    memset(instance->_segment.data, 0, instance->_segment._dataLen);
    instance->_segment.reset = false;
  }

  waves = reinterpret_cast<AuroraWaveNative *>(instance->_segment.data);

  // Calculate effective speed for wave updates
  // Accumulator tells us how much "virtual" speed to apply this frame
  uint32_t effective_speed = should_update > 0 ? speed : 0;

  // Service waves - ALWAYS update for smooth particle motion
  for (int i = 0; i < W_MAX_COUNT; i++) {
    if (waves[i].ttl == 0)
      waves[i].alive = false;

    if (waves[i].alive) {
      if (i >= active_count) {
        waves[i].basealpha = (waves[i].basealpha * 224) >> 8;
        if (waves[i].basealpha < 10)
          waves[i].alive = false;
      }

      // CRITICAL: Update every frame for smooth particle motion
      // Pass effective_speed which is 0 when accumulator hasn't ticked
      waves[i].update(instance->_segment.length(), effective_speed);

      if (!waves[i].stillAlive()) {
        if (i < active_count) {
          uint8_t colorIndex = rand() % 256;
          const uint32_t *active_palette =
              getPaletteByIndex(instance->_segment.palette);
          CRGBW color = ColorFromPalette(colorIndex, 255, active_palette);
          waves[i].init(instance->_segment.length(), color);
        }
      }
    } else {
      if (i < active_count) {
        uint8_t colorIndex = rand() % 256;
        const uint32_t *active_palette =
            getPaletteByIndex(instance->_segment.palette);
        CRGBW color = ColorFromPalette(colorIndex, 255, active_palette);
        waves[i].init(instance->_segment.length(), color);
      }
    }

    if (waves[i].alive)
      waves[i].updateCachedValues();
  }

  // Render
  CRGBW background(0, 0, 0, 0);
  for (int i = 0; i < instance->_segment.length(); i++) {
    CRGBW mixedRgb = background;
    for (int j = 0; j < W_MAX_COUNT; j++) {
      if (waves[j].alive) {
        CRGBW rgb = waves[j].getColorForLED(i);
        mixedRgb = color_add(mixedRgb, rgb);
      }
    }
    instance->_segment.setPixelColor(
        i, RGBW32(mixedRgb.r, mixedRgb.g, mixedRgb.b, mixedRgb.w));
  }

  return FRAMETIME;
}

// --- Fire2012 Effect ---
// Exact WLED implementation by Mark Kriegsman
// Adapted for ESPHome framework
uint16_t mode_fire_2012(void) {
  if (!instance)
    return 350;

  int len = instance->_segment.length();
  if (len <= 1)
    return mode_static();

  // Allocate heat array
  if (!instance->_segment.allocateData(len))
    return mode_static();
  uint8_t *heat = instance->_segment.data;

  // Speed scaling: 128 ESPHome → 83 WLED internal (60fps → 42fps adjustment)
  uint8_t user_speed = instance->_segment.speed;
  uint8_t wled_speed = (user_speed <= 128)
                           ? (user_speed * 83 / 128)
                           : (83 + ((user_speed - 128) * 172 / 127));

  // WLED uses time-based throttling
  const uint32_t it = instance->now >> 5; // div 32

  // Ignition area: 10% of segment length or minimum 3 pixels
  const uint8_t ignition = max(3, len / 10);

  // Step 1. Cool down every cell a little
  for (int i = 0; i < len; i++) {
    uint8_t cool = (it != instance->_segment.step)
                       ? random8((((20 + wled_speed / 3) * 10) / len) +
                                 2) // Reduced from 16 to 10 for taller flames
                       : random8(4);
    uint8_t minTemp = (i < ignition) ? (ignition - i) / 4 + 16
                                     : 0; // don't black out ignition area
    uint8_t temp = qsub8(heat[i], cool);
    heat[i] = (temp < minTemp) ? minTemp : temp;
  }

  if (it != instance->_segment.step) {
    // Step 2. Heat from each cell drifts 'up' and diffuses
    for (int k = len - 1; k > 1; k--) {
      heat[k] = (heat[k - 1] + (heat[k - 2] << 1)) / 3; // heat[k-2] * 2
    }

    // Step 3. Randomly ignite new 'sparks' near bottom
    if (random8() <= instance->_segment.intensity) {
      uint8_t y = random8(ignition);
      uint8_t boost = 17 * (ignition - y / 2) / ignition; // WLED default boost
      heat[y] = qadd8(heat[y], random8(96 + 2 * boost, 207 + boost));
    }
  }

  // Step 4. Map heat to LED colors using default fire gradient
  for (int j = 0; j < len; j++) {
    uint8_t t = min(heat[j], (uint8_t)240);
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

// --- Pacifica Effect ---
// Exact WLED implementation by Mark Kriegsman
// Gentle ocean waves - December 2019
// OPTIMIZED: Pre-computed palette caches for fast lookup

// Static palette caches - 256 entries each, initialized once
// Using only 2 palettes (simplified from 4 layers)
static CRGB pacifica_cache_1[256];
static CRGB pacifica_cache_2[256];
static bool pacifica_caches_initialized = false;

// Initialize palette caches once (called on first frame)
static void pacifica_init_caches() {
  if (pacifica_caches_initialized)
    return;

  // Palette 1: Deep ocean blues transitioning to cyan-green
  static const CRGBPalette16 pal1 = {0x000507, 0x000409, 0x00030B, 0x00030D,
                                     0x000210, 0x000212, 0x000114, 0x000117,
                                     0x000019, 0x00001C, 0x000026, 0x000031,
                                     0x00003B, 0x000046, 0x14554B, 0x28AA50};

  // Palette 2: Similar but with different cyan-green highlights
  static const CRGBPalette16 pal2 = {0x000507, 0x000409, 0x00030B, 0x00030D,
                                     0x000210, 0x000212, 0x000114, 0x000117,
                                     0x000019, 0x00001C, 0x000026, 0x000031,
                                     0x00003B, 0x000046, 0x0C5F52, 0x19BE5F};

  // Pre-compute all 256 interpolated colors for each palette
  for (int i = 0; i < 256; i++) {
    pacifica_cache_1[i] = ColorFromPalette(pal1, i, 255, LINEARBLEND);
    pacifica_cache_2[i] = ColorFromPalette(pal2, i, 255, LINEARBLEND);
  }

  pacifica_caches_initialized = true;
}

// Helper: Add one layer of waves using cached palette lookup with LERP
// interpolation cache parameter: 1 = palette1, 2 = palette2 OPTIMIZED: Linear
// interpolation between cache entries eliminates quantization flicker
static void pacifica_one_layer_cached(CRGB &c, uint16_t i, uint8_t cache_id,
                                      uint16_t cistart, uint16_t wavescale,
                                      uint8_t bri, uint16_t ioff) {
  uint16_t ci = cistart;
  uint16_t waveangle = ioff;
  uint16_t wavescale_half = (wavescale >> 1) + 20;

  // FIXED: Use gentler spatial scaling to prevent acceleration at strip end
  // Original: (120 + intensity) * i = 0-38,000 range at i=265 → too fast!
  // New: Much smaller base + intensity/4 for subtle control
  uint16_t spatial_step =
      32 + (instance->_segment.intensity >> 2); // 32-95 range
  waveangle += (spatial_step * i);

  uint16_t s16 = sin16_t(waveangle) + 32768;
  uint16_t cs = scale16(s16, wavescale_half) + wavescale_half;
  // Add base offset (64) so first pixels (i=0) still get variation
  // Use >> 4 (divide by 16) for gentler progression across strip
  // Range: i=0 gives cs*4, i=265 gives cs*20.5 (ratio ~5x, not infinite)
  // Use uint32_t to prevent overflow: cs * 329 can exceed 65535
  ci += (uint16_t)(((uint32_t)cs * (i + 64)) >> 4);

  // Get full 16-bit sine for interpolation
  uint16_t sindex16_raw = sin16_t(ci) + 32768;

  // High byte = cache index, remainder = fractional for LERP
  uint8_t index_lo = sindex16_raw >> 8; // 0-255 cache index
  uint8_t frac = sindex16_raw & 0xFF;   // 0-255 fractional
  uint8_t index_hi = index_lo + 1;      // Wraps naturally at 255→0

  // Scale to 240 range like original (preserves WLED color mapping)
  index_lo = scale8(index_lo, 240);
  index_hi = scale8(index_hi, 240);

  // Get adjacent cache entries
  CRGB *cache = (cache_id == 1) ? pacifica_cache_1 : pacifica_cache_2;
  CRGB lo = cache[index_lo];
  CRGB hi = cache[index_hi];

  // LERP with SMOOTHSTEP for organic color transitions
  // Smoothstep formula: 3*t^2 - 2*t^3 creates S-curve (ease-in-out)
  // Use uint32_t to prevent overflow: 255 * 255 * 258 = 16.7M > 65535
  uint32_t frac32 = frac;
  uint32_t smooth32 = (frac32 * frac32 * (768 - 2 * frac32)) >> 16;
  uint8_t sfrac = (uint8_t)smooth32; // Smoothed fraction 0-255

  CRGB layer;
  layer.r = lo.r + (((int16_t)(hi.r - lo.r) * sfrac) >> 8);
  layer.g = lo.g + (((int16_t)(hi.g - lo.g) * sfrac) >> 8);
  layer.b = lo.b + (((int16_t)(hi.b - lo.b) * sfrac) >> 8);

  // Apply brightness scaling
  layer.r = scale8(layer.r, bri);
  layer.g = scale8(layer.g, bri);
  layer.b = scale8(layer.b, bri);

  // Additive blending: qadd8 to existing color components
  c.r = qadd8(c.r, layer.r);
  c.g = qadd8(c.g, layer.g);
  c.b = qadd8(c.b, layer.b);
}

// Helper: Add whitecaps to peaks
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

uint16_t mode_pacifica() {
  if (!instance)
    return 350;

  // Initialize palette caches on first call
  pacifica_init_caches();

  int len = instance->_segment.length();
  uint32_t nowOld = instance->now;

  // Get persistent state
  unsigned sCIStart1 = instance->_segment.aux0;
  unsigned sCIStart2 = instance->_segment.aux1;
  unsigned sCIStart3 = instance->_segment.step & 0xFFFF;
  unsigned sCIStart4 = (instance->_segment.step >> 16);

  // === WLED-FAITHFUL SPEED HANDLING ===
  // WLED uses actual deltams from elapsed time, scaled by speed
  // We simulate this with speed-controlled virtual time
  uint8_t speed = instance->_segment.speed;

  // Speed 0 = frozen, speed 1 = very slow, speed 128 = normal, speed 255 = fast
  // Use PER-INSTANCE accumulator for sub-frame precision at low speeds
  // (Prevents cross-strip interference when multiple strips run Pacifica)

  // Add speed to accumulator each frame (56 FPS)
  // At speed=1: 56 per second → takes ~4.5 seconds to accumulate 256
  // At speed=128: 7168 per second → accumulates 256 in ~35ms (fast)
  // At speed=255: 14280 per second → very fast
  instance->pacifica_speed_accum += speed;

  // Consume whole units from accumulator for deltams
  uint32_t deltams = instance->pacifica_speed_accum >> 8; // Integer part
  instance->pacifica_speed_accum &= 0xFF; // Keep fractional part

  // Virtual time for beat functions (per-instance)
  instance->pacifica_virtual_time +=
      deltams; // Only advances when speed accumulates enough
  if (instance->pacifica_virtual_time < 1)
    instance->pacifica_virtual_time = 1;

  instance->now = instance->pacifica_virtual_time;

  // Update wave layer positions using WLED's exact logic
  uint32_t t = instance->now;
  unsigned speedfactor1 = beatsin16_t(3, 179, 269, t);
  unsigned speedfactor2 = beatsin16_t(4, 179, 269, t);
  uint32_t deltams1 = (deltams * speedfactor1) >> 8;
  uint32_t deltams2 = (deltams * speedfactor2) >> 8;
  uint32_t deltams21 = (deltams1 + deltams2) >> 1;

  sCIStart1 += (deltams1 * beatsin88_t(1011, 10, 13, t));
  sCIStart2 -= (deltams21 * beatsin88_t(777, 8, 11, t));
  sCIStart3 -= (deltams1 * beatsin88_t(501, 5, 7, t));
  sCIStart4 -= (deltams2 * beatsin88_t(257, 4, 6, t));

  // Save state
  instance->_segment.aux0 = sCIStart1;
  instance->_segment.aux1 = sCIStart2;
  instance->_segment.step = (sCIStart4 << 16) | (sCIStart3 & 0xFFFF);

  unsigned basethreshold = beatsin8_t(9, 55, 65, t);
  unsigned wave = beat8(7, t);

  // Wave parameters - WLED original
  uint16_t w1_scale = beatsin16_t(3, 11 * 256, 14 * 256, t);
  uint8_t w1_bri = beatsin8_t(10, 70, 130, t);
  uint16_t w1_off = 0 - beat16(301, t);

  uint16_t w2_scale = beatsin16_t(4, 6 * 256, 9 * 256, t);
  uint8_t w2_bri = beatsin8_t(17, 40, 80, t);
  uint16_t w2_off = beat16(401, t);

  uint16_t w3_scale = beatsin16_t(5, 8 * 256, 12 * 256, t);
  uint8_t w3_bri = beatsin8_t(13, 50, 100, t);
  uint16_t w3_off = beat16(503, t);

  for (int i = 0; i < len; i++) {
    // === BRIGHT TEAL BASE COLOR ===
    // Much brighter to match WLED's visible floor
    CRGB c = CRGB(8, 32, 48); // Visible teal (boosted for ESPHome gamma)

    // Use ORIGINAL cached layer function (intensity handled inside)
    pacifica_one_layer_cached(c, i, 1, sCIStart1, w1_scale, w1_bri, w1_off);
    pacifica_one_layer_cached(c, i, 2, sCIStart2, w2_scale, w2_bri, w2_off);
    pacifica_one_layer_cached(c, i, 1, sCIStart3, w3_scale, w3_bri, w3_off);

    // Add whitecaps
    pacifica_add_whitecaps(c, wave, basethreshold);

    // Deepen colors with TEAL preservation
    pacifica_deepen_colors_teal(c);

    // Brightness boost
    c.r = qadd8(c.r, c.r);
    c.g = qadd8(c.g, c.g);
    c.b = qadd8(c.b, c.b);

    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, 0));
    wave += 7;
  }

  instance->now = nowOld;
  return FRAMETIME;
}

// =============================================================================
// PacificaNative - 42FPS NATIVE TIMING PROOF OF CONCEPT
// =============================================================================
// Hypothesis: Running at native 24ms (42FPS) allows 1:1 WLED math without
// scaling
//
// Key changes from mode_pacifica:
// 1. Internal 24ms throttle via timestamp comparison (not external
// update_interval)
// 2. Real deltams from actual elapsed time (NOT virtual speed accumulator)
// 3. Speed slider controls deltams scaling like original WLED
// 4. NO speed hacks: no speed >> 2, no * 0.6, no virtual time accumulator
//
// Diagnostic: Logs real FPS of THIS effect (should be ~42FPS if throttle works)
// =============================================================================

// Static state for native throttle (per-instance would need member vars)
static uint32_t pacifica_native_last_render = 0;
static uint32_t pacifica_native_last_log = 0;
static uint32_t pacifica_native_frame_count = 0;
static uint32_t pacifica_native_virtual_time = 0;
static uint32_t pacifica_native_speed_accum =
    0; // Fractional accumulator for low speeds

uint16_t mode_pacifica_native() {
  if (!instance)
    return 350;

  // === ALWAYS RENDER - USE REAL ELAPSED TIME ===
  // Use actual elapsed time as delta (like WLED does with strip.now and
  // deltams) This ensures virtual time advances at 1000ms/sec regardless of
  // frame rate
  uint32_t now_ms = cfx_millis();

  // Calculate real elapsed time since last frame
  uint32_t real_deltams = now_ms - pacifica_native_last_render;
  if (real_deltams > 100)
    real_deltams = 100; // Cap to avoid jumps on first frame
  pacifica_native_last_render = now_ms;
  pacifica_native_frame_count++;

  // === DIAGNOSTIC: Log real FPS ===
  if (now_ms - pacifica_native_last_log >= 1000) {
    ESP_LOGD("pacifica_native", "Real FPS: %u | real_deltams: ~%u ms",
             pacifica_native_frame_count, real_deltams);
    pacifica_native_frame_count = 0;
    pacifica_native_last_log = now_ms;
  }

  // Initialize palette caches on first call
  pacifica_init_caches();

  int len = instance->_segment.length();

  // Get persistent state from segment
  unsigned sCIStart1 = instance->_segment.aux0;
  unsigned sCIStart2 = instance->_segment.aux1;
  unsigned sCIStart3 = instance->_segment.step & 0xFFFF;
  unsigned sCIStart4 = (instance->_segment.step >> 16);

  // === FRACTIONAL SPEED ACCUMULATOR (exactly like original Pacifica) ===
  // Original just accumulates speed each frame, no deltams multiplication
  // At speed=128, 56FPS: 7168/sec >> 8 = ~28ms/sec of virtual time
  uint8_t speed = instance->_segment.speed;

  uint32_t deltams = 0;
  if (speed > 0) {
    // EXACTLY like original: just add speed per frame
    pacifica_native_speed_accum += speed;
    deltams = pacifica_native_speed_accum >> 8; // Integer ms
    pacifica_native_speed_accum &= 0xFF;        // Keep fractional part
  }

  // === VIRTUAL TIME FOR BEAT FUNCTIONS ===
  // Advances by speed-scaled real delta (0 when speed=0)
  pacifica_native_virtual_time += deltams;
  uint32_t t = pacifica_native_virtual_time;

  // Speedfactors from original WLED
  unsigned speedfactor1 = beatsin16_t(3, 179, 269, t);
  unsigned speedfactor2 = beatsin16_t(4, 179, 269, t);
  uint32_t deltams1 = (deltams * speedfactor1) >> 8;
  uint32_t deltams2 = (deltams * speedfactor2) >> 8;
  uint32_t deltams21 = (deltams1 + deltams2) >> 1;

  // Update wave positions - ONLY if deltams > 0
  if (deltams > 0) {
    sCIStart1 += (deltams1 * beatsin88_t(1011, 10, 13, t));
    sCIStart2 -= (deltams21 * beatsin88_t(777, 8, 11, t));
    sCIStart3 -= (deltams1 * beatsin88_t(501, 5, 7, t));
    sCIStart4 -= (deltams2 * beatsin88_t(257, 4, 6, t));
  }

  // Save state back to segment
  instance->_segment.aux0 = sCIStart1;
  instance->_segment.aux1 = sCIStart2;
  instance->_segment.step = (sCIStart4 << 16) | (sCIStart3 & 0xFFFF);

  // Whitecap threshold and wave - ORIGINAL WLED values
  unsigned basethreshold = beatsin8_t(9, 55, 65, t);
  unsigned wave = beat8(7, t);

  // Wave layer parameters - ORIGINAL WLED values
  uint16_t w1_scale = beatsin16_t(3, 11 * 256, 14 * 256, t);
  uint8_t w1_bri = beatsin8_t(10, 70, 130, t);
  uint16_t w1_off = 0 - beat16(301, t);

  uint16_t w2_scale = beatsin16_t(4, 6 * 256, 9 * 256, t);
  uint8_t w2_bri = beatsin8_t(17, 40, 80, t);
  uint16_t w2_off = beat16(401, t);

  uint16_t w3_scale = beatsin16_t(5, 8 * 256, 12 * 256, t);
  uint8_t w3_bri = beatsin8_t(13, 50, 100, t);
  uint16_t w3_off = beat16(503, t);

  // Render loop
  for (int i = 0; i < len; i++) {
    // Base color - visible teal
    CRGB c = CRGB(8, 32, 48);

    // Add wave layers using cached palette lookup
    pacifica_one_layer_cached(c, i, 1, sCIStart1, w1_scale, w1_bri, w1_off);
    pacifica_one_layer_cached(c, i, 2, sCIStart2, w2_scale, w2_bri, w2_off);
    pacifica_one_layer_cached(c, i, 1, sCIStart3, w3_scale, w3_bri, w3_off);

    // Add whitecaps
    pacifica_add_whitecaps(c, wave, basethreshold);

    // Deepen colors with teal preservation
    pacifica_deepen_colors_teal(c);

    // Brightness boost
    c.r = qadd8(c.r, c.r);
    c.g = qadd8(c.g, c.g);
    c.b = qadd8(c.b, c.b);

    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, 0));
    wave += 7;
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
    uint8_t gammaBri = dim8_video(rawBri);

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
    uint8_t brightness = (brightness16 < 8) ? 8 : (uint8_t)brightness16;

    // Get color from palette with gamma-corrected brightness
    CRGBW c = ColorFromPalette(smoothIndex, brightness, active_palette);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
  }

  instance->_segment.call++;
  return FRAMETIME;
}

// --- Pride 2015 Effect (ID 63) ---
// Ported from WLED FX.cpp mode_colorwaves_pride_base(true)
// Author: Mark Kriegsman
// Reverse = flip output (pixel 0 ↔ pixel N-1)
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

  // WLED beat calculations
  uint8_t sat8 = beatsin88_t(87, 220, 250);
  uint16_t brightdepth = beatsin88_t(341, 96, 224);
  uint16_t brightnessthetainc16 = beatsin88_t(203, (25 * 256), (40 * 256));
  uint16_t msmultiplier = beatsin88_t(147, 23, 60);
  uint16_t hueinc16 = beatsin88_t(113, 1, 3000);

  uint16_t hue16 = sHue16;

  // Advance persistent state
  sPseudotime += duration * msmultiplier;
  sHue16 += duration * beatsin88_t(400, 5, 9);

  uint32_t brightnesstheta16 = sPseudotime;

  // Get active palette
  const uint32_t *active_palette;
  if (instance->_segment.palette == 0) {
    active_palette = PaletteRainbow;
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  // Process all pixels
  for (int i = 0; i < len; i++) {
    // Accumulate hue
    hue16 += hueinc16;
    uint8_t hue8 = hue16 >> 8;

    // Brightness accumulation
    brightnesstheta16 += brightnessthetainc16;
    int16_t sin_val = sin16_t(brightnesstheta16 & 0xFFFF);
    uint16_t b16 = (uint16_t)(sin_val + 32768);

    // Square for contrast
    uint32_t bri16 = ((uint32_t)b16 * (uint32_t)b16) / 65536;

    // Apply brightness depth
    uint8_t bri8 = (uint32_t)(bri16 * brightdepth) / 65536;
    bri8 += (255 - brightdepth);

    // Get color
    CRGBW c = ColorFromPalette(hue8, bri8, active_palette);

    // Blend with existing (setPixelColor handles reverse internally)
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

  // Time-based counter, speed affects rate (no scaling for matching WLED speed)
  uint8_t speed = instance->_segment.speed;
  uint32_t counter = (instance->now * ((speed >> 3) + 10)) & 0xFFFF;
  counter = (counter >> 2) + (counter >> 4); // 0-16384 + 0-2048

  unsigned var = 0;
  if (counter < 16384) {
    if (counter > 8192)
      counter = 8192 - (counter - 8192);
    var = sin16_t(counter) / 103; // Close to parabolic, max ~224
  }

  // lum = 30 + var (30 minimum = ~12% floor, max ~254)
  // WLED uses this as blend amount, not brightness multiplier
  uint8_t lum = 30 + var;

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

  // Allocate shadow buffer if needed (using segment.data)
  if (instance->_segment.data == nullptr ||
      instance->_segment._dataLen < shadow_size) {
    // Free old data if any
    if (instance->_segment.data != nullptr) {
      free(instance->_segment.data);
    }
    instance->_segment.data = (uint8_t *)malloc(shadow_size);
    if (instance->_segment.data == nullptr) {
      return mode_static(); // Allocation failed
    }
    instance->_segment._dataLen = shadow_size;
    // Clear shadow buffer
    memset(instance->_segment.data, 0, shadow_size);
    // Reset state
    instance->_segment.aux0 = 0;
    instance->_segment.aux1 = 0;
    instance->_segment.step = instance->now;
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

  // === DISSOLVE-SPECIFIC SLIDER SCALING ===
  // Map HA slider defaults (128) to WLED defaults (50 speed, 0 intensity)
  // Speed:    slider 128 → internal 50 (scale factor 50/128 ≈ 0.39)
  // Intensity: slider 128 → internal 0 (offset down by 128)
  uint8_t raw_speed = instance->_segment.speed;
  uint8_t raw_intensity = instance->_segment.intensity;

  uint8_t eff_speed = (uint16_t)raw_speed * 50 / 128;
  if (eff_speed > 255)
    eff_speed = 255;

  int16_t eff_intensity = (int16_t)raw_intensity - 128;
  if (eff_intensity < 0)
    eff_intensity = 0;
  if (eff_intensity > 255)
    eff_intensity = 255;

  // === Softer Intensity Scaling ===
  // Map intensity to 1-8 pixels per frame
  uint8_t pixels_per_frame = 1 + ((uint8_t)eff_intensity >> 5);

  // === Longer Hold Times ===
  // Speed 255 = 500ms (fast), Speed 0 = 3050ms (slow)
  uint32_t hold_ms = 500 + ((255 - eff_speed) * 10);

  // Fill threshold: 100% of strip (ensures full fill)
  uint16_t fill_threshold = len;

  // Timeout safety: 15 seconds max for fill phase
  uint32_t fill_timeout = 15000;

  // === STATE MACHINE (manipulates shadow only) ===
  switch (state) {
  case 0: { // FILLING - set random OFF pixels to ON
    for (int n = 0; n < pixels_per_frame && pixel_count < fill_threshold; n++) {
      // === FIX 1: Linear Fallback for Dead Pixels ===
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

  case 2: { // DISSOLVING - set random ON pixels to OFF
    for (int n = 0; n < pixels_per_frame && pixel_count > 0; n++) {
      // === FIX 1: Linear Fallback for Dead Pixels ===
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

  // Counter for animation (scale speed by 70%)
  uint32_t counter = 0;
  uint8_t speed = instance->_segment.speed * 7 / 10;
  if (speed != 0) {
    counter = instance->now * ((speed >> 2) + 1);
    counter = counter >> 8;
  }

  // Calculate zones based on intensity
  int maxZones = len / 6; // Each zone needs at least 6 LEDs
  if (maxZones < 2)
    maxZones = 2;
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
        uint8_t ramp = 255 - (delta * 42);
        if (ramp > 255)
          ramp = 0;
        uint8_t bri = cubicwave8(ramp);

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

  // Get palette
  const uint32_t *active_palette;
  bool use_solid_color = false;

  if (instance->_segment.palette == 0) {
    // User requested "Fire" as default. ID 4 is Fire.
    active_palette = getPaletteByIndex(4);
  } else if (instance->_segment.palette == 255) {
    use_solid_color = true;
    active_palette = nullptr;
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  // Meteor position (using step as accumulator)
  // Precise speed scaling: 128 ESPHome -> 83 WLED (~0.65x)
  uint8_t user_speed = instance->_segment.speed;
  uint8_t speed = (user_speed <= 128) ? (user_speed * 83 / 128)
                                      : (83 + ((user_speed - 128) * 172 / 127));

  uint32_t counter = instance->now * ((speed >> 2) + 8);
  int meteorPos = (counter * len) >> 16;
  meteorPos = meteorPos % len;

  // Meteor size (5% of strip)
  int meteorSize = 1 + len / 20;

  // --- 1. DECAY LOOP (Before Drawing Head) ---
  // Fix: Use subtractive decay instead of multiplicative fade.
  // Map intensity (0-255) to decay amount (e.g. 20-2).
  // High intensity = Low decay amount = Long trail.
  // Low intensity = High decay amount = Short trail.
  // Mapping: 255 -> 2, 0 -> 20.
  uint8_t decay = 20 - (instance->_segment.intensity * 18 / 255);
  // Safety clamp? 20 - 18 = 2. 20 - 0 = 20.

  for (int i = 0; i < len; i++) {
    // Only decay if not part of the *current* head?
    // Actually, WLED decays everything, then overwrites the head.
    // This allows the tail to form behind the moving head.

    // Check if we should random decay this pixel
    // To mimic WLED's randomness:
    // "subtractFromRGB(random(decay))"
    uint8_t d = hw_random8(decay);

    uint32_t c = instance->_segment.getPixelColor(i);
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;

    // Subtractive decay
    r = (r > d) ? (r - d) : 0;
    g = (g > d) ? (g - d) : 0;
    b = (b > d) ? (b - d) : 0;

    instance->_segment.setPixelColor(i, RGBW32(r, g, b, 0));
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
// Perlin noise mapped to palette. By Andrew Tuline
// Simplified - uses single palette with slow organic movement
uint16_t mode_noisepal(void) {
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

  // Scale based on intensity (zoom level) - higher = more waves visible
  uint8_t scale = 15 + (instance->_segment.intensity >> 2); // 15-78

  // Use instance->now for smooth animation, scaled by speed
  // noiseY changes every frame for visible movement
  uint32_t baseTime = instance->now;
  uint8_t speedFactor = 1 + (instance->_segment.speed >> 4); // 1-16
  uint16_t noiseY = ((baseTime * speedFactor) >> 8) & 0xFFFF;

  for (int i = 0; i < len; i++) {
    // Multi-octave noise approximation using staggered sine waves
    uint16_t noiseX = i * scale;
    // Three waves at different frequencies for organic look
    // Shift by less for smoother gradients
    uint8_t wave1 = sin8((uint8_t)((noiseX + noiseY) >> 4));
    uint8_t wave2 = sin8((uint8_t)((noiseX + noiseY * 2) >> 5) + 85);
    uint8_t wave3 = sin8((uint8_t)((noiseX * 2 + noiseY) >> 5) + 170);
    // Average the waves for smooth noise-like pattern
    uint8_t noiseVal = (wave1 + wave2 + wave3) / 3;

    CRGBW c = ColorFromPalette(noiseVal, 255, active_palette);
    instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, c.w));
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

// --- Rainbow/Colorloop Effects (ID 8, 9) ---
// Ported from WLED FX.cpp

// ID 8: Colorloop - Entire strip cycles through one color
// Intensity controls saturation (blends with white)
uint16_t mode_rainbow(void) {
  if (!instance)
    return 350;

  // Speed scaling: 128 ESPHome → 83 WLED internal (60fps → 42fps adjustment)
  uint8_t user_speed = instance->_segment.speed;
  uint8_t wled_speed = (user_speed <= 128)
                           ? (user_speed * 83 / 128)
                           : (83 + ((user_speed - 128) * 172 / 127));

  // Speed controls cycling rate (WLED formula with scaled speed)
  uint32_t counter = (instance->now * ((wled_speed >> 2) + 2)) & 0xFFFF;
  counter = counter >> 8;

  // Get color from palette (Rainbow as default)
  const uint32_t *active_palette =
      (instance->_segment.palette == 0)
          ? getPaletteByIndex(3) // Rainbow palette
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
  // int len = instance->_segment.length();

  // Speed scaling: 128 ESPHome → 83 WLED internal (60fps → 42fps adjustment)
  uint8_t user_speed = instance->_segment.speed;
  uint8_t wled_speed = (user_speed <= 128)
                           ? (user_speed * 83 / 128)
                           : (83 + ((user_speed - 128) * 172 / 127));

  // Speed controls animation flow (WLED formula with scaled speed)
  uint32_t counter = (instance->now * ((wled_speed >> 2) + 2)) & 0xFFFF;
  counter = counter >> 8;

  // Intensity controls spatial density (exponential: 16 << (intensity/29))
  // intensity/29: 0->1x, 29->2x, 58->4x, 87->8x, 116->16x, etc.
  uint16_t spatial_mult = 16 << (instance->_segment.intensity / 29);

  // Get palette (Rainbow as default)
  const uint32_t *active_palette =
      (instance->_segment.palette == 0)
          ? getPaletteByIndex(3) // Rainbow palette
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

  // Speed controls fade rate - slightly faster minimum for visible voids
  // At speed=0: subtract 3 per frame (~85 frames = ~1.4s to fade)
  // At speed=128: subtract 7 per frame (~36 frames = ~600ms)
  // At speed=255: subtract 11 per frame (~23 frames = ~380ms)
  uint8_t speed = instance->_segment.speed;
  uint8_t fadeAmount = 3 + (speed >> 5); // 3-11 range

  // Get palette - use Rainbow (index 3) as default when palette=0
  const uint32_t *active_palette =
      (instance->_segment.palette == 0)
          ? getPaletteByIndex(3) // Rainbow palette default
          : getPaletteByIndex(instance->_segment.palette);

  // Step 1: Fade ALL pixels toward black using subtraction
  for (int i = 0; i < len; i++) {
    uint32_t cur32 = instance->_segment.getPixelColor(i);
    uint8_t r = (cur32 >> 16) & 0xFF;
    uint8_t g = (cur32 >> 8) & 0xFF;
    uint8_t b = cur32 & 0xFF;

    // Subtraction fade - guarantees reaching 0
    r = (r > fadeAmount) ? r - fadeAmount : 0;
    g = (g > fadeAmount) ? g - fadeAmount : 0;
    b = (b > fadeAmount) ? b - fadeAmount : 0;

    instance->_segment.setPixelColor(i, RGBW32(r, g, b, 0));
  }

  // Step 2: Spawn new twinkles
  int spawnLoops = (len / 50) + 1;
  uint8_t intensity = instance->_segment.intensity;

  for (int j = 0; j < spawnLoops; j++) {
    // Reduced spawn rate for calmer twinkle: intensity/2
    if (hw_random8() <= (intensity >> 1)) {
      int i = hw_random16(0, len);
      // Spawn at full brightness (temporarily allowing on any pixel to test)
      CRGBW c = ColorFromPalette(hw_random8(), 255, active_palette);
      instance->_segment.setPixelColor(i, RGBW32(c.r, c.g, c.b, 0));
    }
  }

  return FRAMETIME;
}

// --- Scanner Effect (ID 40) ---
// Also known as "Cylon" or "KITT" - smooth scanning eye with fading tail
// Speed = Scan frequency, Intensity = Tail length (fade rate)
// dualMode = if true, paint a second eye on opposite side
// --- Scanner Effect (ID 40) ---
// Also known as "Cylon" or "KITT" - linear scanning eye with fading tail
// Speed = Scan frequency, Intensity = Tail length (fade rate)
// dualMode = if true, paint a second eye on opposite side
// --- Scanner Effect (ID 40) ---
// Also known as "Cylon" or "KITT" - linear scanning eye with fading tail
// Speed = Scan frequency (BPM 4-30), Intensity = Tail length (fade rate 30-2)
// dualMode = if true, paint a second eye on opposite side
uint16_t mode_scanner_internal(bool dualMode) {
  if (!instance)
    return 350;

  uint16_t len = instance->_segment.length();
  if (len < 1)
    return 350;

  // 1. Reset check
  if (instance->_segment.reset) {
    instance->_segment.fill(0);
    instance->_segment.reset = false;
  }

  // 2. FADE (The "Tail")
  // User Feedback: "Intensity broken" (Range too small) & "Stuck pixels".
  // Solution: ACCUMULATOR SUBTRACTIVE DECAY (Meteor Style).
  // Allows fractional decay (e.g. 0.8 units per frame) for very long tails.

  // Load state
  uint32_t decay_accum = instance->_segment.aux1;

  // Rate Mapping:
  // We want a range from ~8s (Int 255) to ~0.7s (Int 0).
  // Int 255 -> Rate 200 (~0.78 units/frame).
  // Int 128 -> Rate ~1200 (~4.7 units/frame).
  // Int 0   -> Rate 2200 (~8.6 units/frame).
  // Formula: 2200 - (intensity * 7.8) approx (intensity * 8).
  int rate_calc = 2200 - (instance->_segment.intensity * 8);
  if (rate_calc < 200)
    rate_calc = 200; // Floor at 200 (max tail length)
  uint32_t rate = (uint32_t)rate_calc;

  decay_accum += rate;
  uint8_t sub = decay_accum >> 8; // Integer part to subtract
  decay_accum &= 0xFF;            // Keep fractional part

  // Save state
  instance->_segment.aux1 = decay_accum;

  for (int i = 0; i < len; i++) {
    uint32_t c = instance->_segment.getPixelColor(i);
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;

    // Subtractive Decay using qsub8 (saturating subtraction to 0)
    r = qsub8(r, sub);
    g = qsub8(g, sub);
    b = qsub8(b, sub);

    instance->_segment.setPixelColor(i, RGBW32(r, g, b, 0));
  }

  // 3. MOVEMENT (The "Head")
  uint32_t step = instance->_segment.step; // Load state

  // Speed Scaling:
  // User feedback: "Too fast". Scaling down significantly.
  // Old: speed + 2. New: (speed / 2) + 1.
  // Range: Speed 0 -> +1/frame. Speed 255 -> +128/frame.
  // This matches WLED's typical slower scan feel.
  uint16_t increment = (instance->_segment.speed >> 1) + 1;

  step += increment;
  instance->_segment.step = step; // Save state

  // Triangle Wave logic
  uint16_t pos16 = step & 0xFFFF;
  if (pos16 > 32767) {
    pos16 = 65535 - pos16;
    pos16 = pos16 << 1;
  } else {
    pos16 = pos16 << 1;
  }

  // Map to strip position
  uint16_t pos = (pos16 * (len - 1)) >> 16;

  // 4. ANTI-GAP / LINE DRAWING
  int last_pos = instance->_segment.aux0;
  // Reset if jump is huge (loop end or first run)
  if (last_pos >= len || abs((int)pos - last_pos) > (len / 2)) {
    last_pos = pos;
  }
  int start = (last_pos < pos) ? last_pos : pos;
  int end = (last_pos > pos) ? last_pos : pos;

  // 5. DRAW HEAD
  const uint32_t *active_palette;
  bool use_red_default = (instance->_segment.palette == 0);

  if (use_red_default) {
    active_palette = PaletteRainbow;
  } else {
    active_palette = getPaletteByIndex(instance->_segment.palette);
  }

  for (int i = start; i <= end; i++) {
    uint32_t final_color;

    if (use_red_default) {
      // KITT Default: Solid Red
      final_color = RGBW32(255, 0, 0, 0);
    } else {
      // Dynamic Palette Color + Boost
      uint8_t index = (i * 255) / len;
      CRGBW c = ColorFromPalette(index, 255, active_palette);
      // Boost white/brightness to ensure visibility
      c.r = qadd8(c.r, 80);
      c.g = qadd8(c.g, 80);
      c.b = qadd8(c.b, 80);
      final_color = RGBW32(c.r, c.g, c.b, c.w);
    }

    instance->_segment.setPixelColor(i, final_color);
    if (dualMode) {
      instance->_segment.setPixelColor(len - 1 - i, final_color);
    }
  }

  instance->_segment.aux0 = pos;
  return FRAMETIME;
}

// Wrapper for single scanner (ID 40)
uint16_t mode_scanner(void) { return mode_scanner_internal(false); }

// Dual Scanner (ID 60)
// Two scanners moving in opposite directions
uint16_t mode_scanner_dual(void) { return mode_scanner_internal(true); }

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

  now = cfx_millis();

  // Calculate frame time (delta) - using member variables, not static
  frame_time = now - _last_frame;
  _last_frame = now;

  // Increment call counter for effect initialization logic
  _segment.call++;

  // === DIAGNOSTIC: FPS + Frame Time + Heap Monitor (logs every 1 second) ===
  _diag_frame_count++;

  // Track frame time min/max/sum for Phase 1 verification
  if (frame_time > 0 && frame_time < 10000) { // Sanity check
    if (frame_time < _diag_frame_min)
      _diag_frame_min = frame_time;
    if (frame_time > _diag_frame_max)
      _diag_frame_max = frame_time;
    _diag_frame_sum += frame_time;
  }

  if (now - _diag_last_time >= 1000) {
    uint32_t fps = _diag_frame_count;
    uint32_t frame_mean =
        _diag_frame_count > 0 ? _diag_frame_sum / _diag_frame_count : 0;
    uint32_t heap_internal = esp_get_free_internal_heap_size();
    uint32_t heap_total = esp_get_free_heap_size();
    uint32_t max_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    // Phase 1 Verification: Log frame time stats (target: 24ms ± 2ms)
    ESP_LOGD("wled_perf",
             "FPS:%u Frame_ms(min/mean/max):%u/%u/%u Heap:%u/%u MaxBlk:%u", fps,
             _diag_frame_min, frame_mean, _diag_frame_max, heap_internal,
             heap_total, max_block);

    // Reset counters
    _diag_frame_count = 0;
    _diag_frame_min = UINT32_MAX;
    _diag_frame_max = 0;
    _diag_frame_sum = 0;
    _diag_last_time = now;
  }

  // === Phase 2 Verification: One-time math benchmark ===
  if (!_diag_bench_done) {
    _diag_bench_done = true;
    uint32_t bench_start = cfx_millis();
    volatile uint32_t dummy = 0; // Prevent optimization
    for (int i = 0; i < 10000; i++) {
      dummy += sin8(i & 0xFF); // 10k sin8 calls
    }
    uint32_t bench_time = cfx_millis() - bench_start;
    ESP_LOGD("wled_bench",
             "Math benchmark: 10k sin8 calls in %ums (target: <1ms)",
             bench_time);
    (void)dummy; // Suppress unused warning
  }
  // === END DIAGNOSTIC ===

  // --- INTRO LOGIC ---
  if (_state == STATE_INTRO) {
    if (serviceIntro()) {
      _state = STATE_RUNNING;
      // Intro just finished.
      // We let the next loop iteration handle the main effect start to ensure
      // clean state.
    }
    return;
  }

  // Dispatch via Switch for reliability
  switch (_mode) {
  case FX_MODE_RAINBOW: // 8
    mode_rainbow();
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
  case FX_MODE_COLORTWINKLE: // 74
    mode_colortwinkle();
    break;
  case FX_MODE_PLASMA: // 97
    mode_plasma();
    break;
  case FX_MODE_PACIFICA: // 101
    mode_pacifica();
    break;
  case FX_MODE_PACIFICA_NATIVE: // 200 - Native 42FPS timing PoC
    mode_pacifica_native();
    break;
  case FX_MODE_AURORA_NATIVE: // 201 - Native timing PoC for Aurora
    mode_aurora_native();
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
  case FX_MODE_BLINK: // 1
    mode_blink();
    break;
  case FX_MODE_SUNRISE: // 104
    mode_sunrise();
    break;
  case FX_MODE_BOUNCINGBALLS: // 91
    mode_bouncing_balls();
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

  instance->_segment.fadeToBlackBy(60);

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

    // Fade Width based on Intensity (0 = Sharp, 255 = ~2 Pixels / 65536 steps)
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
  // ESP_LOGD("wled_intro", "Starting Intro Mode %d, Dur %.1fs, Color 0x%08X",
  // mode, duration_s, color);

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
    uint8_t r = (R(_intro_color) * brightness) >> 8;
    uint8_t g = (G(_intro_color) * brightness) >> 8;
    uint8_t b = (B(_intro_color) * brightness) >> 8;
    uint8_t w = (W(_intro_color) * brightness) >> 8;

    _segment.fill(RGBW32(r, g, b, w));
  } else if (_intro_mode == INTRO_GLITTER) {
    // Glitter: Accumulate random pixels
    if ((rand() % 100) < 30) {
      uint16_t pos = rand() % len;
      _segment.setPixelColor(pos, _intro_color);
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

  return color_blend(c1, c2, blendAmt);
}
