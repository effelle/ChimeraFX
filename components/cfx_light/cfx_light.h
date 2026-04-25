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
#include "esphome/components/light/light_state.h"
#include "esphome/components/light/light_transformer.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_idf_version.h>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace esphome {

namespace cfx_light {

using OutroCallback = std::function<bool()>;

// --- Segment Infrastructure (Phase 1) ---

#define MAX_CFX_SEGMENTS 4

struct CFXSegmentDef {
  std::string id;
  uint16_t start;
  uint16_t stop;
  bool mirror;
  uint8_t intro_mode;     // 0 = inherit root default
  uint8_t outro_mode;     // 0 = inherit root default
  float intro_duration_s; // 0.0 = inherit root default
  float outro_duration_s; // 0.0 = inherit root default
};

struct CFXTurnOnDefaults {
  bool has_brightness{false};
  float brightness{1.0f};
  bool has_color{false};
  bool color_has_white{false};
  float red{0.0f};
  float green{0.0f};
  float blue{0.0f};
  float white{0.0f};

  bool should_apply(light::LightState *state, bool allow_white) const {
    if (state == nullptr || !state->remote_values.is_on()) {
      return false;
    }

    if (this->has_brightness &&
        std::abs(state->remote_values.get_brightness() - this->brightness) > 0.01f) {
      return true;
    }

    if (!this->has_color) {
      return false;
    }

    if (std::abs(state->remote_values.get_red() - this->red) > 0.01f ||
        std::abs(state->remote_values.get_green() - this->green) > 0.01f ||
        std::abs(state->remote_values.get_blue() - this->blue) > 0.01f) {
      return true;
    }

    if (allow_white && this->color_has_white &&
        std::abs(state->remote_values.get_white() - this->white) > 0.01f) {
      return true;
    }

    return false;
  }

  void apply(light::LightCall &call, bool allow_white) const {
    call.set_transition_length(0);

    if (this->has_brightness) {
      call.set_brightness(this->brightness);
    }

    if (!this->has_color) {
      return;
    }

    call.set_color_mode(
        allow_white && this->color_has_white ? light::ColorMode::RGB_WHITE
                                             : light::ColorMode::RGB);
    call.set_rgb(this->red, this->green, this->blue);
    if (allow_white && this->color_has_white) {
      call.set_white(this->white);
    }
  }
};

// Supported LED chipsets
enum ChimeraChipset : uint8_t {
  CHIPSET_WS2812X, // WS2812B, WS2812C, WS2813 (compatible timings)
  CHIPSET_SK6812,
  CHIPSET_WS2811,
  CHIPSET_APA102,  // SPI two-wire, BGR native
  CHIPSET_SK9822,  // SPI two-wire, BGR native
};

// Transport bus type (inferred from chipset)
enum CFXTransport : uint8_t {
  TRANSPORT_RMT,  // One-wire (WS2812X, SK6812, WS2811)
  TRANSPORT_SPI,  // Two-wire clock+data (APA102, SK9822)
};

// SPI host selection (maps to ESP-IDF spi_host_device_t)
enum CFXSPIHost : uint8_t {
  SPI_HOST_2,  // SPI2_HOST — available on all ESP32 variants
  SPI_HOST_3,  // SPI3_HOST — ESP32/S2/S3/P4 only
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
  CFXLightOutput();
  ~CFXLightOutput(); // CFX-025: closes visualizer socket_fd_ to prevent FD leak
  void setup() override;
  void loop() override;
  void write_state(light::LightState *state) override;
  void update_state(light::LightState *state) override;
  void on_master_update();
  void on_segment_update();
  void send_visualizer_metadata(const std::string &name,
                                const std::string &palette = "");
  float get_setup_priority() const override;
  int32_t size() const override { return this->num_leds_; }
  std::unique_ptr<light::LightTransformer> create_default_transition() override;

  void add_outro_callback(OutroCallback cb) { this->outro_cbs_.push_back(cb); }
  void drain_outro_callbacks();
  bool has_outro() const { return !this->outro_cbs_.empty(); }
  size_t get_outro_callback_count() const { return this->outro_cbs_.size(); }
  static const std::vector<CFXLightOutput *> &get_instances() { return instances_; }

  // Called by CFXVirtualSegmentLight::write_state() to request a DMA flush
  // that bypasses the Master LightState's rendering pipeline.
  // Counter-based: fires write_state(nullptr) only when ALL N segments have
  // reported their render complete, preventing premature DMA on partial frames.
  void request_segment_flush();

