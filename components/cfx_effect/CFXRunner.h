/*
 * ChimeraFX - WLED Effects for ESPHome
 * Copyright (c) 2026 Federico Leoni
 * Based on WLED by Aircoookie (https://github.com/wled/WLED)
 *
 * Licensed under the EUPL-1.2
 */

#pragma once

#include "FastLED_Stub.h"
#include "esphome/components/light/addressable_light.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdint>
#include <vector>

#define DEBUGFX_PRINT(x) ESP_LOGD("chimera_fx", x)
#define DEBUGFX_PRINTLN(x) ESP_LOGD("chimera_fx", x)
#define DEBUGFX_PRINTF(x...) ESP_LOGD("chimera_fx", x)

#define MIN_FRAME_DELAY 2

#define RGBW32(r, g, b, w)                                                     \
  (uint32_t((uint8_t(w) << 24) | (uint8_t(r) << 16) | (uint8_t(g) << 8) |      \
            (uint8_t(b))))
#define R(c) (uint8_t)((c) >> 16)
#define G(c) (uint8_t)((c) >> 8)
#define B(c) (uint8_t)(c)
#define W(c) (uint8_t)((c) >> 24)

#define DEFAULT_BRIGHTNESS 127
#define DEFAULT_MODE 0
#define DEFAULT_SPEED 128
#define DEFAULT_INTENSITY 128
#define DEFAULT_COLOR 0xFFAA00

#define FRAMETIME 15

#define NO_OPTIONS (uint16_t)0x0000
#define REVERSE (uint16_t)0x0002
#define SEGMENT_ON (uint16_t)0x0004
#define MIRROR (uint16_t)0x0008
#define FROZEN (uint16_t)0x0010
#define RESET_REQ (uint16_t)0x0020
#define SELECTED (uint16_t)0x0001

