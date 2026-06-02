#pragma once

#include "FastLED_Stub.h"
#include "cfx_utils.h"
#include "esphome/components/light/addressable_light.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace esphome {
namespace chimera_fx {

#define DEBUGFX_PRINT(x) ESP_LOGD("chimera_fx", x)
#define DEBUGFX_PRINTLN(x) ESP_LOGD("chimera_fx", x)
#define DEBUGFX_PRINTF(x...) ESP_LOGD("chimera_fx", x)

#define MIN_FRAME_DELAY 2

struct CRGBW {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t w;

  CRGBW() = default;
  CRGBW(uint8_t ir, uint8_t ig, uint8_t ib, uint8_t iw = 0)
      : r(ir), g(ig), b(ib), w(iw) {}
  CRGBW(uint32_t c)
      : r((c >> 16) & 0xFF), g((c >> 8) & 0xFF), b(c & 0xFF),
        w((c >> 24) & 0xFF) {}
};

#define RGBW32(r, g, b, w)                                                     \
  (uint32_t((uint8_t(w) << 24) | (uint8_t(r) << 16) | (uint8_t(g) << 8) |      \
            (uint8_t(b))))
#define CFX_R(c) (uint8_t)((c) >> 16)
#define CFX_G(c) (uint8_t)((c) >> 8)
#define CFX_B(c) (uint8_t)(c)
#define CFX_W(c) (uint8_t)((c) >> 24)

#define DEFAULT_BRIGHTNESS 127
#define DEFAULT_MODE 0
#define DEFAULT_SPEED 128
#define DEFAULT_INTENSITY 128
#define DEFAULT_COLOR 0xFFAA00

#define FRAMETIME 17

#define NO_OPTIONS (uint16_t)0x0000
#define REVERSE (uint16_t)0x0002
#define SEGMENT_ON (uint16_t)0x0004
#define MIRROR (uint16_t)0x0008
#define FROZEN (uint16_t)0x0010
#define RESET_REQ (uint16_t)0x0020
#define SELECTED (uint16_t)0x0001

struct ColliderNode {
  float radius;
  float vel;
  uint8_t glue_timer; // New: holds position for N frames
  float offset;       // New: local drift/offset from grid center
};

// Effect Mode IDs
#define FX_MODE_STATIC 0
#define FX_MODE_BLINK 1
#define FX_MODE_BREATH 2
#define FX_MODE_COLOR_WIPE 3
#define FX_MODE_COLOR_WIPE_RANDOM 4
#define FX_MODE_COLOR_SWEEP 6
#define FX_MODE_RAINBOW 8
#define FX_MODE_RAINBOW_CYCLE 9
#define FX_MODE_SCAN 10
#define FX_MODE_RUNNING_LIGHTS 15
#define FX_MODE_SAW 16
#define FX_MODE_DISSOLVE 18
#define FX_MODE_SPARKLE 20
#define FX_MODE_FLASH_SPARKLE 21
#define FX_MODE_HYPER_SPARKLE 22
#define FX_MODE_STROBE 23
#define FX_MODE_STROBE_RAINBOW 24
#define FX_MODE_MULTI_STROBE 25
#define FX_MODE_BLINK_RAINBOW 26
#define FX_MODE_CHASE_COLOR 28
#define FX_MODE_AURORA 38
#define FX_MODE_SCANNER 40
#define FX_MODE_RAIN 43
#define FX_MODE_RUNNING_DUAL 52
#define FX_MODE_FIRE_DUAL 153
#define FX_MODE_CHASE_MULTI 54
#define FX_MODE_SCANNER_DUAL 60
#define FX_MODE_PRIDE_2015 63
#define FX_MODE_JUGGLE 64
#define FX_MODE_FIRE_2012 66
#define FX_MODE_BPM 68
#define FX_MODE_COLORTWINKLE 74
#define FX_MODE_METEOR 76
#define FX_MODE_RIPPLE 79
#define FX_MODE_HEARTBEAT_CENTER 154
#define FX_MODE_GLITTER 87
#define FX_MODE_EXPLODING_FIREWORKS 90
#define FX_MODE_BOUNCINGBALLS 91
#define FX_MODE_POPCORN 95
#define FX_MODE_DRIP 96
#define FX_MODE_PLASMA 97
#define FX_MODE_PERCENT 98
#define FX_MODE_HEARTBEAT 100
#define FX_MODE_OCEAN 101 // Was Pacifica - optimized for long strips
#define FX_MODE_SUNRISE 104
#define FX_MODE_PHASED 105
#define FX_MODE_NOISEPAL 107
#define FX_MODE_FLOW 110
#define FX_MODE_PERCENT_CENTER 152
#define FX_MODE_DROPPING_TIME 151
#define FX_MODE_KALEIDOS 155
#define FX_MODE_FOLLOW_ME 156
#define FX_MODE_FOLLOW_US 157
#define FX_MODE_ENERGY 158
#define FX_MODE_CHAOS_THEORY 159
#define FX_MODE_FLUID_RAIN 160
#define FX_MODE_HORIZON_SWEEP 161
#define FX_MODE_CENTER_SWEEP 162
#define FX_MODE_GLITTER_SWEEP 163
#define FX_MODE_COLLIDER 164
#define FX_MODE_TWIN_PULSE_SWEEP 165
#define FX_MODE_TRANSMISSION 166
#define FX_MODE_FOUR_TIMES_THE_CHARM 167
#define FX_MODE_HYDRO_PULSE 168
#define FX_MODE_DROPPING_FILL 169
#define FX_MODE_ASSEMBLY				170
#define FX_MODE_INERTIA_SWEEP 171
#define FX_MODE_SONAR_REVEAL 172
#define FX_MODE_VENETIAN 173
#define FX_MODE_CRYSTALLIZE 174
#define FX_MODE_DEEP_BREATHE 175
#define FX_MODE_MOIRE_SHIFT 176
#define FX_MODE_RESONANCE_FILL 177
#define FX_MODE_TELEMETRY 178
#define FX_MODE_STELLAR_DUST 179
#define FX_MODE_INTERFERENCE 180
#define FX_MODE_ECLIPSE 181
#define FX_MODE_GAS_DISCHARGE 182
#define FX_MODE_HARMONIC_SETTLE 183
#define FX_MODE_LITHOGRAPH 184
#define FX_MODE_SEPARATOR				185
#define FX_MODE_TIDAL_SURGE				186
#define FX_MODE_IMPACT_FLARE			187
#define FX_MODE_MONOLITH                188

#define INTRO_NONE 0
#define INTRO_WIPE 1
#define INTRO_FADE 2
#define INTRO_CENTER 3
#define INTRO_GLITTER 4
#define INTRO_TWIN_PULSE 5
#define INTRO_MORSE 6
#define INTRO_QUADRANT 7
#define INTRO_HYDRAULICS 8
#define INTRO_DROPPING 9
#define INTRO_DRAINING 10
#define INTRO_INERTIA_SWEEP 11
#define INTRO_SONAR_REVEAL 12
#define INTRO_VENETIAN 13
#define INTRO_CRYSTALLIZE 14
#define INTRO_DEEP_BREATHE 15
#define INTRO_TIDAL_SURGE  25
#define INTRO_IMPACT_FLARE 26
#define OUTRO_CENTER_SQUEEZE 27

// CFX-008: Cover full 0–255 ID range (Ambient Roulette = 255)
#define MODE_COUNT					189

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
  size_t _dataLen; // CFX-003: widened from uint16_t to avoid silent truncation vs allocateData(size_t)
  // audit 4.2: per-segment timestamp for effects that track their own frame
  // cadence (e.g. fire modes). Replaces function-scope static variables that
  // were shared across all runners.
  uint32_t frame_timestamp_ms;

