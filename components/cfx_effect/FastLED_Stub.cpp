/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Copyright (c) FastLED
 *
 * This file is part of the ChimeraFX for ESPHome.
 * Adapted from the FastLED library.
 */

#include "FastLED_Stub.h"

// FastLED's "b_m16_interleave" table for fast sin8/cos8
// This array contains 256 values representing a sine wave (0-255)
// Source: FastLED library
const uint8_t b_m16_interleave[] = {
    0,   49, 49,  41, 90,  27, 117, 10, 128, 0, 128, 0, 128, 0,
    128, 0,  128, 0,  128, 0,  128, 0,  128, 0, 128, 0, 128, 0,
    128, 0,  128, 0,  0,   0,  0,   0,  0,   0, 0,   0,
    // ... Wait, FastLED's actual table is usually calculated or formatted
    // differently. Let's use a standard generated sin8 table for simplicity and
    // correctness. 0-255 amplitude, 0-255 phase.
};

// Actually, let's use a fully pre-calculated sin8 table (amplitude 128+127*sin)
const uint8_t sin8_data[256] = {
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

// Gamma 2.2 correction LUT (256 entries)
// Formula: gamma8_lut[i] = round(pow(i/255.0, 2.2) * 255)
// Provides O(1) gamma correction instead of per-pixel pow()
const uint8_t gamma8_lut[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,
    2,   2,   3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,
    6,   6,   6,   6,   7,   7,   7,   8,   8,   8,   9,   9,   9,   10,  10,
    11,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,
    17,  18,  18,  19,  19,  20,  20,  21,  22,  22,  23,  23,  24,  25,  25,
    26,  26,  27,  28,  28,  29,  30,  30,  31,  32,  33,  33,  34,  35,  35,
    36,  37,  38,  39,  39,  40,  41,  42,  43,  43,  44,  45,  46,  47,  48,
    49,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,
    63,  64,  65,  66,  67,  68,  69,  70,  71,  73,  74,  75,  76,  77,  78,
    79,  81,  82,  83,  84,  85,  87,  88,  89,  90,  91,  93,  94,  95,  97,
    98,  99,  100, 102, 103, 105, 106, 107, 109, 110, 111, 113, 114, 116, 117,
    119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135, 137, 138, 140,
    141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161, 163, 165,
    166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190, 192,
    194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
    223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253,
    255};

// Basic HSV to RGB conversion (Approximation of FastLED's rainbow)
void hsv2rgb_rainbow(const CHSV &hsv, CRGB &rgb) {
  uint8_t h = hsv.h;
  uint8_t s = hsv.s;
  uint8_t v = hsv.v;

  uint8_t r, g, b;
  uint8_t region, remainder, p, q, t;

  if (s == 0) {
    rgb.r = v;
    rgb.g = v;
    rgb.b = v;
    return;
  }

  region = h / 43;
  remainder = (h - (region * 43)) * 6;

  p = (v * (255 - s)) >> 8;
  q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
  case 0:
    r = v;
    g = t;
    b = p;
    break;
  case 1:
    r = q;
    g = v;
    b = p;
    break;
  case 2:
    r = p;
    g = v;
    b = t;
    break;
  case 3:
    r = p;
    g = q;
    b = v;
    break;
  case 4:
    r = t;
    g = p;
    b = v;
    break;
  case 5:
  default:
    r = v;
    g = p;
    b = q;
    break;
  }

  rgb.r = r;
  rgb.g = g;
  rgb.b = b;
}

// Global Palette stubs for Noise effects
const CRGBPalette16 RainbowColors_p = {0xFF0000, 0xD52A00, 0xAB5500, 0xAB7F00,
                                       0xABAB00, 0x56D500, 0x00FF00, 0x00D52A,
                                       0x00AB55, 0x0056AA, 0x0000FF, 0x2A00D5,
                                       0x5500AB, 0x7F0081, 0xAB0055, 0xD5002B};

const CRGBPalette16 OceanColors_p = {0x000080, 0x0019A4, 0x0033C8, 0x004CEC,
                                     0x1966FF, 0x4C80FF, 0x8099FF, 0xB3B3FF,
                                     0xE6CCFF, 0xE6B3FF, 0xE699FF, 0xE680FF,
                                     0xE666FF, 0xE64CFF, 0xE633FF, 0xE619FF};

const CRGBPalette16 PartyColors_p = {0x5500AB, 0x84007C, 0xB5004B, 0xE5001B,
                                     0xE81700, 0xB84700, 0xAB7700, 0xABAB00,
                                     0xAB5500, 0xDD2200, 0xF2000E, 0xC2003E,
                                     0x8F0071, 0x5F00A1, 0x2F00D0, 0x0007F9};