// Effect Mode IDs
#define FX_MODE_STATIC 0
#define FX_MODE_BLINK 1
#define FX_MODE_BREATH 2
#define FX_MODE_COLOR_WIPE 3
#define FX_MODE_COLOR_WIPE_RANDOM 4
#define FX_MODE_RANDOM_COLOR 5
#define FX_MODE_COLOR_SWEEP 6
#define FX_MODE_DYNAMIC 7
#define FX_MODE_RAINBOW 8
#define FX_MODE_RAINBOW_CYCLE 9
#define FX_MODE_SCAN 10
#define FX_MODE_DUAL_SCAN 11
#define FX_MODE_FADE 12
#define FX_MODE_THEATER_CHASE 13
#define FX_MODE_THEATER_CHASE_RAINBOW 14
#define FX_MODE_RUNNING_LIGHTS 15
#define FX_MODE_SAW 16
#define FX_MODE_TWINKLE 17
#define FX_MODE_DISSOLVE 18
#define FX_MODE_DISSOLVE_RANDOM 19
#define FX_MODE_SPARKLE 20
#define FX_MODE_FLASH_SPARKLE 21
#define FX_MODE_HYPER_SPARKLE 22
#define FX_MODE_STROBE 23
#define FX_MODE_STROBE_RAINBOW 24
#define FX_MODE_MULTI_STROBE 25
#define FX_MODE_BLINK_RAINBOW 26
#define FX_MODE_ANDROID 27
#define FX_MODE_CHASE_COLOR 28
#define FX_MODE_CHASE_RANDOM 29
#define FX_MODE_CHASE_RAINBOW 30
#define FX_MODE_CHASE_FLASH 31
#define FX_MODE_CHASE_FLASH_RANDOM 32
#define FX_MODE_CHASE_RAINBOW_WHITE 33
#define FX_MODE_COLORFUL 34
#define FX_MODE_TRAFFIC_LIGHT 35
#define FX_MODE_COLOR_SWEEP_RANDOM 36
#define FX_MODE_RUNNING_COLOR 37
#define FX_MODE_AURORA 38
#define FX_MODE_RUNNING_RANDOM 39
#define FX_MODE_SCANNER 40
#define FX_MODE_COMET 41
#define FX_MODE_FIREWORKS 42
#define FX_MODE_RAIN 43
#define FX_MODE_TETRIX 44
#define FX_MODE_FIRE_FLICKER 45
#define FX_MODE_GRADIENT 46
#define FX_MODE_LOADING 47
#define FX_MODE_ROLLINGBALLS 48
#define FX_MODE_FAIRY 49
#define FX_MODE_TWO_DOTS 50
#define FX_MODE_FAIRYTWINKLE 51
#define FX_MODE_RUNNING_DUAL 52
#define FX_MODE_TRICOLOR_CHASE 54
#define FX_MODE_TRICOLOR_WIPE 55
#define FX_MODE_TRICOLOR_FADE 56
#define FX_MODE_LIGHTNING 57
#define FX_MODE_ICU 58
#define FX_MODE_MULTI_COMET 59
#define FX_MODE_SCANNER_DUAL 60
#define FX_MODE_RANDOM_CHASE 61
#define FX_MODE_OSCILLATE 62
#define FX_MODE_PRIDE_2015 63
#define FX_MODE_JUGGLE 64
#define FX_MODE_PALETTE 65
#define FX_MODE_FIRE_2012 66
#define FX_MODE_COLORWAVES 67
#define FX_MODE_BPM 68
#define FX_MODE_FILLNOISE8 69
#define FX_MODE_NOISE16_1 70
#define FX_MODE_NOISE16_2 71
#define FX_MODE_NOISE16_3 72
#define FX_MODE_NOISE16_4 73
#define FX_MODE_COLORTWINKLE 74
#define FX_MODE_LAKE 75
#define FX_MODE_METEOR 76
#define FX_MODE_METEOR_SMOOTH 77
#define FX_MODE_RAILWAY 78
#define FX_MODE_RIPPLE 79
#define FX_MODE_TWINKLEFOX 80
#define FX_MODE_TWINKLECAT 81
#define FX_MODE_HALLOWEEN_EYES 82
#define FX_MODE_STATIC_PATTERN 83
#define FX_MODE_TRI_STATIC_PATTERN 84
#define FX_MODE_SPOTS 85
#define FX_MODE_SPOTS_FADE 86
#define FX_MODE_GLITTER 87
#define FX_MODE_CANDLE 88
#define FX_MODE_STARBURST 89
#define FX_MODE_EXPLODING_FIREWORKS 90
#define FX_MODE_BOUNCINGBALLS 91
#define FX_MODE_SINELON 92
#define FX_MODE_SINELON_DUAL 93
#define FX_MODE_SINELON_RAINBOW 94
#define FX_MODE_POPCORN 95
#define FX_MODE_DRIP 96
#define FX_MODE_PLASMA 97
#define FX_MODE_PERCENT 98
#define FX_MODE_RIPPLE_RAINBOW 99
#define FX_MODE_HEARTBEAT 100
#define FX_MODE_PACIFICA 101
#define FX_MODE_PACIFICA_NATIVE 200 // Native 42FPS timing PoC
#define FX_MODE_AURORA_NATIVE 201   // Native timing PoC for Aurora
#define FX_MODE_CANDLE_MULTI 102
#define FX_MODE_SOLID_GLITTER 103
#define FX_MODE_SUNRISE 104
#define FX_MODE_PHASED 105
#define FX_MODE_TWINKLEUP 106
#define FX_MODE_NOISEPAL 107
#define FX_MODE_SINEWAVE 108
#define FX_MODE_PHASEDNOISE 109
#define FX_MODE_FLOW 110
#define FX_MODE_CHUNCHUN 111
#define FX_MODE_DANCING_SHADOWS 112
#define FX_MODE_WASHING_MACHINE 113

#define INTRO_NONE 0
#define INTRO_WIPE 1
#define INTRO_FADE 2
#define INTRO_CENTER 3
#define INTRO_GLITTER 4

#define MODE_COUNT 114

enum RunnerState { STATE_RUNNING = 0, STATE_INTRO = 1 };

class CFXRunner;

class Segment {
public:
  uint16_t start;
  uint16_t stop;
  uint16_t offset;

  uint8_t speed;
  uint8_t intensity;
  uint8_t palette;
  uint8_t mode;