  // Segment flush coalescing state (Fix-2: one DMA call per frame)
  bool     seg_flush_pending_{false};
  uint32_t seg_flush_first_ms_{0};

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    if (this->has_segments()) {
      // Segmented mode: main switch is dim-only (like WLED).
      // Individual segment entities expose their own RGB controls.
      traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
    } else {
      // Non-segmented mode: full color control on the main switch.
      if (this->has_white_channel()) {
        traits.set_supported_color_modes(
            {light::ColorMode::RGB_WHITE, light::ColorMode::WHITE});
      } else {
        traits.set_supported_color_modes({light::ColorMode::RGB});
      }
    }
    return traits;
  }

  void clear_effect_data() override {
    for (int i = 0; i < this->size(); i++)
      this->effect_data_[i] = 0;
  }

  void dump_config() override;

  // Public accessor for effect_data_ (used by virtual segment lights)
  uint8_t *get_effect_data() { return effect_data_; }

  // Config setters (called by light.py codegen)
  void set_pin(uint8_t pin) { this->pin_ = pin; }
  void set_num_leds(uint16_t num_leds) { this->num_leds_ = num_leds; }
  void set_chipset(ChimeraChipset chipset) { this->chipset_ = chipset; }
  void set_rgb_order(RGBOrder order) { this->rgb_order_ = order; }
  void set_is_rgbw(bool is_rgbw) { this->is_rgbw_ = is_rgbw; }
  void set_is_wrgb(bool is_wrgb) { this->is_wrgb_ = is_wrgb; }
  bool has_white_channel() const { return this->is_rgbw_ || this->is_wrgb_; }
  void set_turn_on_brightness(float brightness) {
    this->turn_on_defaults_.has_brightness = true;
    this->turn_on_defaults_.brightness = brightness;
  }
  void set_turn_on_color_rgb(float r, float g, float b) {
    this->turn_on_defaults_.has_color = true;
    this->turn_on_defaults_.color_has_white = false;
    this->turn_on_defaults_.red = r;
    this->turn_on_defaults_.green = g;
    this->turn_on_defaults_.blue = b;
    this->turn_on_defaults_.white = 0.0f;
  }
  void set_turn_on_color_rgbw(float r, float g, float b, float w) {
    this->turn_on_defaults_.has_color = true;
    this->turn_on_defaults_.color_has_white = true;
    this->turn_on_defaults_.red = r;
    this->turn_on_defaults_.green = g;
    this->turn_on_defaults_.blue = b;
    this->turn_on_defaults_.white = w;
  }
  void set_force_white_switch(switch_::Switch *sw);
  switch_::Switch *get_force_white_switch() const { return this->force_white_sw_; }
  bool is_force_white_active_for(light::LightState *state) const;
  void set_rmt_symbols(uint32_t symbols) { this->rmt_symbols_ = symbols; }
  void set_max_refresh_rate(uint32_t interval_us) {
    this->max_refresh_rate_ = interval_us;
  }

  // SPI transport setters
  void set_transport(CFXTransport t) { this->transport_ = t; }
  CFXTransport get_transport() const { return this->transport_; }
  bool is_spi_transport() const { return this->transport_ == TRANSPORT_SPI; }
  void set_spi_data_pin(uint8_t pin) { this->spi_data_pin_ = pin; }
  void set_spi_clock_pin(uint8_t pin) { this->spi_clock_pin_ = pin; }
  void set_spi_speed_hz(uint32_t hz) { this->spi_speed_hz_ = hz; }
  void set_spi_host(CFXSPIHost host) { this->spi_host_ = host; }

  // --- Segment configuration (codegen setters) ---
  void add_segment_def(const std::string &id, uint16_t start, uint16_t stop,
                       bool mirror, uint8_t intro, uint8_t outro,
                       float intro_dur, float outro_dur) {
    if (segment_defs_.size() >= MAX_CFX_SEGMENTS)
      return;
    segment_defs_.push_back(
        {id, start, stop, mirror, intro, outro, intro_dur, outro_dur});
  }
  const std::vector<CFXSegmentDef> &get_segment_defs() const {
    return segment_defs_;
  }
  bool has_segments() const { return !segment_defs_.empty(); }

  // Phase 2: Register virtual segment lights for state synchronization
  void set_master_light_state(light::LightState *state) {
    master_light_state_ = state;
  }
  light::LightState *get_master_light_state() const {
    return master_light_state_;
  }

