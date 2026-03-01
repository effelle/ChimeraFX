/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * CFXLight - Async DMA LED Strip Driver for ESPHome
 * Replaces esp32_rmt_led_strip with fire-and-forget DMA output.
 */

#pragma once

#ifdef USE_ESP32

#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/light_output.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <esp_err.h>
#include <esp_idf_version.h>
#include <functional>
#include <string>
#include <vector>

namespace esphome {

namespace cfx_light {

using OutroCallback = std::function<bool()>;

// --- Segment Infrastructure (Phase 1) ---

#define MAX_CFX_SEGMENTS 6

struct CFXSegmentDef {
  std::string id;
  uint16_t start;
  uint16_t stop;
  bool mirror;
  uint8_t intro_mode;      // 0 = inherit root default
  uint8_t outro_mode;      // 0 = inherit root default
  float intro_duration_s;  // 0.0 = inherit root default
  float outro_duration_s;  // 0.0 = inherit root default
};

// Supported LED chipsets
enum ChimeraChipset : uint8_t {
  CHIPSET_WS2812X, // WS2812B, WS2812C, WS2813 (compatible timings)
  CHIPSET_SK6812,
  CHIPSET_WS2811,
};

// RGB byte order in the protocol
enum RGBOrder : uint8_t {
  ORDER_RGB,
  ORDER_RBG,
  ORDER_GRB,
  ORDER_GBR,
  ORDER_BGR,
  ORDER_BRG,
};

// Timing parameters for the RMT encoder
struct LedParams {
  rmt_symbol_word_t bit0;
  rmt_symbol_word_t bit1;
  rmt_symbol_word_t reset;
};

class CFXLightOutput : public light::AddressableLight {
public:
  void setup() override;
  void loop() override;
  void write_state(light::LightState *state) override;
  void send_visualizer_metadata(const std::string &name, const std::string &palette = "");
  float get_setup_priority() const override;
  int32_t size() const override { return this->num_leds_; }

  void set_outro_callback(OutroCallback cb) { this->outro_cb_ = cb; }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    if (this->is_rgbw_ || this->is_wrgb_) {
      traits.set_supported_color_modes(
          {light::ColorMode::RGB_WHITE, light::ColorMode::WHITE});
    } else {
      traits.set_supported_color_modes({light::ColorMode::RGB});
    }
    return traits;
  }

  void clear_effect_data() override {
    for (int i = 0; i < this->size(); i++)
      this->effect_data_[i] = 0;
  }

  void dump_config() override;

  // Config setters (called by __init__.py codegen)
  void set_pin(uint8_t pin) { this->pin_ = pin; }
  void set_num_leds(uint16_t num_leds) { this->num_leds_ = num_leds; }
  void set_chipset(ChimeraChipset chipset) { this->chipset_ = chipset; }
  void set_rgb_order(RGBOrder order) { this->rgb_order_ = order; }
  void set_is_rgbw(bool is_rgbw) { this->is_rgbw_ = is_rgbw; }
  void set_is_wrgb(bool is_wrgb) { this->is_wrgb_ = is_wrgb; }
  void set_rmt_symbols(uint32_t symbols) { this->rmt_symbols_ = symbols; }
  void set_max_refresh_rate(uint32_t interval_us) {
    this->max_refresh_rate_ = interval_us;
  }

  // --- Segment configuration (codegen setters) ---
  void add_segment_def(const std::string &id, uint16_t start, uint16_t stop,
                       bool mirror, uint8_t intro, uint8_t outro,
                       float intro_dur, float outro_dur) {
    if (segment_defs_.size() >= MAX_CFX_SEGMENTS) return;
    segment_defs_.push_back({id, start, stop, mirror, intro, outro, intro_dur, outro_dur});
  }
  const std::vector<CFXSegmentDef> &get_segment_defs() const { return segment_defs_; }
  bool has_segments() const { return !segment_defs_.empty(); }