  bool selected : 1;
  bool on : 1;
  bool mirror : 1;
  bool freeze : 1;
  bool reset : 1;

  uint8_t custom1, custom2, custom3;
  bool check1, check2, check3;

  uint32_t step;
  uint32_t call;
  uint16_t aux0;
  uint16_t aux1;
  uint8_t *data;
  uint16_t _dataLen;

  uint32_t colors[3];

  Segment(uint16_t sStart = 0, uint16_t sStop = 10)
      : start(sStart), stop(sStop), offset(0), speed(DEFAULT_SPEED),
        intensity(DEFAULT_INTENSITY), palette(255), mode(DEFAULT_MODE),
        selected(true), on(true), mirror(false), freeze(false), reset(true),
        step(0), call(0), aux0(0), aux1(0), data(nullptr), _dataLen(0) {
    colors[0] = DEFAULT_COLOR;
    colors[1] = 0x0;
    colors[2] = 0x0;
  }

  uint16_t length() const { return stop - start; }
  uint16_t virtualLength() const { return length(); }
  bool isActive() const { return on && length() > 0; }

  bool allocateData(size_t len) {
    if (data && _dataLen == len)
      return true;
    deallocateData();
    data = (uint8_t *)malloc(len);
    if (!data)
      return false;
    _dataLen = len;
    memset(data, 0, len);
    return true;
  }

  void deallocateData() {
    if (data) {
      free(data);
      data = nullptr;
    }
    _dataLen = 0;
  }

  void setPixelColor(int n, uint32_t c);
  uint32_t getPixelColor(int n);
  void fill(uint32_t c);
  void fadeToBlackBy(uint8_t fadeBy);
  uint32_t color_from_palette(uint16_t i, bool mapping, bool wrap, uint8_t mcol,
                              uint8_t pbri = 255);
};

class CFXRunner {
public:
  CFXRunner(esphome::light::AddressableLight *light);

  // Destructor: Release segment data to reclaim RAM
  ~CFXRunner() { _segment.deallocateData(); }

  void service();
  void setMode(uint8_t m) {
    if (_mode != m) {
      _mode = m;
      _segment.mode = m;
      _segment.reset = true;
    }
  }

  uint8_t getMode() const { return _mode; }

  double _virtual_now = 0;
  float _accum_ms = 0;

  uint32_t pacifica_speed_accum = 0;
  uint32_t pacifica_virtual_time = 1;

  void setSpeed(uint8_t s) {
    if (_segment.speed != s) {
      _segment.speed = s;
    }
  }
  void setIntensity(uint8_t i) {
    if (_segment.intensity != i) {
      _segment.intensity = i;
    }
  }
  void setPalette(uint8_t p) {
    if (_segment.palette != p) {
      _segment.palette = p;
    }
  }
  void setMirror(bool m) {
    if (_segment.mirror != m) {
      _segment.mirror = m;
    }
  }
  void setColor(uint32_t c) { _segment.colors[0] = c; }

  void start() { _state = STATE_RUNNING; }

  esphome::light::AddressableLight *target_light;
  uint32_t now;
  uint16_t frame_time;
  Segment _segment;

  void startIntro(uint8_t mode, float duration_s, uint32_t color);
  bool isIntroRunning() { return _state == STATE_INTRO; }

private:
  RunnerState _state = STATE_RUNNING;
  uint8_t _intro_mode = INTRO_NONE;
  uint32_t _intro_start_time = 0;
  uint32_t _intro_duration_ms = 0;
  uint32_t _intro_color = 0;

  bool serviceIntro();

  uint8_t _mode;

  typedef uint16_t (*mode_ptr)(void);
  static mode_ptr _mode_ptr[];

  uint32_t _last_frame = 0;
  uint32_t _diag_frame_count = 0;
  uint32_t _diag_last_time = 0;

  uint32_t _diag_frame_min = UINT32_MAX;
  uint32_t _diag_frame_max = 0;
  uint32_t _diag_frame_sum = 0;

  bool _diag_bench_done = false;
};

extern CFXRunner *instance;