  void add_segment_light_state(light::LightState *state) {
    segment_light_states_.push_back(state);
  }
  const std::vector<light::LightState *> &get_segment_light_states() const {
    return segment_light_states_;
  }

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
    return seg.intro_duration_s > 0.0f ? seg.intro_duration_s
                                       : default_intro_dur_s_;
  }
  float resolve_outro_dur(const CFXSegmentDef &seg) const {
    return seg.outro_duration_s > 0.0f ? seg.outro_duration_s
                                       : default_outro_dur_s_;
  }

  // Visualizer setters
  void set_visualizer_ip(const std::string &ip) {
#ifdef CFX_VISUALIZER_ENABLED
    this->visualizer_ip_ = ip;
#endif
  }
  void set_visualizer_port(uint16_t port) {
#ifdef CFX_VISUALIZER_ENABLED
    this->visualizer_port_ = port;
#endif
  }
  void set_visualizer_enabled(bool enabled) {
#ifdef CFX_VISUALIZER_ENABLED
    this->visualizer_enabled_ = enabled;
#endif
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

  // Compute timing parameters from chipset (RMT only)
  void configure_timing_();

  // Transport-specific setup/flush helpers
  void setup_rmt_();
  void setup_spi_();
  void flush_rmt_();
  void flush_spi_();
  void bind_force_white_switch_();
  void maybe_apply_turn_on_defaults_(light::LightState *state, bool &prev_on_state);
  void repaint_force_white_solid_(bool state);
  void release_outro_callback_storage_();
  bool wait_for_spi_tx_(uint32_t timeout_ms, const char *context);
  uint32_t get_spi_frame_timeout_ms_() const;
  bool use_blocking_spi_diag_() const { return this->is_spi_transport(); }

  // SPI frame geometry helpers
  size_t get_spi_frame_size_() const;
  size_t get_spi_end_frame_size_() const;
  uint8_t get_spi_end_frame_byte_() const;
  static spi_host_device_t resolve_spi_host_(CFXSPIHost host);

  // Pixel data buffer (written by effects via ESPColorView)
  uint8_t *buf_{nullptr};

  static std::vector<CFXLightOutput *> instances_;

  // Callbacks used to execute Outro animations after ESPHome turns the light
  // off
  std::vector<OutroCallback> outro_cbs_;

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
  std::vector<CFXSegmentDef> segment_defs_;

  light::LightState *master_light_state_{nullptr};
  std::vector<light::LightState *> segment_light_states_;

  uint8_t default_intro_mode_{0}; // 0 = auto-detect from chip variant
  uint8_t default_outro_mode_{0};
  float default_intro_dur_s_{0.0f};
  float default_outro_dur_s_{0.0f};

  bool is_rgbw_{false};
  bool is_wrgb_{false};
  switch_::Switch *force_white_sw_{nullptr};
  switch_::Switch *force_white_cb_sw_{nullptr};
  uint32_t rmt_symbols_{0}; // 0 = auto-detect from chip variant

  // SPI transport fields (idle harmlessly for RMT instances)
  CFXTransport transport_{TRANSPORT_RMT};
  uint8_t spi_data_pin_{0};
  uint8_t spi_clock_pin_{0};
  uint32_t spi_speed_hz_{10000000};  // 10 MHz default
  CFXSPIHost spi_host_{SPI_HOST_2};
  spi_device_handle_t spi_device_{nullptr};
  uint8_t *spi_frame_buf_{nullptr};
  spi_transaction_t spi_trans_{};
  bool spi_tx_in_flight_{false};
  uint32_t spi_wait_count_{0};
  uint32_t spi_wait_timeout_count_{0};
  uint32_t spi_queue_error_count_{0};
  uint8_t spi_diag_write_logs_{0};
  uint8_t spi_diag_flush_logs_{0};
  uint8_t spi_diag_timing_logs_{0};
  uint8_t spi_diag_throttle_logs_{0};
  uint32_t spi_last_flush_ms_{0};

  // Refresh rate limiting
  uint32_t last_refresh_{0};
  optional<uint32_t> max_refresh_rate_{};

  // Visualizer
  int socket_fd_{-1};
#ifdef CFX_VISUALIZER_ENABLED
  std::string visualizer_ip_{""};
  uint16_t visualizer_port_{7777};
  bool visualizer_enabled_{false};
  std::vector<uint8_t> visualizer_pkt_;
#endif  // CFX_VISUALIZER_ENABLED

  // State synchronization listeners
  class MasterListener : public light::LightRemoteValuesListener {
  public:
    MasterListener(CFXLightOutput *parent) : parent_(parent) {}
    void on_light_remote_values_update() override {
      parent_->on_master_update();
    }

  private:
    CFXLightOutput *parent_;
  };

  class SegmentListener : public light::LightRemoteValuesListener {
  public:
    SegmentListener(CFXLightOutput *parent) : parent_(parent) {}
    void on_light_remote_values_update() override {
      parent_->on_segment_update();
    }

  private:
    CFXLightOutput *parent_;
  };

  MasterListener *master_listener_{nullptr};
  std::vector<SegmentListener *> segment_listeners_;

  bool is_syncing_{false};
  bool applying_turn_on_defaults_{false};
  bool prev_master_state_{false};
  bool prev_master_defaults_state_{false};
  uint8_t tracked_brightness_{0};
  CFXTurnOnDefaults turn_on_defaults_{};
};

} // namespace cfx_light
} // namespace esphome

#endif // USE_ESP32