  // Root-level intro/outro defaults (inherited by segments with value 0)
  void set_default_intro_mode(uint8_t v) { default_intro_mode_ = v; }
  void set_default_outro_mode(uint8_t v) { default_outro_mode_ = v; }
  void set_default_intro_dur(float v) { default_intro_dur_s_ = v; }
  void set_default_outro_dur(float v) { default_outro_dur_s_ = v; }

  // Resolve inheritance: per-segment value if set, otherwise root default
  uint8_t resolve_intro_mode(const CFXSegmentDef &seg) const {
    return seg.intro_mode != 0 ? seg.intro_mode : default_intro_mode_;
  }
  uint8_t resolve_outro_mode(const CFXSegmentDef &seg) const {
    return seg.outro_mode != 0 ? seg.outro_mode : default_outro_mode_;
  }
  float resolve_intro_dur(const CFXSegmentDef &seg) const {
    return seg.intro_duration_s > 0.0f ? seg.intro_duration_s : default_intro_dur_s_;
  }
  float resolve_outro_dur(const CFXSegmentDef &seg) const {
    return seg.outro_duration_s > 0.0f ? seg.outro_duration_s : default_outro_dur_s_;
  }

  // Visualizer setters
  void set_visualizer_ip(const std::string &ip) { this->visualizer_ip_ = ip; }
  void set_visualizer_port(uint16_t port) { this->visualizer_port_ = port; }
  void set_visualizer_enabled(bool enabled) {
    this->visualizer_enabled_ = enabled;
  }

protected:
  enum VisualizerPacketType : uint8_t {
    VISUALIZER_TYPE_PIXELS = 0x00,
    VISUALIZER_TYPE_METADATA = 0x01,
  };

  light::ESPColorView get_view_internal(int32_t index) const override;

  // Buffer size: 3 bytes/pixel (RGB) or 4 bytes/pixel (RGBW/WRGB)
  size_t get_buffer_size_() const {
    return this->num_leds_ * ((this->is_rgbw_ || this->is_wrgb_) ? 4 : 3);
  }

  // Compute timing parameters from chipset
  void configure_timing_();

  // Pixel data buffer (written by effects via ESPColorView)
  uint8_t *buf_{nullptr};

  // Callback used to execute an Outro animation after ESPHome turns the light
  // off
  OutroCallback outro_cb_{nullptr};

  // Per-pixel effect data (used by AddressableLight)
  uint8_t *effect_data_{nullptr};

  // RMT transmission buffer
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  uint8_t *rmt_buf_{nullptr};
#else
  rmt_symbol_word_t *rmt_buf_{nullptr};
#endif

  // RMT hardware handles
  rmt_channel_handle_t channel_{nullptr};
  rmt_encoder_handle_t encoder_{nullptr};

  // LED timing parameters
  LedParams params_;

  // Configuration
  uint8_t pin_{0};
  uint16_t num_leds_{0};
  ChimeraChipset chipset_{CHIPSET_WS2812X};
  RGBOrder rgb_order_{ORDER_GRB};
  bool is_rgbw_{false};
  bool is_wrgb_{false};
  uint32_t rmt_symbols_{0}; // 0 = auto-detect from chip variant

  // Refresh rate limiting
  uint32_t last_refresh_{0};
  optional<uint32_t> max_refresh_rate_{};

  // Visualizer
  int socket_fd_{-1};
  std::string visualizer_ip_{""};
  uint16_t visualizer_port_{7777};
  bool visualizer_enabled_{false};

  // --- Segment definitions (Phase 1) ---
  std::vector<CFXSegmentDef> segment_defs_;
  uint8_t default_intro_mode_{0};
  uint8_t default_outro_mode_{0};
  float default_intro_dur_s_{0.0f};
  float default_outro_dur_s_{0.0f};
};

} // namespace cfx_light
} // namespace esphome

#endif // USE_ESP32