  uint32_t colors[3];

  Segment(uint16_t sStart = 0, uint16_t sStop = 10)
      : start(sStart), stop(sStop), offset(0), speed(DEFAULT_SPEED),
        intensity(DEFAULT_INTENSITY), palette(255), mode(DEFAULT_MODE),
        selected(true), on(true), mirror(false), freeze(false), reset(true),
        step(0), call(0), aux0(0), aux1(0), data(nullptr), _dataLen(0),
        frame_timestamp_ms(0) {
    colors[0] = DEFAULT_COLOR;
    colors[1] = 0x0;
    colors[2] = 0x0;
  }

  // CFX-001 mapping reverted: mirror now correctly reverses the axis as intended.
  uint16_t physicalLength() const { return stop - start; }
  uint16_t virtualLength() const { return physicalLength(); }
  uint16_t length() const { return physicalLength(); }
  bool isActive() const { return on && physicalLength() > 0; }

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
  void subtractive_fade_val(uint8_t fade_amt);
  void fade_out_smooth(uint8_t fade_amt);
  void blur(uint8_t blur_amount);
  uint32_t color_from_palette(uint16_t i, bool mapping, bool wrap, uint8_t mcol,
                              uint8_t pbri = 255);
};

class CFXRunner {
public:
  CFXRunner(esphome::light::AddressableLight *light);

  // Segment identity (set from YAML segment id, empty for single-runner mode)
  std::string segment_id_;
  void set_segment_id(const std::string &id) { segment_id_ = id; }
  const std::string &get_segment_id() const { return segment_id_; }

  int32_t current_leading_pixel{-1};

  // Gamma Correction Helper Support
  // Non-static to allow multiple strips with different gammas to coexist
  const uint8_t *_lut{nullptr};
  uint8_t *_dynamic_lut{nullptr};
  float _gamma;

  // Sequence Iteration Limits
  uint32_t iteration_count_{0};
  uint32_t target_iterations_{0};
  bool effect_complete_{false};
  // Set by set_active_sequence() / cfx_set when sequence or cfx_set params
  // override speed/intensity/palette. Checked by CFXControl push callbacks
  // to prevent UI slider callbacks from stomping sequence-injected values.
  bool sequence_owns_speed_{false};
  bool sequence_owns_intensity_{false};
  bool sequence_owns_palette_{false};
  bool sequence_owns_mirror_{false};

  void setGamma(float g);
  inline uint8_t applyGamma(uint8_t val) { return _lut ? _lut[val] : val; }
  uint8_t shiftFloor(uint8_t val);
  uint8_t getFadeFactor(uint8_t factor);
  uint8_t getSubFactor(uint8_t factor);

  // Destructor: Release segment data to reclaim RAM
  ~CFXRunner() {
    _segment.deallocateData();
    if (_dynamic_lut != nullptr) {
      free(_dynamic_lut);
      _dynamic_lut = nullptr;
    }
  }

  void setDebug(bool state) { diagnostics.enabled = state; }
  bool getDebug() const { return diagnostics.enabled; }
  void setName(const char *name) {
    const char *safe_name = name != nullptr ? name : "";
    if (_name != safe_name)
      _name = safe_name;
  }
  void setPaletteSeedSalt(uint32_t salt) {
    if (palette_seed_salt_ == salt)
      return;
    palette_seed_salt_ = salt;
    if (_segment.palette == 254)
      generateRandomPalette();
  }
  const char *getModeName() const;

  void service();
  void reset();
  void setMode(uint8_t m) {
    if (_mode != m) {
      _mode = m;
      _segment.mode = m;
      _segment.reset = true;
    }
  }

  uint8_t getMode() const { return _mode; }
  uint8_t getPalette() const { return _segment.palette; }

  // CFX-010: Returns animation progress 0–100% for progressive effects.
  // Uses current_leading_pixel (set by sweep/wipe/sunrise/dropping_time effects).
  // Effects that do not advance a leading pixel will still report 0 — that is
  // correct and expected; this hook is primarily useful for sequencer on_reach triggers.
  uint8_t progress_pct() const {
    uint16_t len = _segment.length();
    if (len == 0 || current_leading_pixel < 0)
      return 0;
    uint32_t pct = ((uint32_t)current_leading_pixel * 100u) / len;
    return (pct > 100u) ? 100u : (uint8_t)pct;
  }

  float _virtual_now = 0.0f; // CFX-009: was double — ESP32 Xtensa LX6 only has single-precision HW FPU
  float _accum_ms = 0;

  uint32_t pacifica_speed_accum = 0;
  uint32_t pacifica_virtual_time = 1;

  void setSpeed(uint8_t s) {
    if (_segment.speed != s) {
      _segment.speed = s;
    }
  }
  uint8_t getSpeed() const { return _segment.speed; }

  void setIntensity(uint8_t i) {
    if (_segment.intensity != i) {
      _segment.intensity = i;
    }
  }
  uint8_t getIntensity() const { return _segment.intensity; }
  void setPalette(uint8_t p) {
    if (_segment.palette != p) {
      _segment.palette = p;
      if (p == 254)
        generateRandomPalette();
    }
  }
  void setMirror(bool m) {
    if (_segment.mirror != m) {
      _segment.mirror = m;
      _segment.fill(0); // clear the segment when direction changes
    }
  }
  bool getMirror() const { return _segment.mirror; }
  void setColor(uint32_t c) { _segment.colors[0] = c; }
  void generateRandomPalette();
  void setBakeBrightness(bool bake) { bake_brightness_ = bake; }


  void start() { _state = STATE_RUNNING; }

  esphome::light::AddressableLight *target_light;
  uint32_t now;
  uint16_t frame_time;
  Segment _segment;

  void startIntro(uint8_t mode, float duration_s, uint32_t color);
  bool isIntroRunning() { return _state == STATE_INTRO; }

  const char *_name = "CFX";

  cfx::FrameDiagnostics diagnostics;

  // Smart Random Palette Storage
  CRGBPalette16 _currentRandomPalette;
  uint32_t _currentRandomPaletteBuffer[16];
  uint32_t random_palette_nonce_{0};
  uint32_t palette_seed_salt_{0};

  bool force_white_active_{false};
  float global_brightness_ = 1.0f;
  bool is_return_phase_{false};
  bool bake_brightness_ = false;


private:
  RunnerState _state = STATE_RUNNING;
  uint8_t _intro_mode = INTRO_NONE;
  uint32_t _intro_start_time = 0;
  uint32_t _intro_duration_ms = 0;
  uint32_t _intro_color = 0;

  bool serviceIntro();

  uint8_t _mode;

  // CFX-008 / CFX-004: _mode_ptr[] dispatch table removed — superseded by the
  // switch-case in service(). It was indexed by mode ID and would have caused
  // out-of-bounds reads for IDs 120–167 with the old MODE_COUNT of 120.
  // CFX-004 FIXED: global `instance` pointer race resolved via RAII InstanceGuard
  // in service(). The guard ensures each service() call operates on correct runner
  // context even with multiple strips.

  uint32_t _last_frame = 0;
};

// Per-core instance pointer array.
// index 0 → Core 0 (or the only core on unicore ESP32 variants: C3, S2, H2)
// index 1 → Core 1 (ESPHome main loop on dual-core ESP32/S3)
extern CFXRunner *instance_per_core[2];

// RAII guard — sets the per-core slot on construction, restores the
// previous value on destruction. Thread-safe: Core 0 and Core 1 each
// write their own independent slot, so simultaneous service() calls
// on different cores cannot corrupt each other's pointer.
// NOTE: the `instance` macro is NOT defined here. Putting it in a header
// corrupts every TU that uses `instance` as a variable name (CFXEventManager,
// CFXSequenceSelect, esphome logger, etc.). The macro is defined after the
// #include block in CFXRunner.cpp and cfx_addressable_light_effect.cpp only.
class InstanceGuard {
  uint8_t core_id_;
  CFXRunner *prev_;
public:
  explicit InstanceGuard(CFXRunner *runner) {
    core_id_ = (uint8_t)xPortGetCoreID();
    prev_ = instance_per_core[core_id_];
    instance_per_core[core_id_] = runner;
  }
  ~InstanceGuard() {
    instance_per_core[core_id_] = prev_;
  }

  // Non-copyable
  InstanceGuard(const InstanceGuard &) = delete;
  InstanceGuard &operator=(const InstanceGuard &) = delete;
};

} // namespace chimera_fx
} // namespace esphome
