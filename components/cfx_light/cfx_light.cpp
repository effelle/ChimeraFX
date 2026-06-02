/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * CFXLight - Async DMA LED Strip Driver for ESPHome
 *
 * Phase 1: Skeleton — pixel buffer, get_view_internal, basic setup/show.
 * Phase 2 will add async DMA fire-and-forget via rmt_transmit.
 */

#include "cfx_light.h"
#include "cfx_power_manager.h"
#include "cfx_virtual_segment_light.h"
#include "cfx_transmit_barrier.h"
#include "../cfx_effect/cfx_control.h"
#include "../cfx_effect/cfx_scheduler.h"
#include "../cfx_effect/cfx_utils.h"
#include "../cfx_effect/cfx_effect_stub.h"

#ifdef USE_WIFI
#include <lwip/inet.h>
#include <lwip/sockets.h>

#endif

#ifdef USE_ESP32

#include "esphome/components/light/light_state.h"
#include "esphome/components/light/transformers.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#if defined(CONFIG_IDF_TARGET_ESP32)
#include <esp_intr_alloc.h>
#include <esp_private/periph_ctrl.h>
#include <rom/gpio.h>
#include <rom/lldesc.h>
#include <soc/gpio_sig_map.h>
#include <soc/i2s_struct.h>
#include <soc/periph_defs.h>
#endif
#ifdef CFX_PARALLEL_I80_ENABLED
#include <esp_lcd_panel_io.h>
#endif
#include <esp_system.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include <esp_clk_tree.h>
#endif

#ifdef USE_CFX_EVENTS
#include "esphome/components/cfx_effect/cfx_event_manager.h"
#endif

namespace esphome {
namespace cfx_light {

static const char *const TAG = "cfx_light";

static portMUX_TYPE g_parallel_mux = portMUX_INITIALIZER_UNLOCKED;

//#define CFX_PARALLEL_TEST_PATTERN

std::vector<CFXVirtualSegmentLight *> CFXVirtualSegmentLight::all_segments;

static const size_t RMT_SYMBOLS_PER_BYTE = 8;
static uint32_t g_last_rmt_launch_us = 0;
static uint32_t g_rmt_launch_seq = 0;
static volatile uint32_t g_rmt_dma_active_count = 0;
static volatile uint32_t g_spi_dma_active_count = 0;

static bool cfx_unicore_build_() {
#ifdef CONFIG_FREERTOS_UNICORE
  return true;
#else
  return false;
#endif
}

#if defined(CONFIG_IDF_TARGET_ESP32)
static const uint8_t PARALLEL_SYMBOL_SAMPLES = 3;
static const uint32_t PARALLEL_PCLK_HZ = 2400000;
static const size_t PARALLEL_RESET_SAMPLES = 240;  // 100 us at 2.4 MHz.
#else
// S3/C3: Also use 3 samples/bit @ 2.4 MHz to reduce DMA memory footprint by 25%.
// This allows driving up to 680 LEDs per lane within 70-80 KB DMA RAM.
static const uint8_t PARALLEL_SYMBOL_SAMPLES = 3;
static const uint32_t PARALLEL_PCLK_HZ = 2400000;
static const size_t PARALLEL_RESET_SAMPLES = 240;  // 100 us at 2.4 MHz.
#endif
static const uint8_t PARALLEL_MAX_LANES = 4;
static const uint8_t PARALLEL_MAX_GROUPS = 2;
static const uint8_t PARALLEL_I80_BUS_WIDTH = 8;
static const uint16_t PARALLEL_CLASSIC_CHUNK_LEDS = 64;
static const uint16_t PARALLEL_LCD_CHUNK_LEDS = 128;
static const uint16_t PARALLEL_LCD_SECONDARY_CHUNK_LEDS = 128;
static const uint16_t PARALLEL_LCD_FALLBACK_CHUNK_LEDS = 64;
static const uint8_t PARALLEL_TX_BUFFER_COUNT = 4;
static const uint8_t PARALLEL_LCD_TX_BUFFER_COUNT = 3;
static const size_t PARALLEL_CLASSIC_DMA_MAX_LEN = 4092;
static const uint8_t PARALLEL_CLASSIC_DESC_PER_BUFFER = 2;
static const uint8_t PARALLEL_CLASSIC_DESC_COUNT =
    2 + (PARALLEL_TX_BUFFER_COUNT * PARALLEL_CLASSIC_DESC_PER_BUFFER) + 1;
static const uint8_t PARALLEL_CLASSIC_SILENCE_DESC_A = 0;
static const uint8_t PARALLEL_CLASSIC_SILENCE_DESC_B = 1;
static const uint8_t PARALLEL_CLASSIC_DATA_DESC_BASE = 2;
static const uint8_t PARALLEL_CLASSIC_RESET_DESC =
    PARALLEL_CLASSIC_DATA_DESC_BASE +
    (PARALLEL_TX_BUFFER_COUNT * PARALLEL_CLASSIC_DESC_PER_BUFFER);
static const size_t PARALLEL_CLASSIC_SILENCE_BYTES = 32;
static const size_t PARALLEL_CANARY_BYTES = 32;
static const uint8_t PARALLEL_CANARY_VALUE = 0xA5;
static const uint32_t PARALLEL_LCD_FLUSH_TIMEOUT_MS = 10;
static const uint32_t PARALLEL_LCD_WHOLE_FLUSH_TIMEOUT_MS = 22;
static const uint32_t PARALLEL_CLASSIC_FLUSH_TIMEOUT_MS = 12;
static const uint32_t PARALLEL_CLASSIC_WHOLE_FLUSH_TIMEOUT_MS = 24;
static const char *const PARALLEL_BACKEND_REV =
    "parallel-v1-classic-i2s-stream-2026-05-18";
static const char *const PARALLEL_LCD_BACKEND_REV =
    "parallel-v1-s3-esp-lcd-fullframe-seg-guarded-2026-05-20";
static const uint8_t PARALLEL_DUMMY_PIN_CANDIDATES[] = {
    4, 5, 13, 14, 16, 17, 18, 26, 27, 32, 33};

struct CFXParallelGroupRuntime {
  bool configured{false};
  bool ready{false};
  bool init_attempted{false};
  bool tx_in_flight{false};
  volatile uint8_t tx_in_flight_count{0};
  volatile uint32_t tx_done_count{0};
  bool force_full_span_next{false};
  bool shutdown_blackout_sent{false};
  bool has_segments{false};
  const char *buffer_policy{"default"};
  uint8_t pending_mask{0};
  uint32_t pending_first_ms{0};
  std::string name{};
  uint8_t group_index{0};
  uint8_t bit_offset{0};
  uint8_t lane_count{0};
  uint8_t strobe_pin{22};
  uint8_t dc_pin{21};
  uint8_t lane_pins[PARALLEL_I80_BUS_WIDTH]{};
  CFXLightOutput *outputs[PARALLEL_MAX_LANES]{};
#if defined(CONFIG_IDF_TARGET_ESP32)
  lldesc_t *classic_descs{nullptr};
  intr_handle_t classic_intr{nullptr};
  uint8_t *classic_silence_buf{nullptr};
  uint8_t *classic_reset_buf{nullptr};
  volatile bool classic_sending{false};
  volatile uint32_t classic_completed_chunks{0};
  volatile uint32_t classic_expected_chunks{0};
  volatile bool classic_auto_relink{false};
  volatile uint32_t classic_dma_errors{0};
  volatile uint32_t classic_relink_count{0};
  uint8_t classic_data_desc_tail[PARALLEL_TX_BUFFER_COUNT]{};
  uint32_t classic_underrun_count{0};
  uint32_t classic_stream_count{0};
#endif
#ifdef CFX_PARALLEL_I80_ENABLED
  esp_lcd_i80_bus_handle_t bus{nullptr};
  esp_lcd_panel_io_handle_t io{nullptr};
#else
  void *bus{nullptr};
  void *io{nullptr};
#endif
  uint8_t *frame_buf{nullptr};
  uint8_t *frame_bufs[PARALLEL_TX_BUFFER_COUNT]{};
  size_t frame_size{0};  // Logical full-frame size, for diagnostics.
  uint16_t chunk_leds{0};
  uint16_t max_leds{0};
  uint16_t last_tx_leds{0};
  uint8_t next_tx_buffer_index{0};
  uint8_t last_tx_buffer_index{0};
  size_t chunk_frame_size{0};
  size_t bus_max_transfer_size{0};
  size_t chunk_alloc_size{0};
  uint8_t buffer_count{0};
  uint32_t tx_count{0};
  uint32_t chunk_tx_count{0};
  uint32_t led_recorded_done_count{0};
  uint32_t coalesced_count{0};
  uint32_t timeout_flush_count{0};
  uint32_t queue_error_count{0};
  uint32_t wait_timeout_count{0};
};

#ifdef CFX_PARALLEL_I80_ENABLED
struct CFXParallelI80SharedRuntime {
  bool ready{false};
  bool init_attempted{false};
  uint8_t strobe_pin{22};
  uint8_t dc_pin{21};
  uint8_t lane_pins[PARALLEL_I80_BUS_WIDTH]{};
  esp_lcd_i80_bus_handle_t bus{nullptr};
  esp_lcd_panel_io_handle_t io{nullptr};
  uint8_t *frame_buf{nullptr};
  uint8_t *frame_bufs[PARALLEL_TX_BUFFER_COUNT]{};
  size_t chunk_alloc_size{0};
  size_t bus_max_transfer_size{0};
  uint8_t buffer_count{0};
  volatile uint8_t tx_in_flight_count{0};
  CFXParallelGroupRuntime *active_group{nullptr};
  volatile uint8_t active_group_mask{0};
  uint8_t pending_group_mask{0};
  uint32_t pending_group_first_ms{0};
  uint32_t combined_tx_count{0};
};
#endif

static CFXParallelGroupRuntime g_parallel_groups[PARALLEL_MAX_GROUPS];
#ifdef CFX_PARALLEL_I80_ENABLED
static CFXParallelI80SharedRuntime g_parallel_i80;
#endif

static CFXParallelGroupRuntime *parallel_group_at_(uint8_t index) {
  if (index >= PARALLEL_MAX_GROUPS) {
    index = 0;
  }
  return &g_parallel_groups[index];
}

static CFXParallelGroupRuntime *parallel_group_for_output_(
    const CFXLightOutput *output) {
  if (output == nullptr) {
    return &g_parallel_groups[0];
  }
  return parallel_group_at_(output->get_parallel_group_index());
}

static uint8_t parallel_configured_group_count_() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < PARALLEL_MAX_GROUPS; i++) {
    if (g_parallel_groups[i].configured) {
      count++;
    }
  }
  return count;
}

static bool wait_for_parallel_group_idle_(uint32_t timeout_us,
                                          CFXParallelGroupRuntime *group,
                                          uint32_t *waited_us = nullptr) {
  const uint32_t wait_start_us = micros();
  uint32_t wdt_feed_us = wait_start_us;
  if (group == nullptr) {
    return true;
  }
  while (group->tx_in_flight_count > 0) {
    const uint32_t now_us = micros();
    if ((now_us - wait_start_us) > timeout_us) {
      if (waited_us != nullptr) {
        *waited_us = now_us - wait_start_us;
      }
      return false;
    }
    esp_rom_delay_us(50);
    if ((micros() - wdt_feed_us) >= 1000u) {
      esphome::App.feed_wdt();
      wdt_feed_us = micros();
    }
  }
  if (waited_us != nullptr) {
    *waited_us = micros() - wait_start_us;
  }
  return true;
}

static bool has_active_rendering_cfx_effect(CFXLightOutput *output);
static bool parallel_shared_whole_group_mode_enabled_();
static bool parallel_shared_segment_group_mode_enabled_();

#if defined(CONFIG_IDF_TARGET_ESP32)
static void parallel_dma_desc_init_(lldesc_t *desc, uint8_t *buf, size_t len,
                                    lldesc_t *next, bool eof) {
  if (desc == nullptr) {
    return;
  }
  memset(desc, 0, sizeof(lldesc_t));
  desc->eof = eof ? 1 : 0;
  desc->owner = 1;
  desc->sosf = 0;
  desc->offset = 0;
  desc->buf = buf;
  desc->size = len;
  desc->length = len;
  desc->qe.stqe_next = next;
}

static void IRAM_ATTR parallel_classic_i2s_isr_(void *arg) {
  auto *group = static_cast<CFXParallelGroupRuntime *>(arg);
  const uint32_t status = I2S1.int_st.val;
  const bool out_eof = I2S1.int_st.out_eof;
  const bool out_dscr_err = I2S1.int_st.out_dscr_err;
  if (out_eof && group != nullptr && group->classic_sending) {
    group->classic_completed_chunks++;
    group->tx_done_count++;
    if (group->classic_auto_relink &&
        group->classic_expected_chunks != 0 &&
        group->classic_completed_chunks >= group->classic_expected_chunks &&
        group->classic_descs != nullptr) {
      group->classic_descs[PARALLEL_CLASSIC_SILENCE_DESC_B].qe.stqe_next =
          &group->classic_descs[PARALLEL_CLASSIC_SILENCE_DESC_A];
      group->classic_auto_relink = false;
      group->classic_relink_count++;
    }
  }
  if (out_dscr_err && group != nullptr) {
    group->classic_dma_errors++;
  }
  I2S1.int_clr.val = status;
}

static esp_err_t parallel_classic_set_clock_() {
  I2S1.clkm_conf.val = 0;
  I2S1.clkm_conf.clk_en = 1;
  I2S1.clkm_conf.clka_en = 0;
  I2S1.clkm_conf.clkm_div_num = 16;
  I2S1.clkm_conf.clkm_div_b = 2;
  I2S1.clkm_conf.clkm_div_a = 3;

  I2S1.sample_rate_conf.val = 0;
  I2S1.sample_rate_conf.tx_bck_div_num = 4;
  I2S1.sample_rate_conf.tx_bits_mod = 8;
  return ESP_OK;
}

static esp_err_t parallel_classic_configure_i2s_(CFXParallelGroupRuntime *group) {
  if (group == nullptr || group->classic_descs == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  periph_module_enable(PERIPH_I2S1_MODULE);

  for (uint8_t lane = 0; lane < group->lane_count; lane++) {
    gpio_reset_pin(static_cast<gpio_num_t>(group->lane_pins[lane]));
    gpio_set_direction(static_cast<gpio_num_t>(group->lane_pins[lane]),
                       GPIO_MODE_OUTPUT);
    gpio_matrix_out(group->lane_pins[lane], I2S1O_DATA_OUT0_IDX + lane, false,
                    false);
  }

  I2S1.out_link.stop = 1;
  I2S1.conf.tx_start = 0;
  I2S1.int_ena.val = 0;
  I2S1.int_clr.val = 0xFFFFFFFF;
  I2S1.fifo_conf.dscr_en = 0;

  I2S1.conf.tx_reset = 1;
  I2S1.conf.tx_reset = 0;
  I2S1.conf.rx_reset = 1;
  I2S1.conf.rx_reset = 0;
  I2S1.lc_conf.in_rst = 1;
  I2S1.lc_conf.in_rst = 0;
  I2S1.lc_conf.out_rst = 1;
  I2S1.lc_conf.out_rst = 0;
  I2S1.conf.rx_fifo_reset = 1;
  I2S1.conf.rx_fifo_reset = 0;
  I2S1.conf.tx_fifo_reset = 1;
  I2S1.conf.tx_fifo_reset = 0;

  I2S1.conf2.val = 0;
  I2S1.conf2.lcd_en = 1;
  I2S1.conf2.lcd_tx_wrx2_en = 1;
  I2S1.conf2.lcd_tx_sdx2_en = 0;

  I2S1.lc_conf.val = 0;
  I2S1.lc_conf.out_eof_mode = 1;

  I2S1.pdm_conf.pcm2pdm_conv_en = 0;
  I2S1.pdm_conf.pdm2pcm_conv_en = 0;

  I2S1.fifo_conf.val = 0;
  I2S1.fifo_conf.tx_fifo_mod_force_en = 1;
  I2S1.fifo_conf.tx_fifo_mod = 1;
  I2S1.fifo_conf.tx_data_num = 32;

  I2S1.conf1.val = 0;
  I2S1.conf1.tx_stop_en = 0;
  I2S1.conf1.tx_pcm_bypass = 1;

  I2S1.conf_chan.val = 0;
  I2S1.conf_chan.tx_chan_mod = 1;

  I2S1.conf.val = 0;
  I2S1.conf.tx_msb_shift = 0;
  I2S1.conf.tx_right_first = 1;
  I2S1.conf.tx_short_sync = 0;

  I2S1.timing.val = 0;
  I2S1.pdm_conf.tx_pdm_en = 0;

  parallel_classic_set_clock_();

  I2S1.lc_conf.in_rst = 1;
  I2S1.lc_conf.out_rst = 1;
  I2S1.lc_conf.ahbm_rst = 1;
  I2S1.lc_conf.ahbm_fifo_rst = 1;
  I2S1.lc_conf.in_rst = 0;
  I2S1.lc_conf.out_rst = 0;
  I2S1.lc_conf.ahbm_rst = 0;
  I2S1.lc_conf.ahbm_fifo_rst = 0;
  I2S1.conf.tx_reset = 1;
  I2S1.conf.tx_fifo_reset = 1;
  I2S1.conf.rx_fifo_reset = 1;
  I2S1.conf.tx_reset = 0;
  I2S1.conf.tx_fifo_reset = 0;
  I2S1.conf.rx_fifo_reset = 0;

  I2S1.lc_conf.val = 0;
  I2S1.lc_conf.out_data_burst_en = 0;
  I2S1.lc_conf.indscr_burst_en = 0;

  esp_err_t err = esp_intr_alloc(ETS_I2S1_INTR_SOURCE,
                                 ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL1,
                                 parallel_classic_i2s_isr_, group,
                                 &group->classic_intr);
  if (err != ESP_OK) {
    return err;
  }
  I2S1.int_ena.out_eof = 1;
  I2S1.int_ena.out_dscr_err = 1;
  I2S1.fifo_conf.dscr_en = 1;
  I2S1.out_link.start = 0;
  I2S1.out_link.addr =
      reinterpret_cast<uint32_t>(&group->classic_descs[PARALLEL_CLASSIC_SILENCE_DESC_A]);
  I2S1.out_link.start = 1;
  I2S1.conf.tx_start = 1;
  return ESP_OK;
}
#endif

static bool parallel_pin_used_(uint8_t pin, const uint8_t *pins, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    if (pins[i] == pin) {
      return true;
    }
  }
  return false;
}

static uint32_t rmt_launch_stagger_gap_us() {
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) || \
    defined(CONFIG_IDF_TARGET_ESP32P4)
  return 300;
#else
  return 0;
#endif
}

static const char *rmt_dma_backend_label() {
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
  return "GDMA";
#else
  return "DMA";
#endif
}

static const char *transport_label(CFXTransport transport) {
  switch (transport) {
  case TRANSPORT_SPI:
    return "SPI";
  case TRANSPORT_PARALLEL:
    return "PARALLEL";
  case TRANSPORT_RMT:
  default:
    return "RMT";
  }
}

#ifdef CFX_PARALLEL_I80_ENABLED
static bool IRAM_ATTR parallel_tx_done_cb_(esp_lcd_panel_io_handle_t,
                                           esp_lcd_panel_io_event_data_t *,
                                           void *user_ctx) {
  auto *shared = static_cast<CFXParallelI80SharedRuntime *>(user_ctx);
  auto *group = shared != nullptr ? shared->active_group : nullptr;
  if (shared != nullptr) {
    uint8_t group_mask = shared->active_group_mask;
    if (group_mask == 0 && group != nullptr) {
      group_mask = static_cast<uint8_t>(1u << group->group_index);
    }
    if (group_mask != 0) {
      portENTER_CRITICAL_ISR(&g_parallel_mux);
      for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
        if ((group_mask & static_cast<uint8_t>(1u << gi)) == 0) {
          continue;
        }
        auto &done_group = g_parallel_groups[gi];
        if (done_group.tx_in_flight_count > 0) {
          done_group.tx_in_flight_count--;
        }
        done_group.tx_in_flight = done_group.tx_in_flight_count > 0;
        done_group.tx_done_count++;
      }
      if (shared->tx_in_flight_count > 0) {
        shared->tx_in_flight_count--;
      }
      if (shared->tx_in_flight_count == 0) {
        shared->active_group_mask = 0;
      }
      portEXIT_CRITICAL_ISR(&g_parallel_mux);
    }
  }
  return false;
}
#endif

static uint32_t rmt_non_dma_symbols(uint32_t configured_symbols) {
#if defined(CONFIG_IDF_TARGET_ESP32)
  return configured_symbols < 128u ? 128u : configured_symbols;
#else
  return configured_symbols;
#endif
}

static size_t rmt_encoder_min_chunk_size(uint32_t mem_block_symbols) {
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S3) || \
    defined(CONFIG_IDF_TARGET_ESP32P4)
  // C3/S3/P4 RMT blocks are small, and waking the simple encoder every byte
  // creates heavy ISR pressure on long strips. Refill once roughly 3/4 of the
  // block is free, rounded down to whole bytes, so hardware still has symbols
  // to transmit while the callback prepares the next chunk.
  size_t chunk = (mem_block_symbols * 3u) / 4u;
  chunk = (chunk / RMT_SYMBOLS_PER_BYTE) * RMT_SYMBOLS_PER_BYTE;
  if (chunk >= RMT_SYMBOLS_PER_BYTE)
    return chunk;
#else
  (void) mem_block_symbols;
#endif
  return RMT_SYMBOLS_PER_BYTE;
}

static uint32_t wait_for_dma_counter_idle_(volatile uint32_t *counter,
                                           uint32_t timeout_us) {
  if (counter == nullptr || *counter == 0) {
    return 0;
  }

  const uint32_t wait_start_us = micros();
  while (*counter > 0) {
    const uint32_t elapsed = micros() - wait_start_us;
    if (elapsed >= timeout_us) {
      return elapsed;
    }
    if ((elapsed % 5000u) < 200u) {
      esphome::App.feed_wdt();
    }
    esp_rom_delay_us(100);
  }
  return micros() - wait_start_us;
}

static void IRAM_ATTR spi_tx_done_cb_(spi_transaction_t *) {
  if (g_spi_dma_active_count > 0) {
    g_spi_dma_active_count--;
  }
}

static light::ColorMode resolve_low_ram_warning_mode(light::LightState *state) {
  if (state == nullptr) {
    return light::ColorMode::RGB;
  }

  const auto traits = state->get_traits();
  if (traits.supports_color_mode(light::ColorMode::RGB)) {
    return light::ColorMode::RGB;
  }
  if (traits.supports_color_mode(light::ColorMode::RGB_WHITE)) {
    return light::ColorMode::RGB_WHITE;
  }
  if (traits.supports_color_mode(light::ColorMode::WHITE)) {
    return light::ColorMode::WHITE;
  }

  auto current_mode = state->remote_values.get_color_mode();
  if (current_mode != light::ColorMode::UNKNOWN) {
    return current_mode;
  }
  return light::ColorMode::ON_OFF;
}

static bool resolve_low_ram_warning_segment(light::LightState *state,
                                            const std::vector<light::LightState *> &segment_states,
                                            const std::vector<CFXSegmentDef> &defs,
                                            uint16_t &start, uint16_t &stop) {
  if (state == nullptr) {
    return false;
  }
  for (size_t i = 0; i < segment_states.size() && i < defs.size(); i++) {
    if (segment_states[i] == state) {
      start = defs[i].start;
      stop = defs[i].stop;
      return true;
    }
  }
  return false;
}

static chimera_fx::CFXAddressableLightEffect *
resolve_perf_diag_effect(CFXLightOutput *output);
static bool perf_diag_enabled_for_effect(
    chimera_fx::CFXAddressableLightEffect *effect);
static bool runtime_debug_enabled_for_output(CFXLightOutput *output);

void CFXLightOutput::set_force_white_switch(switch_::Switch *sw) {
  if (this->force_white_sw_ == sw && this->force_white_cb_sw_ == sw)
    return;

  this->force_white_sw_ = sw;
  this->bind_force_white_switch_();
}

void CFXLightOutput::bind_force_white_switch_() {
  if (this->force_white_sw_ == nullptr)
    return;
  if (this->force_white_cb_sw_ == this->force_white_sw_)
    return;

  this->force_white_cb_sw_ = this->force_white_sw_;
  this->force_white_sw_->add_on_state_callback([this](bool state) {
    this->wake_mono_idle_light_state_(this->master_light_state_);
    this->repaint_force_white_solid_(state);
  });
}

bool CFXLightOutput::is_force_white_active_for(light::LightState *state) const {
  if (!this->has_white_channel())
    return false;

  if (state != nullptr) {
    auto *ctrl = chimera_fx::CFXControl::find(state);
    if (ctrl != nullptr && ctrl->get_force_white() != nullptr)
      return ctrl->get_force_white()->state;
  }

  return this->force_white_sw_ != nullptr && this->force_white_sw_->state;
}

void CFXLightOutput::repaint_force_white_solid_(bool state) {
  if (this->is_effect_active() || this->has_segments())
    return;

  if (this->state_parent_ != nullptr) {
    if (!this->state_parent_->remote_values.is_on() ||
        this->state_parent_->current_values.get_state() <= 0.0f) {
      return;
    }

    auto val = this->state_parent_->current_values;
    Color c = light::color_from_light_color_values(val);
    if (state)
      cfx::apply_force_white(c.r, c.g, c.b, c.w);
    this->all() = c;
    this->schedule_show();
  }
}

std::unique_ptr<light::LightTransformer>
CFXLightOutput::create_default_transition() {
  // For CFXLight, default_transition_length is only meant for plain
  // solid-color mode. Using the generic LightTransitionTransformer gives a
  // true LightColorValues brightness ramp instead of relying on ESPHome's
  // addressable buffer-decay transition path.
  return make_unique<light::LightTransitionTransformer>();
}

void CFXLightOutput::release_outro_callback_storage_() {
  if (this->outro_cbs_.empty()) {
    std::vector<OutroCallback>().swap(this->outro_cbs_);
  }
}

void CFXLightOutput::add_outro_callback(OutroCallback cb) {
  if (this->outro_cbs_.empty()) {
    this->outro_last_frame_ms_ = 0;
  }
  this->outro_cbs_.push_back(cb);
  this->update_high_frequency_loop_request_();
}

void CFXLightOutput::drain_outro_callbacks() {
  if (this->outro_cbs_.empty()) {
    return;
  }

  for (auto it = this->outro_cbs_.begin(); it != this->outro_cbs_.end();) {
    bool done = (*it)();
    if (done) {
      it = this->outro_cbs_.erase(it);
    } else {
      ++it;
    }
  }
  this->release_outro_callback_storage_();
}

void CFXLightOutput::reset_perf_diag_() {
  this->perf_diag_last_flush_valid_ = false;
  this->perf_diag_last_flush_total_us_ = 0;
  this->perf_diag_last_flush_tx_us_ = 0;
  this->perf_diag_flush_count_ = 0;
  this->perf_diag_max_queue_us_ = 0;
  this->perf_diag_max_write_us_ = 0;
  this->perf_diag_max_flush_us_ = 0;
  this->perf_diag_max_tx_us_ = 0;
  this->perf_diag_max_wait_us_ = 0;
  this->perf_diag_max_gate_us_ = 0;
  this->perf_diag_max_gate_defers_ = 0;
  this->perf_diag_max_refresh_defers_ = 0;
  this->perf_diag_max_partial_missing_ = 0;
  this->perf_diag_max_rmt_starve_count_ = 0;
  this->perf_diag_max_rmt_reset_starve_count_ = 0;
  this->perf_diag_max_seg_contrib_ = 0;
  this->perf_diag_max_spi_flush_interval_us_ = 0;
  this->perf_diag_max_spi_pack_us_ = 0;
  this->perf_diag_max_spi_queue_us_ = 0;
  this->perf_diag_max_show_request_interval_us_ = 0;
  this->perf_diag_max_dma_guard_wait_us_ = 0;
  this->perf_diag_max_rmt_tx_launch_interval_us_ = 0;
  this->perf_diag_min_rmt_symbols_free_ = UINT32_MAX;
  this->perf_diag_total_queue_us_ = 0;
  this->perf_diag_total_write_us_ = 0;
  this->perf_diag_total_flush_us_ = 0;
  this->perf_diag_total_tx_us_ = 0;
  this->perf_diag_total_wait_us_ = 0;
  this->perf_diag_total_gate_us_ = 0;
  this->perf_diag_total_gate_defers_ = 0;
  this->perf_diag_total_refresh_defers_ = 0;
  this->perf_diag_total_partial_flushes_ = 0;
  this->perf_diag_total_rmt_starve_count_ = 0;
  this->perf_diag_total_rmt_reset_starve_count_ = 0;
  this->perf_diag_total_rmt_callback_count_ = 0;
  this->perf_diag_total_seg_contrib_ = 0;
  this->perf_diag_total_spi_flush_interval_us_ = 0;
  this->perf_diag_total_spi_pack_us_ = 0;
  this->perf_diag_total_spi_queue_us_ = 0;
  this->perf_diag_total_show_request_interval_us_ = 0;
  this->perf_diag_total_rmt_coalesced_flushes_ = 0;
  this->perf_diag_total_rmt_tx_launches_ = 0;
  this->perf_diag_total_rmt_tx_launch_interval_us_ = 0;
  this->perf_diag_total_dma_guard_wait_us_ = 0;
  this->perf_diag_total_dma_guard_hits_ = 0;
  this->perf_diag_total_dma_guard_timeouts_ = 0;
  this->perf_diag_show_request_interval_count_ = 0;
  this->perf_diag_last_show_request_interval_us_ = 0;
  this->perf_diag_spi_flush_interval_count_ = 0;
  this->perf_diag_last_spi_flush_start_us_ = 0;
  this->perf_diag_rmt_tx_launch_interval_count_ = 0;
  this->perf_diag_last_rmt_tx_launch_us_ = 0;
}

void CFXLightOutput::record_led_frame_() {
  const uint32_t now_us = micros();
  if (this->led_fps_last_frame_us_ == 0 ||
      now_us - this->led_fps_last_frame_us_ > 1500000u) {
    this->led_fps_window_start_us_ = now_us;
    this->led_fps_last_frame_us_ = now_us;
    this->led_fps_window_intervals_ = 0;
    this->led_fps_valid_ = false;
    return;
  }

  this->led_fps_last_frame_us_ = now_us;
  this->led_fps_window_intervals_++;
  const uint32_t elapsed_us = now_us - this->led_fps_window_start_us_;
  if (elapsed_us >= 1000000u) {
    this->led_fps_ =
        (1000000.0f * this->led_fps_window_intervals_) / elapsed_us;
    this->led_fps_valid_ = true;
    this->led_fps_window_start_us_ = now_us;
    this->led_fps_window_intervals_ = 0;
  }
}

uint32_t CFXLightOutput::get_rmt_wire_frame_floor_us() const {
  if (!this->is_rmt_transport() || this->num_leds_ == 0) {
    return 0;
  }

  uint32_t bit_time_ns = 1250;
  uint32_t reset_ns = 280000;
  switch (this->chipset_) {
  case CHIPSET_SK6812:
    bit_time_ns = 1200;
    reset_ns = 80000;
    break;
  case CHIPSET_WS2811:
    bit_time_ns = 2500;
    reset_ns = 280000;
    break;
  case CHIPSET_WS2812X:
  default:
    bit_time_ns = 1250;
    reset_ns = 280000;
    break;
  }

  constexpr uint32_t RMT_FRAME_GUARD_US = 1800;
  const uint64_t bit_count =
      static_cast<uint64_t>(this->get_rmt_physical_led_count_()) *
      static_cast<uint64_t>(this->get_pixel_stride_()) * 8ULL;
  const uint64_t wire_ns =
      bit_count * static_cast<uint64_t>(bit_time_ns) +
      static_cast<uint64_t>(reset_ns);
  return static_cast<uint32_t>((wire_ns + 999ULL) / 1000ULL) +
         RMT_FRAME_GUARD_US;
}

uint32_t CFXLightOutput::get_effective_rmt_update_interval_ms(
    uint32_t requested_ms) const {
  uint32_t effective_ms = requested_ms;
  if (effective_ms != 0 && effective_ms < 17) {
    effective_ms = 17;
  }

  if (!this->is_rmt_transport()) {
    if (this->max_refresh_rate_.has_value()) {
      const uint32_t max_refresh_ms =
          (*this->max_refresh_rate_ + 999u) / 1000u;
      if (max_refresh_ms > effective_ms) {
        effective_ms = max_refresh_ms;
      }
    }
    return effective_ms;
  }

  const uint32_t wire_floor_us = this->get_rmt_wire_frame_floor_us();
  if (wire_floor_us > 0) {
    uint32_t wire_floor_ms = (wire_floor_us + 999u) / 1000u;
    // C3 has one small RMT block on a single core. Long RGBW frames can be
    // technically cadence-clean yet still leave too little slack for WiFi/HA
    // and runner work, showing as rare visual glitches near the limit.
    if (this->rmt_c3_stability_cushion_) {
      wire_floor_ms += this->has_segments() ? 2u : 1u;
    }
    if (wire_floor_ms > effective_ms) {
      effective_ms = wire_floor_ms;
    }
  }

  if (this->max_refresh_rate_.has_value()) {
    const uint32_t max_refresh_ms =
        (*this->max_refresh_rate_ + 999u) / 1000u;
    if (max_refresh_ms > effective_ms) {
      effective_ms = max_refresh_ms;
    }
  }
  return effective_ms;
}

void CFXLightOutput::record_parallel_completed_led_frames_() {
#if !defined(CONFIG_IDF_TARGET_ESP32) && defined(CFX_PARALLEL_I80_ENABLED)
  auto &g_parallel_group = *parallel_group_for_output_(this);
  if (!this->is_parallel_transport() || !g_parallel_group.configured) {
    return;
  }
  const uint32_t done_count =
      static_cast<uint32_t>(g_parallel_group.tx_done_count);
  while (g_parallel_group.led_recorded_done_count < done_count) {
    g_parallel_group.led_recorded_done_count++;
    for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
      auto *lane_output = g_parallel_group.outputs[lane];
      if (lane_output != nullptr) {
        lane_output->record_led_frame_();
        if (lane_output->power_manager_ != nullptr) {
          lane_output->power_manager_->record_output_frame(lane_output);
        }
      }
    }
  }
#endif
}

void CFXLightOutput::record_perf_diag_flush_(uint32_t write_start_us,
                                             bool perf_diag_enabled,
                                             bool spi_cadence_diag_enabled,
                                             bool rmt_cadence_diag_enabled) {
  if (!(perf_diag_enabled || spi_cadence_diag_enabled ||
        rmt_cadence_diag_enabled) ||
      !this->perf_diag_last_flush_valid_) {
    return;
  }

  uint32_t queue_us = 0;
  if (this->perf_diag_last_show_request_us_ != 0) {
    queue_us = write_start_us - this->perf_diag_last_show_request_us_;
    this->perf_diag_total_queue_us_ += queue_us;
    if (queue_us > this->perf_diag_max_queue_us_) {
      this->perf_diag_max_queue_us_ = queue_us;
    }
  }
  this->perf_diag_last_show_request_us_ = 0;

  const uint32_t write_us = micros() - write_start_us;
  this->perf_diag_total_write_us_ += write_us;
  this->perf_diag_total_flush_us_ += this->perf_diag_last_flush_total_us_;
  this->perf_diag_total_tx_us_ += this->perf_diag_last_flush_tx_us_;
  this->perf_diag_total_seg_contrib_ += this->seg_last_flush_count_;
  this->perf_diag_total_gate_defers_ += this->perf_diag_pending_gate_defers_;

  if (write_us > this->perf_diag_max_write_us_) {
    this->perf_diag_max_write_us_ = write_us;
  }
  if (this->perf_diag_last_flush_total_us_ > this->perf_diag_max_flush_us_) {
    this->perf_diag_max_flush_us_ = this->perf_diag_last_flush_total_us_;
  }
  if (this->perf_diag_last_flush_tx_us_ > this->perf_diag_max_tx_us_) {
    this->perf_diag_max_tx_us_ = this->perf_diag_last_flush_tx_us_;
  }
  if (this->seg_last_flush_count_ > this->perf_diag_max_seg_contrib_) {
    this->perf_diag_max_seg_contrib_ = this->seg_last_flush_count_;
  }
  if (this->perf_diag_pending_gate_defers_ > this->perf_diag_max_gate_defers_) {
    this->perf_diag_max_gate_defers_ = this->perf_diag_pending_gate_defers_;
  }
  this->perf_diag_pending_gate_defers_ = 0;
  this->seg_last_flush_count_ = 0;
  this->seg_last_flush_mask_ = 0;

  const uint32_t now_ms = esphome::millis();
  if (this->perf_diag_last_log_ms_ == 0) {
    this->perf_diag_last_log_ms_ = now_ms;
  } else if ((now_ms - this->perf_diag_last_log_ms_) >= 2000 &&
             this->perf_diag_flush_count_ > 0) {
    if (spi_cadence_diag_enabled) {
      this->log_spi_cadence_diag_();
    } else if (this->is_spi_transport()) {
      this->log_spi_cadence_diag_(true);
    } else if (perf_diag_enabled || rmt_cadence_diag_enabled) {
      this->log_rmt_cadence_diag_(true);
    } else {
      this->reset_perf_diag_();
    }
    this->perf_diag_last_log_ms_ = now_ms;
  }
}

void CFXLightOutput::log_spi_cadence_diag_(bool force) {
  if (!this->is_spi_transport()) {
    return;
  }
  if (!runtime_debug_enabled_for_output(this)) {
    return;
  }

  const uint32_t now_ms = esphome::millis();
  if (this->perf_diag_spi_loop_log_ms_ == 0) {
    this->perf_diag_spi_loop_log_ms_ = now_ms;
    return;
  }
  if ((now_ms - this->perf_diag_spi_loop_log_ms_) < 2000) {
    return;
  }
  this->perf_diag_spi_loop_log_ms_ = now_ms;

  const char *light_name =
      (this->state_parent_ != nullptr) ? this->state_parent_->get_name().c_str()
                                       : "<spi>";
  const uint32_t frames = this->perf_diag_flush_count_;
  const uint32_t safe_frames = frames > 0 ? frames : 1;
  const uint32_t flush_dt_count =
      this->perf_diag_spi_flush_interval_count_ > 0
          ? this->perf_diag_spi_flush_interval_count_
          : 1;
  const uint32_t avg_flush_dt_us = static_cast<uint32_t>(
      this->perf_diag_total_spi_flush_interval_us_ / flush_dt_count);
  const uint32_t avg_write_us =
      static_cast<uint32_t>(this->perf_diag_total_write_us_ / safe_frames);
  const uint32_t avg_flush_us =
      static_cast<uint32_t>(this->perf_diag_total_flush_us_ / safe_frames);
  const uint32_t avg_wait_us =
      static_cast<uint32_t>(this->perf_diag_total_wait_us_ / safe_frames);
  const uint32_t avg_pack_us =
      static_cast<uint32_t>(this->perf_diag_total_spi_pack_us_ / safe_frames);
  const uint32_t avg_queue_us =
      static_cast<uint32_t>(this->perf_diag_total_spi_queue_us_ / safe_frames);
  const uint32_t avg_show_queue_us =
      static_cast<uint32_t>(this->perf_diag_total_queue_us_ / safe_frames);
  const uint32_t show_req_dt_count =
      this->perf_diag_show_request_interval_count_ > 0
          ? this->perf_diag_show_request_interval_count_
          : 1;
  const uint32_t avg_show_req_dt_us = static_cast<uint32_t>(
      this->perf_diag_total_show_request_interval_us_ / show_req_dt_count);
  const uint64_t guard_hits = this->perf_diag_total_dma_guard_hits_;
  const uint32_t avg_guard_us =
      guard_hits > 0
          ? static_cast<uint32_t>(this->perf_diag_total_dma_guard_wait_us_ /
                                  guard_hits)
          : 0;
  char led_fps_text[16];
  cfx::FrameDiagnostics::format_led_fps(this->get_led_fps(), led_fps_text,
                                        sizeof(led_fps_text));
  ESP_LOGV(TAG,
           "CFX spi_cad[%s] frames=%" PRIu32
           " LedFPS=%s avg_us(dt=%" PRIu32 " show_q=%" PRIu32
           " write=%" PRIu32 " flush=%" PRIu32 " wait=%" PRIu32
           " pack=%" PRIu32 " queue=%" PRIu32 ")"
           " max_us(dt=%" PRIu32 " show_q=%" PRIu32
           " write=%" PRIu32 " flush=%" PRIu32 " wait=%" PRIu32
           " pack=%" PRIu32 " queue=%" PRIu32 ")"
           " req_us(avg=%" PRIu32 " max=%" PRIu32 ")"
           " guard(avg=%" PRIu32 " max=%" PRIu32
           " hits=%" PRIu64 " timeout=%" PRIu64 ")"
           " defers(refresh=%" PRIu64 " gate=%" PRIu64
           " partial=%" PRIu64 ") spi(wait=%" PRIu32
           " timeout=%" PRIu32 " qerr=%" PRIu32 " in_flight=%d)",
           light_name, frames, led_fps_text, avg_flush_dt_us, avg_show_queue_us,
           avg_write_us, avg_flush_us, avg_wait_us, avg_pack_us,
           avg_queue_us, this->perf_diag_max_spi_flush_interval_us_,
           this->perf_diag_max_queue_us_, this->perf_diag_max_write_us_,
           this->perf_diag_max_flush_us_, this->perf_diag_max_wait_us_,
           this->perf_diag_max_spi_pack_us_,
           this->perf_diag_max_spi_queue_us_,
           avg_show_req_dt_us, this->perf_diag_max_show_request_interval_us_,
           avg_guard_us, this->perf_diag_max_dma_guard_wait_us_,
           this->perf_diag_total_dma_guard_hits_,
           this->perf_diag_total_dma_guard_timeouts_,
           this->perf_diag_total_refresh_defers_,
           this->perf_diag_total_gate_defers_,
           this->perf_diag_total_partial_flushes_, this->spi_wait_count_,
           this->spi_wait_timeout_count_, this->spi_queue_error_count_,
           this->spi_tx_in_flight_);
  this->reset_perf_diag_();
}

void CFXLightOutput::log_rmt_cadence_diag_(bool force) {
  if (this->is_spi_transport()) {
    return;
  }
  if (!force && !runtime_debug_enabled_for_output(this)) {
    return;
  }

  if (this->perf_diag_flush_count_ == 0) {
    return;
  }
  if (!force) {
    const uint32_t now_ms = esphome::millis();
    if (this->perf_diag_last_log_ms_ == 0) {
      this->perf_diag_last_log_ms_ = now_ms;
      return;
    }
    if ((now_ms - this->perf_diag_last_log_ms_) < 2000) {
      return;
    }
    this->perf_diag_last_log_ms_ = now_ms;
  }

  const char *light_name =
      (this->state_parent_ != nullptr) ? this->state_parent_->get_name().c_str()
                                       : "<rmt>";
  const uint32_t frames = this->perf_diag_flush_count_;
  const uint32_t safe_frames = frames > 0 ? frames : 1;
  const uint32_t avg_write_us =
      static_cast<uint32_t>(this->perf_diag_total_write_us_ / safe_frames);
  const uint32_t avg_flush_us =
      static_cast<uint32_t>(this->perf_diag_total_flush_us_ / safe_frames);
  const uint32_t avg_wait_us =
      static_cast<uint32_t>(this->perf_diag_total_wait_us_ / safe_frames);
  const uint32_t avg_show_queue_us =
      static_cast<uint32_t>(this->perf_diag_total_queue_us_ / safe_frames);
  const uint32_t show_req_dt_count =
      this->perf_diag_show_request_interval_count_ > 0
          ? this->perf_diag_show_request_interval_count_
          : 1;
  const uint32_t avg_show_req_dt_us = static_cast<uint32_t>(
      this->perf_diag_total_show_request_interval_us_ / show_req_dt_count);
  const uint64_t guard_hits = this->perf_diag_total_dma_guard_hits_;
  const uint32_t avg_guard_us =
      guard_hits > 0
          ? static_cast<uint32_t>(this->perf_diag_total_dma_guard_wait_us_ /
                                  guard_hits)
          : 0;
  const uint32_t rmt_tx_dt_count =
      this->perf_diag_rmt_tx_launch_interval_count_ > 0
          ? this->perf_diag_rmt_tx_launch_interval_count_
          : 1;
  const uint32_t avg_rmt_tx_dt_us = static_cast<uint32_t>(
      this->perf_diag_total_rmt_tx_launch_interval_us_ / rmt_tx_dt_count);
  const uint32_t wire_floor_us = this->get_rmt_wire_frame_floor_us();
  const uint32_t effective_interval_ms =
      this->perf_diag_last_effective_rmt_update_ms_ != 0
          ? this->perf_diag_last_effective_rmt_update_ms_
          : this->get_effective_rmt_update_interval_ms(0);
  char led_fps_text[16];
  cfx::FrameDiagnostics::format_led_fps(this->get_led_fps(), led_fps_text,
                                        sizeof(led_fps_text));

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  const char *encoder_label = "bytes+reset";
#else
  const char *encoder_label = "copy";
#endif

  ESP_LOGV(TAG,
           "CFX rmt_cad[%s] frames=%" PRIu32
           " LedFPS=%s avg_us(show_q=%" PRIu32 " write=%" PRIu32
           " flush=%" PRIu32 " wait=%" PRIu32 ")"
           " max_us(show_q=%" PRIu32 " write=%" PRIu32
           " flush=%" PRIu32 " wait=%" PRIu32 ")"
           " req_us(avg=%" PRIu32 " max=%" PRIu32 ")"
           " guard(avg=%" PRIu32 " max=%" PRIu32
           " hits=%" PRIu64 " timeout=%" PRIu64 ")"
           " rmt(enc=%s symbols=%" PRIu32 " mem=%" PRIu32
           " leds=%" PRIu32 " stride=%u wire_floor=%" PRIu32
           " eff_ms=%" PRIu32
           " tx=%" PRIu64 " launch_us(avg=%" PRIu32 " max=%" PRIu32
           ") coalesce=%" PRIu64 " wait=%" PRIu32
           " timeout=%" PRIu32 " in_flight=%d)",
           light_name, frames, led_fps_text, avg_show_queue_us, avg_write_us,
           avg_flush_us, avg_wait_us, this->perf_diag_max_queue_us_,
           this->perf_diag_max_write_us_, this->perf_diag_max_flush_us_,
           this->perf_diag_max_wait_us_, avg_show_req_dt_us,
           this->perf_diag_max_show_request_interval_us_,
           avg_guard_us, this->perf_diag_max_dma_guard_wait_us_,
           this->perf_diag_total_dma_guard_hits_,
           this->perf_diag_total_dma_guard_timeouts_,
           encoder_label, this->rmt_symbols_, this->rmt_mem_block_symbols_,
           this->get_rmt_physical_led_count_(),
           static_cast<unsigned>(this->get_pixel_stride_()), wire_floor_us,
           effective_interval_ms,
           this->perf_diag_total_rmt_tx_launches_,
           avg_rmt_tx_dt_us, this->perf_diag_max_rmt_tx_launch_interval_us_,
           this->perf_diag_total_rmt_coalesced_flushes_, this->rmt_wait_count_,
           this->rmt_wait_timeout_count_, this->rmt_tx_in_flight_);
  this->reset_perf_diag_();
}

void CFXLightOutput::reset_rmt_encoder_diag_() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  this->params_.diag_callback_count = 0;
  this->params_.diag_starve_count = 0;
  this->params_.diag_reset_starve_count = 0;
  this->params_.diag_min_symbols_free = UINT32_MAX;
#endif
}

void CFXLightOutput::harvest_rmt_encoder_diag_() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  const uint32_t callback_count = this->params_.diag_callback_count;
  const uint32_t starve_count = this->params_.diag_starve_count;
  const uint32_t reset_starve_count = this->params_.diag_reset_starve_count;
  const uint32_t min_symbols_free = this->params_.diag_min_symbols_free;

  this->perf_diag_total_rmt_callback_count_ += callback_count;
  this->perf_diag_total_rmt_starve_count_ += starve_count;
  this->perf_diag_total_rmt_reset_starve_count_ += reset_starve_count;

  if (starve_count > this->perf_diag_max_rmt_starve_count_) {
    this->perf_diag_max_rmt_starve_count_ = starve_count;
  }
  if (reset_starve_count > this->perf_diag_max_rmt_reset_starve_count_) {
    this->perf_diag_max_rmt_reset_starve_count_ = reset_starve_count;
  }
  if (min_symbols_free < this->perf_diag_min_rmt_symbols_free_) {
    this->perf_diag_min_rmt_symbols_free_ = min_symbols_free;
  }
#endif
}

void CFXLightOutput::log_segment_coordinator_diag_() {
  if (this->segment_light_states_.empty()) {
    return;
  }
  if (!runtime_debug_enabled_for_output(this)) {
    return;
  }
  const uint32_t now_ms = esphome::millis();
  if (this->seg_batch_diag_last_log_ms_ == 0) {
    this->seg_batch_diag_last_log_ms_ = now_ms;
    return;
  }
  if ((now_ms - this->seg_batch_diag_last_log_ms_) < 2000) {
    return;
  }
  if (this->seg_partial_frame_suppressed_ == 0 &&
      this->seg_clean_epoch_suppressed_ == 0 &&
      this->seg_coord_apply_skips_ == 0 &&
      this->seg_coord_write_skips_ == 0 &&
      this->seg_coord_epochs_ == 0 &&
      this->seg_coord_epoch_dt_count_ == 0 &&
      this->seg_coord_collect_flush_count_ == 0 &&
      this->seg_coord_refresh_dt_count_ == 0) {
    this->seg_batch_diag_last_log_ms_ = now_ms;
    return;
  }

  uint8_t active_mask = 0;
  for (size_t i = 0; i < this->segment_light_states_.size() &&
                     i < MAX_CFX_SEGMENTS; i++) {
    auto *state = this->segment_light_states_[i];
    if (state != nullptr && state->remote_values.is_on()) {
      active_mask |= static_cast<uint8_t>(1u << i);
    }
  }

  const char *light_name =
      (this->state_parent_ != nullptr) ? this->state_parent_->get_name().c_str()
                                       : "<seg>";
  const uint32_t avg_segments =
      this->seg_coord_epochs_ > 0
          ? this->seg_coord_rendered_segments_ / this->seg_coord_epochs_
          : 0;
  int32_t due_in_ms = 0;
  if (this->segment_coord_next_due_ms_ != 0) {
    due_in_ms =
        static_cast<int32_t>(this->segment_coord_next_due_ms_) -
        static_cast<int32_t>(now_ms);
  }
  const uint32_t epoch_dt_count =
      this->seg_coord_epoch_dt_count_ > 0 ? this->seg_coord_epoch_dt_count_ : 1;
  const uint32_t avg_epoch_dt_us = static_cast<uint32_t>(
      this->seg_coord_total_epoch_dt_us_ / epoch_dt_count);
  const uint32_t due_late_count =
      this->seg_coord_due_late_count_ > 0 ? this->seg_coord_due_late_count_ : 1;
  const uint32_t avg_due_late_ms = static_cast<uint32_t>(
      this->seg_coord_total_due_late_ms_ / due_late_count);
  const uint32_t collect_flush_count =
      this->seg_coord_collect_flush_count_ > 0
          ? this->seg_coord_collect_flush_count_
          : 1;
  const uint32_t avg_collect_flush_us = static_cast<uint32_t>(
      this->seg_coord_total_collect_flush_us_ / collect_flush_count);
  const uint32_t refresh_dt_count =
      this->seg_coord_refresh_dt_count_ > 0 ? this->seg_coord_refresh_dt_count_
                                            : 1;
  const uint32_t avg_refresh_dt_us = static_cast<uint32_t>(
      this->seg_coord_total_refresh_dt_us_ / refresh_dt_count);

  ESP_LOGD(TAG,
           "CFX seg_coord[%s] active=0x%02x owned=0x%02x dormant=0x%02x "
           "idle=0x%02x last=0x%02x/%u epochs=%" PRIu32
           " segs=%" PRIu32 " avg=%" PRIu32
           " partial=%" PRIu32 " missed=%" PRIu32
           " max_missing=%" PRIu32 " clean=%" PRIu32
           " apply_skip=%" PRIu32 " write_skip=%" PRIu32
           " max_contrib=%" PRIu32 " refresh_defers=%" PRIu64
           " epoch_us=%" PRIu32 "/%" PRIu32
           " late_ms=%" PRIu32 "/%" PRIu32
           " collect_flush_us=%" PRIu32 "/%" PRIu32
           " refresh_us=%" PRIu32 "/%" PRIu32
           " next_due=%" PRId32 "ms heap=%ukB",
           light_name, active_mask, this->segment_coord_owned_mask_,
           this->segment_coord_dormant_mask_,
           this->segment_mono_idle_dormant_mask_, this->seg_last_flush_mask_,
           this->seg_last_flush_count_, this->seg_coord_epochs_,
           this->seg_coord_rendered_segments_, avg_segments,
           this->seg_partial_frame_suppressed_, this->seg_missed_epoch_count_,
           this->perf_diag_max_partial_missing_,
           this->seg_clean_epoch_suppressed_, this->seg_coord_apply_skips_,
           this->seg_coord_write_skips_, this->perf_diag_max_seg_contrib_,
           this->perf_diag_total_refresh_defers_, avg_epoch_dt_us,
           this->seg_coord_max_epoch_dt_us_, avg_due_late_ms,
           this->seg_coord_max_due_late_ms_, avg_collect_flush_us,
           this->seg_coord_max_collect_flush_us_, avg_refresh_dt_us,
           this->seg_coord_max_refresh_dt_us_, due_in_ms,
           esp_get_free_heap_size() / 1024);

  this->seg_partial_frame_suppressed_ = 0;
  this->seg_missed_epoch_count_ = 0;
  this->seg_clean_epoch_suppressed_ = 0;
  this->seg_coord_apply_skips_ = 0;
  this->seg_coord_write_skips_ = 0;
  this->seg_coord_epochs_ = 0;
  this->seg_coord_rendered_segments_ = 0;
  this->seg_coord_epoch_dt_count_ = 0;
  this->seg_coord_max_epoch_dt_us_ = 0;
  this->seg_coord_total_epoch_dt_us_ = 0;
  this->seg_coord_due_late_count_ = 0;
  this->seg_coord_max_due_late_ms_ = 0;
  this->seg_coord_total_due_late_ms_ = 0;
  this->seg_coord_collect_flush_count_ = 0;
  this->seg_coord_max_collect_flush_us_ = 0;
  this->seg_coord_total_collect_flush_us_ = 0;
  this->seg_coord_refresh_dt_count_ = 0;
  this->seg_coord_max_refresh_dt_us_ = 0;
  this->seg_coord_total_refresh_dt_us_ = 0;
  this->seg_batch_diag_last_log_ms_ = now_ms;
}

void CFXLightOutput::note_show_request() {
  // Track the freshest show request. Coalesced flushes render the latest
  // completed frame, not the oldest pending request, so keeping the newest
  // timestamp gives a truer queue-age measurement and avoids stale outliers.
  const uint32_t now_us = micros();
  if (this->perf_diag_last_show_request_interval_us_ != 0) {
    const uint32_t dt_us =
        now_us - this->perf_diag_last_show_request_interval_us_;
    this->perf_diag_total_show_request_interval_us_ += dt_us;
    this->perf_diag_show_request_interval_count_++;
    if (dt_us > this->perf_diag_max_show_request_interval_us_) {
      this->perf_diag_max_show_request_interval_us_ = dt_us;
    }
  }
  this->perf_diag_last_show_request_interval_us_ = now_us;
  this->perf_diag_last_show_request_us_ = now_us;
}

void CFXLightOutput::trigger_low_ram_warning(light::LightState *state) {
  if (state == nullptr) {
    return;
  }

  constexpr uint32_t LOW_RAM_WARNING_MS = 5000U;
  uint32_t on_hash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(state)) ^
                     0xCF041u;
  uint32_t off_hash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(state)) ^
                      0xCF042u;

  auto clear_call = state->make_call();
  clear_call.set_effect("None");
  clear_call.perform();

  esphome::App.scheduler.set_timeout(this, on_hash, 0, [this, state]() {
    this->paint_low_ram_warning_(state, true);

    auto on_call = state->make_call();
    on_call.set_transition_length(0);
    on_call.set_state(true);
    on_call.set_brightness(1.0f);

    auto color_mode = resolve_low_ram_warning_mode(state);
    if (color_mode == light::ColorMode::RGB ||
        color_mode == light::ColorMode::RGB_WHITE) {
      on_call.set_color_mode(light::ColorMode::RGB);
      on_call.set_rgb(1.0f, 0.0f, 0.0f);
    } else if (color_mode == light::ColorMode::WHITE) {
      on_call.set_color_mode(light::ColorMode::WHITE);
      on_call.set_white(1.0f);
    }

    this->applying_turn_on_defaults_ = true;
    on_call.perform();
    this->applying_turn_on_defaults_ = false;
  });

  esphome::App.scheduler.set_timeout(this, off_hash, LOW_RAM_WARNING_MS, [this, state]() {
    this->paint_low_ram_warning_(state, false);

    auto off_call = state->make_call();
    off_call.set_state(false);
    off_call.set_transition_length(0);
    off_call.perform();

    this->restore_low_ram_warning_color_(state);
  });
}

void CFXLightOutput::paint_low_ram_warning_(light::LightState *state, bool on) {
  uint16_t start = 0;
  uint16_t stop = static_cast<uint16_t>(this->size());
  resolve_low_ram_warning_segment(state, this->segment_light_states_,
                                  this->segment_defs_, start, stop);

  const Color warning = on ? Color(255, 0, 0, 0) : Color::BLACK;
  for (uint16_t i = start; i < stop && i < this->size(); i++) {
    (*this)[i] = warning;
  }
  this->schedule_show();
}

void CFXLightOutput::restore_low_ram_warning_color_(light::LightState *state) {
  if (state == nullptr) {
    return;
  }

  auto call = state->make_call();
  call.set_transition_length(0);
  call.set_state(false);

  auto color_mode = resolve_low_ram_warning_mode(state);
  if (color_mode == light::ColorMode::RGB ||
      color_mode == light::ColorMode::RGB_WHITE) {
    if (this->has_white_channel()) {
      call.set_color_mode(light::ColorMode::RGB_WHITE);
      call.set_rgb(1.0f, 1.0f, 1.0f);
      call.set_white(1.0f);
    } else {
      call.set_color_mode(light::ColorMode::RGB);
      call.set_rgb(1.0f, 1.0f, 1.0f);
    }
  } else if (color_mode == light::ColorMode::WHITE) {
    call.set_color_mode(light::ColorMode::WHITE);
    call.set_white(1.0f);
  }

  call.perform();
}

// Query the RMT default clock source frequency (varies by chip variant)
static uint32_t rmt_resolution_hz() {
  uint32_t freq;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  esp_clk_tree_src_get_freq_hz((soc_module_clk_t)RMT_CLK_SRC_DEFAULT,
                               ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &freq);
#else
  freq = 80000000; // APB clock default for older ESP-IDF
#endif
  return freq;
}

static chimera_fx::CFXAddressableLightEffect *
resolve_active_cfx_effect(light::LightState *state) {
  if (state == nullptr) {
    return nullptr;
  }

  auto *output = state->get_output();
  bool virtual_segment_output = false;
  if (output != nullptr) {
    for (auto *seg_out : CFXVirtualSegmentLight::all_segments) {
      if (seg_out != output) {
        continue;
      }
      virtual_segment_output = true;
      auto *slot_effect = seg_out->get_parent()->get_parent_owned_segment_effect(state);
      if (slot_effect != nullptr) {
        return slot_effect;
      }
      break;
    }
  }

  light::LightEffect *effect = chimera_fx::LightStateProxy::get_active_effect(state);
  if (effect == nullptr) {
    return nullptr;
  }

  // If the active effect is a Stub (virtual segment), return its underlying singleton
#if defined(USE_CFX_LIGHT) && defined(USE_ESP32)
  if (virtual_segment_output) {
    auto *cfx_stub = static_cast<chimera_fx::CFXEffectStub *>(effect);
    return cfx_stub->get_singleton();
  }
#endif

  for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_effects) {
    if (inst == effect) {
      return inst;
    }
  }
  for (auto *inst : chimera_fx::CFXAddressableLightEffect::all_segment_effects) {
    if (inst == effect) {
      return inst;
    }
  }

  return nullptr;
}

static bool perf_diag_enabled_for_effect(
    chimera_fx::CFXAddressableLightEffect *effect) {
  if (effect == nullptr) {
    return false;
  }
  auto *act = effect->get_act();
  if (act == nullptr) {
    return false;
  }
  if (act->runner != nullptr && act->runner->getDebug()) {
    return true;
  }
  for (auto *runner : act->segment_runners) {
    if (runner != nullptr && runner->getDebug()) {
      return true;
    }
  }
  return false;
}

static bool runtime_debug_enabled_for_output(CFXLightOutput *output) {
  if (output == nullptr) {
    return false;
  }
  if (output->is_runtime_debug_enabled()) {
    return true;
  }
  if (chimera_fx::CFXControl::global_debug_enabled_) {
    return true;
  }
  auto *state = output->get_master_light_state();
  if (state == nullptr) {
    return false;
  }
  for (auto *control : chimera_fx::CFXControl::get_instances()) {
    if (control == nullptr || control->get_light() != state) {
      continue;
    }
    auto *debug = control->get_debug();
    return debug != nullptr && debug->has_state() && debug->state;
  }
  return false;
}

static chimera_fx::CFXAddressableLightEffect *
resolve_perf_diag_effect(CFXLightOutput *output) {
  if (output == nullptr) {
    return nullptr;
  }

  auto *effect = resolve_active_cfx_effect(output->get_master_light_state());
  if (perf_diag_enabled_for_effect(effect)) {
    return effect;
  }

  for (auto *seg_state : output->get_segment_light_states()) {
    effect = resolve_active_cfx_effect(seg_state);
    if (perf_diag_enabled_for_effect(effect)) {
      return effect;
    }
  }

  return nullptr;
}

static bool segment_participates_in_barrier(light::LightState *state) {
  if (state == nullptr) {
    return false;
  }
  if (!state->remote_values.is_on()) {
    return false;
  }
  auto *effect = resolve_active_cfx_effect(state);
  if (effect == nullptr) {
    return false;
  }
  return !effect->is_clean_mono_idle_output();
}

static bool active_cfx_effect_is_clean_mono_idle(light::LightState *state) {
  auto *effect = resolve_active_cfx_effect(state);
  return effect != nullptr && effect->is_clean_mono_idle_output();
}

static bool all_active_cfx_effects_clean_mono_idle(CFXLightOutput *output) {
  if (output == nullptr || output->has_outro()) {
    return false;
  }

  bool saw_clean_idle = false;
  auto *master_effect = resolve_active_cfx_effect(output->get_master_light_state());
  if (master_effect != nullptr) {
    if (!master_effect->is_clean_mono_idle_output()) {
      return false;
    }
    saw_clean_idle = true;
  }

  for (auto *seg_state : output->get_segment_light_states()) {
    auto *effect = resolve_active_cfx_effect(seg_state);
    if (effect == nullptr) {
      continue;
    }
    if (!effect->is_clean_mono_idle_output()) {
      return false;
    }
    saw_clean_idle = true;
  }

  return saw_clean_idle;
}

static void mark_committed_mono_idle_outputs(CFXLightOutput *output) {
  if (output == nullptr) {
    return;
  }
  auto *master_effect = resolve_active_cfx_effect(output->get_master_light_state());
  if (master_effect != nullptr && master_effect->has_dirty_mono_idle_output()) {
    master_effect->mark_mono_output_committed();
  }
  for (auto *seg_state : output->get_segment_light_states()) {
    auto *effect = resolve_active_cfx_effect(seg_state);
    if (effect != nullptr && effect->has_dirty_mono_idle_output()) {
      effect->mark_mono_output_committed();
    }
  }
}

uint8_t CFXLightOutput::collect_clean_mono_idle_segment_mask_() const {
  if (!this->has_segments() || this->has_outro()) {
    return 0;
  }

  uint8_t mask = 0;
  for (size_t i = 0; i < this->segment_light_states_.size() &&
                     i < MAX_CFX_SEGMENTS; i++) {
    auto *seg_state = this->segment_light_states_[i];
    if (seg_state == nullptr || !seg_state->remote_values.is_on()) {
      continue;
    }
    auto *effect = resolve_active_cfx_effect(seg_state);
    if (effect != nullptr && effect->is_clean_mono_idle_output()) {
      mask |= static_cast<uint8_t>(1u << i);
    }
  }
  return mask;
}

void CFXLightOutput::wake_mono_idle_light_state_(light::LightState *state) {
  if (state == nullptr) {
    return;
  }

  auto *effect = resolve_active_cfx_effect(state);
  if (effect == nullptr || !effect->is_mono_idle()) {
    return;
  }
  this->invalidate_segment_coord_schedule_();
  effect->wake_mono_idle_output();

  if (state == this->master_light_state_) {
    if (this->master_mono_idle_dormant_) {
      state->enable_loop();
      this->master_mono_idle_dormant_ = false;
      this->master_mono_idle_sleep_ms_ = 0;
      this->mono_idle_wake_count_++;
    }
    return;
  }

  for (size_t i = 0; i < this->segment_light_states_.size() &&
                     i < MAX_CFX_SEGMENTS; i++) {
    if (this->segment_light_states_[i] != state) {
      continue;
    }
    const uint8_t bit = static_cast<uint8_t>(1u << i);
    if ((this->segment_coord_dormant_mask_ & bit) != 0) {
      state->enable_loop();
      this->segment_coord_dormant_mask_ &=
          static_cast<uint8_t>(~bit);
      if ((this->segment_mono_idle_dormant_mask_ & bit) != 0) {
        this->segment_mono_idle_dormant_mask_ &=
            static_cast<uint8_t>(~bit);
        this->segment_mono_idle_sleep_ms_[i] = 0;
        this->mono_idle_wake_count_++;
      }
    }
    return;
  }
}

void CFXLightOutput::apply_mono_idle_loop_state_(uint8_t segment_idle_mask) {
  const uint32_t now_ms = esphome::millis();
  const bool master_should_sleep =
      !this->has_outro() && this->master_light_state_ != nullptr &&
      active_cfx_effect_is_clean_mono_idle(this->master_light_state_);

  if (master_should_sleep &&
      (!this->master_mono_idle_dormant_ ||
       this->master_light_state_->is_in_loop_state())) {
    const bool entering_idle = !this->master_mono_idle_dormant_;
    auto *effect = resolve_active_cfx_effect(this->master_light_state_);
    if (effect != nullptr) {
      effect->log_mono_idle_sleep(entering_idle);
    }
    if (!this->master_mono_idle_dormant_) {
      this->master_mono_idle_sleep_ms_ = esphome::millis();
      this->mono_idle_sleep_count_++;
    }
    chimera_fx::LightStateProxy::clear_pending_write(this->master_light_state_);
    this->master_light_state_->disable_loop();
    this->master_mono_idle_dormant_ = true;
  } else if (master_should_sleep && this->master_mono_idle_dormant_) {
    auto *effect = resolve_active_cfx_effect(this->master_light_state_);
    if (effect != nullptr) {
      effect->log_mono_idle_sleep();
    }
  } else if (!master_should_sleep && this->master_mono_idle_dormant_) {
    this->master_light_state_->enable_loop();
    this->master_mono_idle_dormant_ = false;
    this->master_mono_idle_sleep_ms_ = 0;
    this->mono_idle_wake_count_++;
  }

  const uint8_t previous_segment_idle_mask =
      this->segment_mono_idle_dormant_mask_;
  for (size_t i = 0; i < this->segment_light_states_.size() &&
                     i < MAX_CFX_SEGMENTS; i++) {
    auto *seg_state = this->segment_light_states_[i];
    const uint8_t bit = static_cast<uint8_t>(1u << i);
    const bool now_idle = (segment_idle_mask & bit) != 0;
    const bool was_idle = (previous_segment_idle_mask & bit) != 0;
    auto *effect = seg_state != nullptr
                       ? resolve_active_cfx_effect(seg_state)
                       : nullptr;
    if (now_idle && !was_idle) {
      if (effect != nullptr) {
        effect->log_mono_idle_sleep(true);
      }
      this->segment_mono_idle_sleep_ms_[i] = now_ms;
      this->mono_idle_sleep_count_++;
    } else if (now_idle && was_idle) {
      if (effect != nullptr) {
        effect->log_mono_idle_sleep();
      }
    } else if (!now_idle && was_idle) {
      this->segment_mono_idle_sleep_ms_[i] = 0;
      this->mono_idle_wake_count_++;
    }
  }
  this->segment_mono_idle_dormant_mask_ = segment_idle_mask;

  this->apply_segment_coordination_loop_state_(
      this->segment_coord_owned_mask_ | segment_idle_mask);
}

void CFXLightOutput::apply_segment_coordination_loop_state_(
    uint8_t owned_mask) {
  uint8_t next_dormant_mask = this->segment_coord_dormant_mask_;

  for (size_t i = 0; i < this->segment_light_states_.size() &&
                     i < MAX_CFX_SEGMENTS; i++) {
    auto *seg_state = this->segment_light_states_[i];
    if (seg_state == nullptr) {
      continue;
    }

    const uint8_t bit = static_cast<uint8_t>(1u << i);
    const bool should_sleep = (owned_mask & bit) != 0;
    const bool is_sleeping = (this->segment_coord_dormant_mask_ & bit) != 0;
    auto *effect = resolve_active_cfx_effect(seg_state);
    const bool keep_probe_awake =
        should_sleep && seg_state->is_in_loop_state() && effect != nullptr &&
        effect->has_pending_mono_idle_probe();

    if (should_sleep && (!is_sleeping || seg_state->is_in_loop_state()) &&
        !keep_probe_awake) {
      chimera_fx::LightStateProxy::clear_pending_write(seg_state);
      seg_state->disable_loop();
      next_dormant_mask |= bit;
    } else if (!should_sleep && is_sleeping) {
      seg_state->enable_loop();
      next_dormant_mask &= static_cast<uint8_t>(~bit);
    }
  }

  this->segment_coord_dormant_mask_ = next_dormant_mask;
}

void CFXLightOutput::apply_master_segment_coordination_loop_state_() {
  if (this->master_light_state_ == nullptr) {
    return;
  }

  const bool master_effect_active =
      this->master_light_state_->get_effect_name() != "None";
  const bool should_sleep =
      this->has_active_parent_owned_segments_() && !this->has_outro() &&
      !master_effect_active;

  if (should_sleep &&
      (!this->master_segment_coord_dormant_ ||
       this->master_light_state_->is_in_loop_state())) {
    chimera_fx::LightStateProxy::clear_pending_write(this->master_light_state_);
    this->master_light_state_->current_values =
        this->master_light_state_->remote_values;
    if (chimera_fx::LightStateProxy::has_active_transformer(
            this->master_light_state_)) {
      chimera_fx::LightStateProxy::stop_state_transformer(
          this->master_light_state_);
    }
    this->master_light_state_->disable_loop();
    this->master_segment_coord_dormant_ = true;
  } else if (!should_sleep && this->master_segment_coord_dormant_) {
    this->master_light_state_->enable_loop();
    this->master_segment_coord_dormant_ = false;
  }
}

int CFXLightOutput::find_segment_runtime_slot_(light::LightState *state) const {
  if (state == nullptr) {
    return -1;
  }
  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    if (this->segment_runtime_slots_[i].active &&
        this->segment_runtime_slots_[i].state == state) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void CFXLightOutput::clear_segment_runtime_slot_(size_t index) {
  if (index >= MAX_CFX_SEGMENTS) {
    return;
  }
  this->segment_runtime_slots_[index] = CFXSegmentRuntimeSlot{};
}

bool CFXLightOutput::has_active_parent_owned_segments_(bool include_outro) const {
  if (this->has_outro() && !include_outro) {
    return false;
  }
  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    const auto &slot = this->segment_runtime_slots_[i];
    if (slot.active && slot.state != nullptr && slot.effect != nullptr &&
        slot.runner != nullptr && slot.state->remote_values.is_on()) {
      return true;
    }
  }
  return false;
}

chimera_fx::CFXAddressableLightEffect *
CFXLightOutput::get_parent_owned_segment_effect(light::LightState *state) const {
  if (state == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    const auto &slot = this->segment_runtime_slots_[i];
    if (!slot.active || slot.state != state) {
      continue;
    }
    return slot.effect;
  }
  return nullptr;
}

void CFXLightOutput::refresh_parent_owned_segment_slot_(
    CFXSegmentRuntimeSlot &slot) {
  if (!slot.active || slot.state == nullptr) {
    return;
  }

  auto *state = slot.state;
  state->current_values = state->remote_values;
  if (chimera_fx::LightStateProxy::has_active_transformer(state)) {
    chimera_fx::LightStateProxy::stop_state_transformer(state);
  }

  float r = state->remote_values.get_red();
  float g = state->remote_values.get_green();
  float b = state->remote_values.get_blue();
  float w = state->remote_values.get_white();
  slot.color = (uint32_t(roundf(w * 255.0f)) << 24) |
               (uint32_t(roundf(r * 255.0f)) << 16) |
               (uint32_t(roundf(g * 255.0f)) << 8) |
               uint32_t(roundf(b * 255.0f));
  if (slot.color == 0 && state->remote_values.is_on()) {
    slot.color = 0xFFFFFFFF;
  }

  slot.gamma = state->get_gamma_correct();

  float state_bri = state->remote_values.get_brightness();
  if (state_bri == 0.0f && state->remote_values.is_on()) {
    state_bri = 1.0f;
  }
  slot.global_brightness =
      state_bri * state->remote_values.get_state();
}

void CFXLightOutput::refresh_parent_owned_segment_slots_() {
  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    auto &slot = this->segment_runtime_slots_[i];
    if (!slot.active) {
      continue;
    }
    this->refresh_parent_owned_segment_slot_(slot);
    slot.dirty = true;
  }
}

void CFXLightOutput::invalidate_segment_coord_schedule_() {
  this->segment_coord_schedule_dirty_ = true;
  this->segment_coord_next_due_ms_ = 0;
}

bool CFXLightOutput::register_parent_owned_segment(
    light::LightState *state, CFXVirtualSegmentLight *segment,
    chimera_fx::CFXAddressableLightEffect *effect, chimera_fx::CFXRunner *runner) {
  if (state == nullptr || segment == nullptr || effect == nullptr ||
      runner == nullptr || segment->get_parent() != this) {
    return false;
  }

  int slot_index = -1;
  for (size_t i = 0; i < this->segment_light_states_.size() &&
                     i < MAX_CFX_SEGMENTS; i++) {
    if (this->segment_light_states_[i] == state) {
      slot_index = static_cast<int>(i);
      break;
    }
  }
  if (slot_index < 0) {
    return false;
  }

  auto &slot = this->segment_runtime_slots_[slot_index];
  slot.state = state;
  slot.segment = segment;
  slot.effect = effect;
  slot.runner = runner;
  slot.active = true;
  slot.dirty = true;
  slot.bound = false;
  slot.fallback = false;
  slot.due_at = 0;
  this->refresh_parent_owned_segment_slot_(slot);
  this->invalidate_segment_coord_schedule_();

  chimera_fx::LightStateProxy::clear_pending_write(state);
  state->enable_loop();

  const uint8_t bit = static_cast<uint8_t>(1u << slot_index);
  this->segment_coord_dormant_mask_ &= static_cast<uint8_t>(~bit);
  this->segment_mono_idle_dormant_mask_ &= static_cast<uint8_t>(~bit);
  this->segment_mono_idle_sleep_ms_[slot_index] = 0;
  this->refresh_segment_coordination_mask_();
  return true;
}

void CFXLightOutput::unregister_parent_owned_segment(
    light::LightState *state, chimera_fx::CFXAddressableLightEffect *effect) {
  if (state == nullptr) {
    return;
  }
  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    auto &slot = this->segment_runtime_slots_[i];
    if (!slot.active || slot.state != state) {
      continue;
    }
    if (effect != nullptr && slot.effect != effect) {
      continue;
    }
    const uint8_t bit = static_cast<uint8_t>(1u << i);
    this->segment_coord_dormant_mask_ &= static_cast<uint8_t>(~bit);
    this->segment_mono_idle_dormant_mask_ &= static_cast<uint8_t>(~bit);
    this->segment_mono_idle_sleep_ms_[i] = 0;
    this->seg_request_generation_[i] = 0;
    this->seg_flushed_generation_[i] = 0;
    state->enable_loop();
    this->clear_segment_runtime_slot_(i);
    this->invalidate_segment_coord_schedule_();
  }
  this->refresh_segment_coordination_mask_();
}

void CFXLightOutput::refresh_segment_coordination_mask_(bool include_outro) {
  uint8_t mask = 0;
  if (this->has_segments() && (!this->has_outro() || include_outro)) {
    for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
      const auto &slot = this->segment_runtime_slots_[i];
      if (!slot.active || slot.state == nullptr || slot.effect == nullptr ||
          slot.runner == nullptr || slot.segment == nullptr) {
        continue;
      }
      if (!slot.state->remote_values.is_on() ||
          !slot.effect->can_parent_coordinate_segment() ||
          slot.effect->is_clean_mono_idle_output()) {
        continue;
      }
      mask |= static_cast<uint8_t>(1u << i);
    }
  }
  this->segment_coord_owned_mask_ = mask;
  this->segment_coord_owned_mask_ms_ = esphome::millis();
  if (mask == 0) {
    this->segment_coord_next_due_ms_ = 0;
  }
}

bool CFXLightOutput::segment_coordinator_owns(light::LightState *state) {
  if (this->segment_coord_schedule_dirty_) {
    this->refresh_segment_coordination_mask_(this->has_outro());
  }
  const int slot_index = this->find_segment_runtime_slot_(state);
  return slot_index >= 0 &&
         (this->segment_coord_owned_mask_ &
          static_cast<uint8_t>(1u << slot_index)) != 0;
}

void CFXLightOutput::note_segment_coord_apply_skip() {
  this->seg_coord_apply_skips_++;
}

void CFXLightOutput::note_segment_coord_write_skip() {
  this->seg_coord_write_skips_++;
}

void CFXLightOutput::mark_parent_owned_segment_dirty(light::LightState *state) {
  if (state == nullptr) {
    return;
  }
  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    auto &slot = this->segment_runtime_slots_[i];
    if (!slot.active || slot.state != state) {
      continue;
    }
    slot.dirty = true;
    this->invalidate_segment_coord_schedule_();
    return;
  }
}

bool CFXLightOutput::collect_segment_coordinator_epoch_(uint8_t &mask,
                                                        uint8_t &count,
                                                        uint64_t now,
                                                        bool force_due,
                                                        bool allow_outro) {
  mask = 0;
  count = 0;
  const uint64_t scheduled_due = this->segment_coord_next_due_ms_;
  if (!this->has_segments() || (this->has_outro() && !allow_outro)) {
    this->apply_segment_coordination_loop_state_(0);
    this->apply_master_segment_coordination_loop_state_();
    this->apply_mono_idle_loop_state_(0);
    return false;
  }

  if (!force_due && !this->segment_coord_schedule_dirty_ &&
      this->segment_coord_owned_mask_ != 0 &&
      this->segment_coord_next_due_ms_ != 0 &&
      now < this->segment_coord_next_due_ms_) {
    return false;
  }
  uint64_t next_due = 0;
  this->refresh_segment_coordination_mask_(allow_outro);
  const uint8_t segment_idle_mask =
      allow_outro ? 0 : this->collect_clean_mono_idle_segment_mask_();
  this->apply_mono_idle_loop_state_(segment_idle_mask);
  this->apply_master_segment_coordination_loop_state_();
  if (this->segment_coord_owned_mask_ == 0) {
    this->segment_coord_schedule_dirty_ = false;
    this->segment_coord_next_due_ms_ = 0;
    return false;
  }

  if (this->segment_coord_runners_.capacity() < MAX_CFX_SEGMENTS) {
    this->segment_coord_runners_.reserve(MAX_CFX_SEGMENTS);
  }
  this->segment_coord_runners_.clear();

  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    auto &slot = this->segment_runtime_slots_[i];
    if (!slot.active || slot.state == nullptr || slot.effect == nullptr ||
        slot.runner == nullptr || slot.segment == nullptr) {
      continue;
    }
    if ((this->segment_coord_owned_mask_ & static_cast<uint8_t>(1u << i)) == 0) {
      continue;
    }
    const uint32_t interval = slot.effect->get_effective_update_interval();
    if (slot.due_at == 0) {
      slot.due_at = now;
    }
    if (!force_due && now < slot.due_at) {
      if (next_due == 0 || slot.due_at < next_due) {
        next_due = slot.due_at;
      }
      continue;
    }
    if (!slot.bound) {
      slot.effect->prepare_parent_coordinated_runner(*this);
      slot.bound = true;
    }
    if (slot.dirty) {
      slot.effect->sync_parent_owned_inputs(slot.color, slot.gamma,
                                            slot.global_brightness);
      slot.dirty = false;
    }
    slot.effect->mark_parent_coordinated_run(now);
    if (interval == 0) {
      slot.due_at = now;
    } else {
      const uint64_t late_ms = now > slot.due_at ? now - slot.due_at : 0;
      const uint64_t reset_threshold = static_cast<uint64_t>(interval) * 4ULL;
      slot.due_at =
          late_ms > reset_threshold ? (now + interval) : (slot.due_at + interval);
    }
    if (next_due == 0 || slot.due_at < next_due) {
      next_due = slot.due_at;
    }
    this->segment_coord_runners_.push_back(slot.runner);
    mask |= static_cast<uint8_t>(1u << i);
    count++;
  }
  this->segment_coord_schedule_dirty_ = false;
  this->segment_coord_next_due_ms_ = next_due;

  if (count == 0) {
    return false;
  }

  const uint32_t epoch_us = micros();
  if (this->seg_coord_last_epoch_us_ != 0) {
    const uint32_t epoch_dt_us = epoch_us - this->seg_coord_last_epoch_us_;
    this->seg_coord_total_epoch_dt_us_ += epoch_dt_us;
    this->seg_coord_epoch_dt_count_++;
    if (epoch_dt_us > this->seg_coord_max_epoch_dt_us_) {
      this->seg_coord_max_epoch_dt_us_ = epoch_dt_us;
    }
  }
  this->seg_coord_last_epoch_us_ = epoch_us;
  const uint32_t due_late_ms =
      scheduled_due != 0 && now > scheduled_due
          ? static_cast<uint32_t>(now - scheduled_due)
          : 0;
  this->seg_coord_total_due_late_ms_ += due_late_ms;
  this->seg_coord_due_late_count_++;
  if (due_late_ms > this->seg_coord_max_due_late_ms_) {
    this->seg_coord_max_due_late_ms_ = due_late_ms;
  }

  return true;
}

bool CFXLightOutput::render_segment_coordinator_epoch_(uint8_t &mask,
                                                       uint8_t &count,
                                                       bool force_due,
                                                       bool allow_outro) {
  const uint64_t now = static_cast<uint64_t>(esphome::millis());
  this->seg_coord_collect_start_us_ = micros();
  if (!this->collect_segment_coordinator_epoch_(mask, count, now, force_due,
                                               allow_outro)) {
    this->seg_coord_collect_start_us_ = 0;
    return false;
  }

  // P1: pass the transport constraint directly to the scheduler batch - no
  // global state mutation. SPI batches run sequentially on Core 1 (SPI driver
  // is not safe across cores); RMT batches keep the dual-core split.
  const bool complete =
      chimera_fx::CFXScheduler::get().service_runners(
          this->segment_coord_runners_, this->is_spi_transport());
  esphome::App.feed_wdt();

  if (!complete) {
    this->seg_coord_collect_start_us_ = 0;
    this->perf_diag_total_partial_flushes_++;
    this->seg_partial_frame_suppressed_++;
    this->seg_missed_epoch_count_ += count;
    if (count > this->perf_diag_max_partial_missing_) {
      this->perf_diag_max_partial_missing_ = count;
    }
    this->log_segment_coordinator_diag_();
    mask = 0;
    count = 0;
    return true;
  }

  for (auto *runner : this->segment_coord_runners_) {
    if (runner != nullptr) {
      runner->diagnostics.flush_log(this->get_led_fps());
    }
  }
  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    if ((mask & static_cast<uint8_t>(1u << i)) == 0) {
      continue;
    }
    auto &slot = this->segment_runtime_slots_[i];
    if (slot.effect != nullptr) {
      slot.effect->process_parent_coordinated_runner_events();
    }
  }
  this->seg_coord_epochs_++;
  this->seg_coord_rendered_segments_ += count;
  return true;
}

void CFXLightOutput::mark_segment_coordinator_epoch_committed_(uint8_t mask) {
  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    if ((mask & static_cast<uint8_t>(1u << i)) == 0) {
      continue;
    }
    const auto &slot = this->segment_runtime_slots_[i];
    if (slot.effect != nullptr && slot.effect->has_dirty_mono_idle_output()) {
      slot.effect->mark_mono_output_committed();
    }
  }
}

void CFXLightOutput::finalize_segment_coordinator_epoch_(uint8_t mask,
                                                         uint8_t count,
                                                         bool transmit) {
  if (mask == 0 || count == 0) {
    return;
  }
  this->seg_generation_counter_++;
  if (this->seg_generation_counter_ == 0) {
    this->seg_generation_counter_ = 1;
  }
  for (size_t i = 0; i < this->segment_light_states_.size() &&
                     i < MAX_CFX_SEGMENTS; i++) {
    if ((mask & static_cast<uint8_t>(1u << i)) == 0) {
      continue;
    }
    this->seg_request_generation_[i] = this->seg_generation_counter_;
    this->seg_flushed_generation_[i] = this->seg_generation_counter_;
  }
  this->seg_flush_pending_mask_ = 0;
  this->seg_flush_dirty_mask_ = 0;
  this->seg_flush_pending_ = false;
  this->seg_flush_first_ms_ = 0;
  this->seg_last_flush_mask_ = mask;
  this->seg_last_flush_count_ = count;
  if (transmit) {
    this->flush_parent_owned_segment_epoch_direct_(mask, count);
  }
}

bool CFXLightOutput::service_parallel_segment_group_coordinator_() {
  auto &g_parallel_group = *parallel_group_for_output_(this);
  if (!this->is_parallel_transport() || !g_parallel_group.configured ||
      g_parallel_group.lane_count <= 1) {
    return false;
  }

  uint8_t lane_masks[PARALLEL_MAX_LANES] = {};
  uint8_t lane_counts[PARALLEL_MAX_LANES] = {};
  bool group_due = false;
  const uint64_t now = static_cast<uint64_t>(esphome::millis());

  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *output = g_parallel_group.outputs[lane];
    if (output == nullptr || !output->has_segments() || output->has_outro()) {
      continue;
    }
    output->refresh_segment_coordination_mask_();
    if (output->segment_coord_owned_mask_ == 0) {
      continue;
    }
    if (output->segment_coord_schedule_dirty_ ||
        output->segment_coord_next_due_ms_ == 0 ||
        now >= output->segment_coord_next_due_ms_) {
      group_due = true;
      break;
    }
  }

  if (!group_due) {
    // All lanes are idle (segment_coord_owned_mask_ == 0 everywhere).
    // Still service the idle state so log_mono_idle_sleep() fires correctly,
    // mirroring what render_segment_coordinator_epoch_() does at the non-parallel
    // early-exit path (line ~1675).
    for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
      auto *output = g_parallel_group.outputs[lane];
      if (output == nullptr || !output->has_segments() || output->has_outro()) {
        continue;
      }
      const uint8_t segment_idle_mask =
          output->collect_clean_mono_idle_segment_mask_();
      output->apply_mono_idle_loop_state_(segment_idle_mask);
    }
    return false;
  }

  if (this->parallel_segment_coord_runners_.capacity() <
      PARALLEL_MAX_LANES * MAX_CFX_SEGMENTS) {
    this->parallel_segment_coord_runners_.reserve(
        PARALLEL_MAX_LANES * MAX_CFX_SEGMENTS);
  }
  this->parallel_segment_coord_runners_.clear();
  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *output = g_parallel_group.outputs[lane];
    if (output == nullptr || !output->has_segments() || output->has_outro()) {
      continue;
    }
    uint8_t mask = 0;
    uint8_t count = 0;
    output->seg_coord_collect_start_us_ = micros();
    if (output->collect_segment_coordinator_epoch_(mask, count, now, false)) {
      lane_masks[lane] = mask;
      lane_counts[lane] = count;
      this->parallel_segment_coord_runners_.insert(
          this->parallel_segment_coord_runners_.end(),
          output->segment_coord_runners_.begin(),
          output->segment_coord_runners_.end());
    } else {
      output->seg_coord_collect_start_us_ = 0;
    }
  }

  if (this->parallel_segment_coord_runners_.empty()) {
    return false;
  }

  const bool complete =
      chimera_fx::CFXScheduler::get().service_runners(
          this->parallel_segment_coord_runners_, false);
  esphome::App.feed_wdt();

  if (!complete) {
    for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
      auto *output = g_parallel_group.outputs[lane];
      if (output == nullptr || lane_counts[lane] == 0) {
        continue;
      }
      output->seg_coord_collect_start_us_ = 0;
      output->perf_diag_total_partial_flushes_++;
      output->seg_partial_frame_suppressed_++;
      output->seg_missed_epoch_count_ += lane_counts[lane];
      if (lane_counts[lane] > output->perf_diag_max_partial_missing_) {
        output->perf_diag_max_partial_missing_ = lane_counts[lane];
      }
      output->log_segment_coordinator_diag_();
    }
    return true;
  }

  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *output = g_parallel_group.outputs[lane];
    if (output == nullptr || lane_counts[lane] == 0) {
      continue;
    }
    for (auto *runner : output->segment_coord_runners_) {
      if (runner != nullptr) {
        runner->diagnostics.flush_log(output->get_led_fps());
      }
    }
    for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
      if ((lane_masks[lane] & static_cast<uint8_t>(1u << i)) == 0) {
        continue;
      }
      auto &slot = output->segment_runtime_slots_[i];
      if (slot.effect != nullptr) {
        slot.effect->process_parent_coordinated_runner_events();
      }
    }
    output->seg_coord_epochs_++;
    output->seg_coord_rendered_segments_ += lane_counts[lane];
    output->finalize_segment_coordinator_epoch_(lane_masks[lane],
                                                lane_counts[lane], false);
  }

  uint8_t segment_ready_mask = 0;
  const uint32_t launch_us = micros();
  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *output = g_parallel_group.outputs[lane];
    if (output == nullptr || !output->has_active_parent_owned_segments_()) {
      continue;
    }
    segment_ready_mask |= static_cast<uint8_t>(1u << lane);
    if (lane_masks[lane] == 0) {
      continue;
    }
    output->last_refresh_ = launch_us;
    output->mark_shown_();
  }

  if (segment_ready_mask != 0) {
    g_parallel_group.pending_mask |= segment_ready_mask;
    if (g_parallel_group.pending_first_ms == 0) {
      g_parallel_group.pending_first_ms = esphome::millis();
    }
  }

  uint8_t active_lanes_mask = 0;
  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    if (has_active_rendering_cfx_effect(g_parallel_group.outputs[lane])) {
      active_lanes_mask |= static_cast<uint8_t>(1u << lane);
    }
  }

  if (active_lanes_mask == 0 ||
      (g_parallel_group.pending_mask & active_lanes_mask) ==
          active_lanes_mask) {
    g_parallel_group.pending_mask = 0;
    g_parallel_group.pending_first_ms = 0;
    g_parallel_group.coalesced_count++;
    this->status_clear_warning();
    bool shared_deferred = false;
#if !defined(CONFIG_IDF_TARGET_ESP32) && defined(CFX_PARALLEL_I80_ENABLED)
    if (parallel_shared_segment_group_mode_enabled_()) {
      shared_deferred = !this->request_parallel_shared_group_flush_();
    }
#endif
    if (!shared_deferred) {
      this->flush_parallel_();
    }
  }

  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *output = g_parallel_group.outputs[lane];
    if (output == nullptr || lane_masks[lane] == 0) {
      continue;
    }
    output->mark_segment_coordinator_epoch_committed_(lane_masks[lane]);
    output->log_segment_coordinator_diag_();
  }
  return true;
}

bool CFXLightOutput::service_segment_render_coordinator_() {
  auto &g_parallel_group = *parallel_group_for_output_(this);
  if (this->is_parallel_transport() && g_parallel_group.lane_count > 1 &&
      this->has_active_parent_owned_segments_()) {
    return this->service_parallel_segment_group_coordinator_();
  }

  uint8_t mask = 0;
  uint8_t count = 0;
  if (!this->render_segment_coordinator_epoch_(mask, count, false)) {
    return false;
  }
  this->flush_segment_coordinator_epoch_(mask, count);
  return true;
}

void CFXLightOutput::flush_segment_coordinator_epoch_(uint8_t mask,
                                                     uint8_t count) {
  this->finalize_segment_coordinator_epoch_(mask, count, true);
}

void CFXLightOutput::flush_parent_owned_segment_epoch_direct_(uint8_t mask,
                                                              uint8_t count) {
  if (mask == 0 || count == 0) {
    return;
  }

  if (!this->outro_cbs_.empty()) {
    return;
  }

  // Keep OFF segments scrubbed without routing the whole epoch back through the
  // generic LightState/effect bookkeeping path.
  if (!this->segment_light_states_.empty()) {
    esphome::App.feed_wdt();
    for (size_t i = 0; i < this->segment_light_states_.size(); i++) {
      auto *seg_state = this->segment_light_states_[i];
      if (seg_state == nullptr || seg_state->remote_values.is_on()) {
        continue;
      }
      const auto &def = this->segment_defs_[i];
      for (int p = def.start; p < def.stop; p++) {
        if (p < this->size()) {
          (*this)[p] = esphome::Color::BLACK;
        }
      }
    }
  }

  this->status_clear_warning();

  uint32_t now = micros();
  if (*this->max_refresh_rate_ != 0 &&
      (now - this->last_refresh_) < *this->max_refresh_rate_) {
    this->perf_diag_total_refresh_defers_++;
    if (this->perf_diag_total_refresh_defers_ > this->perf_diag_max_refresh_defers_) {
      this->perf_diag_max_refresh_defers_ =
          static_cast<uint32_t>(this->perf_diag_total_refresh_defers_);
    }
    this->seg_coord_collect_start_us_ = 0;
    this->schedule_show();
    return;
  }

  if (this->seg_coord_collect_start_us_ != 0) {
    const uint32_t collect_flush_us = now - this->seg_coord_collect_start_us_;
    this->seg_coord_total_collect_flush_us_ += collect_flush_us;
    this->seg_coord_collect_flush_count_++;
    if (collect_flush_us > this->seg_coord_max_collect_flush_us_) {
      this->seg_coord_max_collect_flush_us_ = collect_flush_us;
    }
    this->seg_coord_collect_start_us_ = 0;
  }
  if (this->last_refresh_ != 0) {
    const uint32_t refresh_dt_us = now - this->last_refresh_;
    this->seg_coord_total_refresh_dt_us_ += refresh_dt_us;
    this->seg_coord_refresh_dt_count_++;
    if (refresh_dt_us > this->seg_coord_max_refresh_dt_us_) {
      this->seg_coord_max_refresh_dt_us_ = refresh_dt_us;
    }
  }
  this->last_refresh_ = now;
  this->mark_shown_();

  if (this->transport_ == TRANSPORT_SPI) {
    esphome::App.feed_wdt();
    this->flush_spi_();
  } else if (this->transport_ == TRANSPORT_PARALLEL) {
    esphome::App.feed_wdt();
    this->flush_parallel_();
  } else {
    const uint32_t launch_us = micros();
    g_last_rmt_launch_us = launch_us;
    this->perf_diag_last_launch_slot_ =
        static_cast<uint8_t>(g_rmt_launch_seq & 0x3);
    g_rmt_launch_seq++;
    this->flush_rmt_();
  }

  for (size_t i = 0; i < MAX_CFX_SEGMENTS; i++) {
    if ((mask & static_cast<uint8_t>(1u << i)) == 0) {
      continue;
    }
    const auto &slot = this->segment_runtime_slots_[i];
    if (slot.effect != nullptr && slot.effect->has_dirty_mono_idle_output()) {
      slot.effect->mark_mono_output_committed();
    }
  }
  this->log_segment_coordinator_diag_();
  this->seg_last_flush_count_ = 0;
  this->seg_last_flush_mask_ = 0;
}

// --- Core Control Loop & Initialization ---

// CFX-025: Destructor closes the visualizer UDP socket if it was opened.
// ESP32 has a small FD pool (~5 sockets under default ESP-IDF config). Without
// this, each OTA cycle or component teardown leaks one FD, eventually causing
// all subsequent socket() calls to fail. close() is available via
// lwip/sockets.h.
CFXLightOutput::~CFXLightOutput() {
  this->high_freq_loop_requester_.stop();
  if (this->socket_fd_ >= 0) {
    ::close(this->socket_fd_);
    this->socket_fd_ = -1;
  }
  // RMT teardown: drain any in-flight DMA before releasing the channel.
  if (this->rmt_tx_in_flight_ && this->channel_ != nullptr) {
    this->wait_for_rmt_tx_(50, "destructor");
  }
  // SPI teardown
  if (this->spi_tx_in_flight_ && this->spi_device_ != nullptr) {
    this->wait_for_spi_tx_(50, "destructor");
  }
  if (this->spi_device_ != nullptr) {
    spi_bus_remove_device(this->spi_device_);
    this->spi_device_ = nullptr;
  }
  if (this->transport_ == TRANSPORT_SPI) {
    spi_bus_free(resolve_spi_host_(this->spi_host_));
  }
  if (this->spi_frame_buf_ != nullptr) {
    free(this->spi_frame_buf_);
    this->spi_frame_buf_ = nullptr;
  }
}

void CFXLightOutput::on_shutdown() {
  if (this->transport_ == TRANSPORT_PARALLEL) {
    this->force_parallel_shutdown_blackout_();
  }
  this->high_freq_loop_requester_.stop();
}

// --- Timing Configuration ---

void CFXLightOutput::configure_timing_() {
  float ratio = (float)rmt_resolution_hz() / 1e09f;

  // All timings in nanoseconds, converted to RMT ticks via ratio
  uint32_t t0h_ns, t0l_ns, t1h_ns, t1l_ns, reset_ns;

  switch (this->chipset_) {
  case CHIPSET_SK6812:
    // SK6812 strict timing — prevents white channel bleeding
    t0h_ns = 300;
    t0l_ns = 900;
    t1h_ns = 600;
    t1l_ns = 600;
    reset_ns = 80000;
    break;
  case CHIPSET_WS2811:
    t0h_ns = 500;
    t0l_ns = 2000;
    t1h_ns = 1200;
    t1l_ns = 1300;
    reset_ns = 280000;
    break;
  case CHIPSET_WS2812X:
  default:
    // WS2812B/C/WS2813 compatible timings
    t0h_ns = 400;
    t0l_ns = 850;
    t1h_ns = 800;
    t1l_ns = 450;
    reset_ns = 280000;
    break;
  }

  this->params_.bit0.duration0 = (uint32_t)(ratio * t0h_ns);
  this->params_.bit0.level0 = 1;
  this->params_.bit0.duration1 = (uint32_t)(ratio * t0l_ns);
  this->params_.bit0.level1 = 0;

  this->params_.bit1.duration0 = (uint32_t)(ratio * t1h_ns);
  this->params_.bit1.level0 = 1;
  this->params_.bit1.duration1 = (uint32_t)(ratio * t1l_ns);
  this->params_.bit1.level1 = 0;

  this->params_.reset.duration0 = (uint32_t)(ratio * reset_ns);
  this->params_.reset.level0 = 0;
  this->params_.reset.duration1 = 0;
  this->params_.reset.level1 = 0;
}

// --- RMT Encoder Callback (ESP-IDF >= 5.3) ---

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
static size_t IRAM_ATTR HOT encoder_callback(const void *data, size_t size,
                                             size_t symbols_written,
                                             size_t symbols_free,
                                             rmt_symbol_word_t *symbols,
                                             bool *done, void *arg) {
  auto *params = static_cast<LedParams *>(arg);
  const auto *bytes = static_cast<const uint8_t *>(data);
  size_t index = symbols_written / RMT_SYMBOLS_PER_BYTE;
  params->diag_callback_count++;
  if (symbols_free < params->diag_min_symbols_free) {
    params->diag_min_symbols_free = symbols_free;
  }

  if (index < size) {
    if (symbols_free < RMT_SYMBOLS_PER_BYTE) {
      params->diag_starve_count++;
      return 0;
    }
    size_t out = 0;
    while (index < size &&
           (symbols_free - out) >= RMT_SYMBOLS_PER_BYTE) {
      const uint8_t b = bytes[index++];
      for (size_t i = 0; i < RMT_SYMBOLS_PER_BYTE; i++) {
        symbols[out + i] = (b & (1 << (7 - i))) ? params->bit1 : params->bit0;
      }
      out += RMT_SYMBOLS_PER_BYTE;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 1)
    if (index >= size && params->reset.duration0 == 0 &&
        params->reset.duration1 == 0) {
      *done = true;
    }
#endif
    if (index >= size &&
        (params->reset.duration0 > 0 || params->reset.duration1 > 0) &&
        out < symbols_free) {
      symbols[out++] = params->reset;
      *done = true;
    }
    return out;
  }

  // Send reset pulse
  if (params->reset.duration0 == 0 && params->reset.duration1 == 0) {
    *done = true;
    return 0;
  }
  if (symbols_free < 1) {
    params->diag_reset_starve_count++;
    return 0;
  }
  symbols[0] = params->reset;
  *done = true;
  return 1;
}
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
struct CFXRMTLedEncoder {
  rmt_encoder_t base;
  rmt_encoder_t *bytes_encoder{nullptr};
  rmt_encoder_t *reset_encoder{nullptr};
  rmt_symbol_word_t reset_symbol{};
  uint8_t state{0};
};

RMT_ENCODER_FUNC_ATTR
static size_t cfx_rmt_led_encode(rmt_encoder_t *encoder,
                                 rmt_channel_handle_t channel,
                                 const void *primary_data, size_t data_size,
                                 rmt_encode_state_t *ret_state) {
  auto *led_encoder = reinterpret_cast<CFXRMTLedEncoder *>(encoder);
  rmt_encode_state_t session_state = RMT_ENCODING_RESET;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  size_t encoded_symbols = 0;

  if (led_encoder->state == 0) {
    encoded_symbols += led_encoder->bytes_encoder->encode(
        led_encoder->bytes_encoder, channel, primary_data, data_size,
        &session_state);
    if (session_state & RMT_ENCODING_COMPLETE) {
      led_encoder->state = 1;
    }
    if (session_state & RMT_ENCODING_MEM_FULL) {
      state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_MEM_FULL);
      goto out;
    }
  }

  if (led_encoder->state == 1) {
    encoded_symbols += led_encoder->reset_encoder->encode(
        led_encoder->reset_encoder, channel, &led_encoder->reset_symbol,
        sizeof(led_encoder->reset_symbol), &session_state);
    if (session_state & RMT_ENCODING_COMPLETE) {
      led_encoder->state = 0;
      state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_COMPLETE);
    }
    if (session_state & RMT_ENCODING_MEM_FULL) {
      state = static_cast<rmt_encode_state_t>(state | RMT_ENCODING_MEM_FULL);
      goto out;
    }
  }

out:
  *ret_state = state;
  return encoded_symbols;
}

static esp_err_t cfx_rmt_led_encoder_reset(rmt_encoder_t *encoder) {
  auto *led_encoder = reinterpret_cast<CFXRMTLedEncoder *>(encoder);
  rmt_encoder_reset(led_encoder->bytes_encoder);
  rmt_encoder_reset(led_encoder->reset_encoder);
  led_encoder->state = 0;
  return ESP_OK;
}

static esp_err_t cfx_rmt_led_encoder_del(rmt_encoder_t *encoder) {
  auto *led_encoder = reinterpret_cast<CFXRMTLedEncoder *>(encoder);
  if (led_encoder->bytes_encoder != nullptr) {
    rmt_del_encoder(led_encoder->bytes_encoder);
  }
  if (led_encoder->reset_encoder != nullptr) {
    rmt_del_encoder(led_encoder->reset_encoder);
  }
  free(led_encoder);
  return ESP_OK;
}

static esp_err_t cfx_rmt_new_led_encoder(const LedParams &params,
                                         rmt_encoder_handle_t *ret_encoder) {
  if (ret_encoder == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  auto *led_encoder = static_cast<CFXRMTLedEncoder *>(
      rmt_alloc_encoder_mem(sizeof(CFXRMTLedEncoder)));
  if (led_encoder == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  memset(led_encoder, 0, sizeof(CFXRMTLedEncoder));

  led_encoder->base.encode = cfx_rmt_led_encode;
  led_encoder->base.reset = cfx_rmt_led_encoder_reset;
  led_encoder->base.del = cfx_rmt_led_encoder_del;
  led_encoder->reset_symbol = params.reset;

  rmt_bytes_encoder_config_t bytes_config;
  memset(&bytes_config, 0, sizeof(bytes_config));
  bytes_config.bit0 = params.bit0;
  bytes_config.bit1 = params.bit1;
  bytes_config.flags.msb_first = 1;
  esp_err_t err =
      rmt_new_bytes_encoder(&bytes_config, &led_encoder->bytes_encoder);
  if (err != ESP_OK) {
    cfx_rmt_led_encoder_del(&led_encoder->base);
    return err;
  }

  rmt_copy_encoder_config_t reset_config;
  memset(&reset_config, 0, sizeof(reset_config));
  err = rmt_new_copy_encoder(&reset_config, &led_encoder->reset_encoder);
  if (err != ESP_OK) {
    cfx_rmt_led_encoder_del(&led_encoder->base);
    return err;
  }

  *ret_encoder = &led_encoder->base;
  return ESP_OK;
}
#endif

// --- P2: RMT async-done callback ---
//
// Fires from the RMT ISR when DMA completes. Clears the per-instance
// rmt_tx_in_flight_ flag so flush_rmt_() can poll non-blocking.
//
// Design: ctx carries &this->rmt_tx_in_flight_ registered in setup_rmt_().
// This is a member function (has protected access) that stores the address;
// the free ISR function writes through the void* — no class membership needed,
// no global pointer, works correctly for any number of RMT instances.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static bool IRAM_ATTR rmt_tx_done_cb_(rmt_channel_handle_t,
                                       const rmt_tx_done_event_data_t *,
                                       void *ctx) {
  // ctx = &rmt_tx_in_flight_ set in setup_rmt_() — one per instance.
  auto *done = static_cast<CFXRMTDoneContext *>(ctx);
  if (done == nullptr || done->in_flight == nullptr) {
    return false;
  }
  *done->in_flight = false;
  if (done->dma_enabled && g_rmt_dma_active_count > 0) {
    g_rmt_dma_active_count--;
  }
  return false;
}
#endif

void CFXLightOutput::setup() {
  if (this->setup_completed_) {
    ESP_LOGD(TAG, "CFXLight setup skipped: already initialized (this=%p pin=%u)",
             this, this->pin_);
    return;
  }

  size_t buffer_size = this->get_buffer_size_();
  ESP_LOGI(TAG,
           "CFXLight setup begin: this=%p transport=%s pin=%u leds=%u "
           "buffer=%u stride=%u",
           this, transport_label(this->transport_), this->pin_,
           this->num_leds_, static_cast<unsigned>(buffer_size),
           static_cast<unsigned>(this->get_pixel_stride_()));

  // Allocate pixel buffer (internal RAM)
  RAMAllocator<uint8_t> allocator(RAMAllocator<uint8_t>::ALLOC_INTERNAL);
  this->buf_ = allocator.allocate(buffer_size);
  if (this->buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate LED buffer (%u bytes)!", buffer_size);
    this->mark_failed();
    return;
  }
  memset(this->buf_, 0, buffer_size);

  // Allocate effect data buffer (1 byte per LED)
  this->effect_data_ = allocator.allocate(this->num_leds_);
  if (this->effect_data_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate effect data!");
    this->mark_failed();
    return;
  }

  // Transport-specific hardware init
  if (this->transport_ == TRANSPORT_SPI) {
    this->setup_spi_();
  } else if (this->transport_ == TRANSPORT_PARALLEL) {
    this->setup_parallel_();
  } else {
    this->setup_rmt_();
  }
  if (this->is_failed())
    return;

  // The LED strip keeps its last latched pixels across MCU resets until we
  // actively transmit a new frame. Push one startup blackout frame so a reboot
  // cannot leave the previous effect visually stuck on the strip until the
  // first HA-driven state update arrives.
  if (this->transport_ == TRANSPORT_SPI) {
    this->flush_spi_();
  } else if (this->transport_ == TRANSPORT_PARALLEL) {
    ESP_LOGI(TAG,
             "Parallel startup blackout is deferred until first light update "
             "(group=%s)",
             this->parallel_group_.c_str());
  } else {
    this->flush_rmt_();
  }
  this->reset_perf_diag_();

  // P3: Register with the transmit barrier so coordinated DMA launch is active
  // once all outputs have completed setup(). Single-output setups are no-ops
  // (barrier passes through immediately when count_ < 2).
  CFXTransmitBarrier::get().register_output(this);
  if (!this->segment_light_states_.empty()) {
    this->segment_coord_runners_.reserve(MAX_CFX_SEGMENTS);
  }

  // --- Phase 2: Set up Event-Driven State Synchronization ---
  // Decoupled from the high-frequency DMA write loop to prevent recursion!
  if (this->master_light_state_ != nullptr) {
    this->master_light_state_->set_default_transition_length(
        this->default_transition_length_ms_);
    this->master_listener_ = new MasterListener(this);
    this->master_light_state_->add_remote_values_listener(
        this->master_listener_);
    this->prev_master_state_ =
        this->master_light_state_->remote_values.is_on();
    this->prev_master_defaults_state_ =
        this->master_light_state_->remote_values.is_on();
  }

  if (!this->segment_light_states_.empty()) {
    for (auto *seg_state : this->segment_light_states_) {
      if (seg_state != nullptr) {
        seg_state->set_default_transition_length(
            this->default_transition_length_ms_);
      }
      auto *listener = new SegmentListener(this);
      this->segment_listeners_.push_back(listener);
      seg_state->add_remote_values_listener(listener);
    }
  }

  // QoL FIX: Live force-white reactivity for solid colors.
  // The switch may be attached either before or after setup(), so bind here
  // and also on late attachment from CFXControl.
  this->bind_force_white_switch_();

  if (this->transport_ == TRANSPORT_SPI) {
    ESP_LOGI(TAG, "CFXLight ready: %u LEDs on SPI (data=GPIO%u clock=GPIO%u speed=%" PRIu32 " Hz)",
             this->num_leds_, this->spi_data_pin_, this->spi_clock_pin_,
             this->spi_speed_hz_);
  } else if (this->transport_ == TRANSPORT_PARALLEL) {
    ESP_LOGI(TAG,
             "CFXLight ready: %u visible LEDs on GPIO%u "
             "(parallel_group=%s, stride=%u)",
             this->num_leds_, this->pin_, this->parallel_group_.c_str(),
             static_cast<unsigned>(this->get_pixel_stride_()));
  } else {
    ESP_LOGI(TAG,
             "CFXLight ready: %u visible LEDs on GPIO%u (%s, rmt_symbols=%u, "
             "mem_block_symbols=%u, physical_leds=%" PRIu32 ")",
             this->num_leds_, this->pin_,
             this->rmt_dma_enabled_ ? rmt_dma_backend_label() : "non-DMA",
             this->rmt_symbols_, this->rmt_mem_block_symbols_,
             this->get_rmt_physical_led_count_());
  }
  this->setup_completed_ = true;
}

void CFXLightOutput::setup_state(light::LightState *state) {
  light::AddressableLight::setup_state(state);
  if (!this->setup_completed_ && !this->is_failed() && this->num_leds_ > 0) {
    ESP_LOGW(TAG,
             "CFXLight output was not reached by the component setup pass "
             "(this=%p pin=%u state=%p); initializing from LightState setup",
             this, this->pin_, state);
    this->setup();
  }
}

// --- RMT Transport Setup (extracted from setup()) ---

void CFXLightOutput::setup_rmt_() {
  size_t buffer_size = this->get_rmt_transmit_buffer_size_();
  this->rmt_dma_enabled_ = false;
  this->rmt_mem_block_symbols_ = 0;
  this->rmt_alloc_index_ = 0;

  // Allocate RMT transmission buffer
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  RAMAllocator<uint8_t> rmt_alloc(RAMAllocator<uint8_t>::ALLOC_INTERNAL);
  this->rmt_buf_ = rmt_alloc.allocate(buffer_size);
  if (this->rmt_buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate RMT transmit buffer (%u bytes)!",
             static_cast<unsigned>(buffer_size));
    this->mark_failed();
    return;
  }
#else
  RAMAllocator<rmt_symbol_word_t> rmt_allocator(
      RAMAllocator<rmt_symbol_word_t>::ALLOC_INTERNAL);
  this->rmt_buf_ = rmt_allocator.allocate(buffer_size * 8 + 1);
  if (this->rmt_buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate RMT transmit buffer (%u symbols)!",
             static_cast<unsigned>(buffer_size * 8 + 1));
    this->mark_failed();
    return;
  }
#endif

  // Auto-detect RMT symbol buffer size from chip variant
  if (this->rmt_symbols_ == 0) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    this->rmt_symbols_ = 64;
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    this->rmt_symbols_ = 64;
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
    this->rmt_symbols_ = 48;
#else
    this->rmt_symbols_ = 48;
#endif
  }

  // Configure timing from chipset
  this->configure_timing_();

  // Create RMT TX channel.
  rmt_tx_channel_config_t channel;
  memset(&channel, 0, sizeof(channel));
  channel.clk_src = RMT_CLK_SRC_DEFAULT;
  channel.resolution_hz = rmt_resolution_hz();
  channel.gpio_num = gpio_num_t(this->pin_);
  channel.trans_queue_depth = 1;
  channel.flags.invert_out = 0;
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32S3) || \
    defined(CONFIG_IDF_TARGET_ESP32P4)
  channel.intr_priority = 2;
#else
  channel.intr_priority = 0;
#endif

  // DMA only supported on ESP32-S3 and ESP32-P4
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
  {
    static uint32_t s_rmt_alloc_count = 0;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // Probe GDMA per output on S3. Forcing every channel to non-DMA only
    // exposes two TX slots on current hardware, while old 4-RMT test builds
    // relied on the driver's allocator deciding which channels can use GDMA.
    const bool skip_dma_probe = false;
    const char *skip_dma_reason = "";
#else
    const bool skip_dma_probe = false;
    const char *skip_dma_reason = "";
#endif
    this->rmt_alloc_index_ = ++s_rmt_alloc_count;

    if (skip_dma_probe) {
      channel.flags.with_dma = false;
      channel.mem_block_symbols = rmt_non_dma_symbols(this->rmt_symbols_);
      ESP_LOGI(TAG,
               "RMT alloc #%" PRIu32
               ": pin=%u GDMA skipped (%s) "
               "mem_block_symbols=%u rmt_symbols=%u hw_tx_slots=%d",
               this->rmt_alloc_index_, this->pin_, skip_dma_reason,
               (unsigned)channel.mem_block_symbols, this->rmt_symbols_,
               SOC_RMT_TX_CANDIDATES_PER_GROUP);
      esp_err_t err = rmt_new_tx_channel(&channel, &this->channel_);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT channel creation failed (pin=%u, err=%d)",
                 this->pin_, (int) err);
        this->mark_failed();
        return;
      }
      this->rmt_dma_enabled_ = false;
      this->rmt_mem_block_symbols_ = channel.mem_block_symbols;
    } else {
      channel.flags.with_dma = true;
      // Keep S3/P4 GDMA on one 48-symbol block. Although ESP-IDF's field is
      // a DMA buffer size in this mode, larger values have caused S3 boot
      // instability during LightState setup on real hardware. Non-DMA channels
      // still use the configured rmt_symbols_ budget below.
      channel.mem_block_symbols = 48;
      ESP_LOGI(TAG,
               "RMT alloc #%" PRIu32 ": pin=%u %s=true mem_block_symbols=%u "
               "rmt_symbols=%u hw_tx_slots=%d",
               this->rmt_alloc_index_, this->pin_, rmt_dma_backend_label(),
               (unsigned)channel.mem_block_symbols, this->rmt_symbols_,
               SOC_RMT_TX_CANDIDATES_PER_GROUP);
      esp_err_t err = rmt_new_tx_channel(&channel, &this->channel_);
      if (err == ESP_OK) {
        this->rmt_dma_enabled_ = true;
        this->rmt_mem_block_symbols_ = channel.mem_block_symbols;
      } else {
        ESP_LOGW(TAG,
                 "RMT %s unavailable for pin=%u (alloc #%" PRIu32
                 " of %d hw slots, err=%d) - falling back to non-DMA "
                 "(mem_block_symbols=%u). "
                 "Check for other RMT consumers (remote_transmitter, "
                 "neopixelbus, status_led, ir_transmitter).",
                 rmt_dma_backend_label(), this->pin_, this->rmt_alloc_index_,
                 SOC_RMT_TX_CANDIDATES_PER_GROUP, (int) err,
                 rmt_non_dma_symbols(this->rmt_symbols_));
        this->channel_ = nullptr;
        channel.flags.with_dma = false;
        channel.mem_block_symbols = rmt_non_dma_symbols(this->rmt_symbols_);
        err = rmt_new_tx_channel(&channel, &this->channel_);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "RMT channel creation failed (pin=%u, err=%d)",
                   this->pin_, (int) err);
          this->mark_failed();
          return;
        }
        this->rmt_dma_enabled_ = false;
        this->rmt_mem_block_symbols_ = channel.mem_block_symbols;
        ESP_LOGI(TAG,
                 "RMT alloc #%" PRIu32
                 ": pin=%u non-DMA fallback OK mem_block_symbols=%u",
                 this->rmt_alloc_index_, this->pin_,
                 this->rmt_mem_block_symbols_);
      }
    }
  }
#else
  channel.flags.with_dma = false;
  channel.mem_block_symbols = this->rmt_symbols_;
  esp_err_t err = rmt_new_tx_channel(&channel, &this->channel_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RMT channel creation failed (pin=%u, err=%d)",
             this->pin_, (int) err);
    this->mark_failed();
    return;
  }
  this->rmt_dma_enabled_ = false;
  this->rmt_mem_block_symbols_ = channel.mem_block_symbols;
#endif

  // Create RMT encoder
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  ESP_LOGI(TAG, "RMT encoder: pin=%u bytes+reset", this->pin_);
  if (cfx_rmt_new_led_encoder(this->params_, &this->encoder_) != ESP_OK) {
    ESP_LOGE(TAG, "LED bytes encoder creation failed");
    this->mark_failed();
    return;
  }
#else
  rmt_copy_encoder_config_t encoder;
  memset(&encoder, 0, sizeof(encoder));
  if (rmt_new_copy_encoder(&encoder, &this->encoder_) != ESP_OK) {
    ESP_LOGE(TAG, "Copy encoder creation failed");
    this->mark_failed();
    return;
  }
#endif

  // Enable the RMT channel
  if (rmt_enable(this->channel_) != ESP_OK) {
    ESP_LOGE(TAG, "RMT channel enable failed");
    this->mark_failed();
    return;
  }

  // P2: Register async-done callback — mirrors SPI's spi_tx_in_flight_ pattern.
  // The ISR clears rmt_tx_in_flight_ the moment DMA finishes, letting
  // flush_rmt_() poll the flag non-blocking instead of calling the heavyweight
  // rmt_tx_wait_all_done() on every frame.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  {
    rmt_tx_event_callbacks_t rmt_cbs;
    memset(&rmt_cbs, 0, sizeof(rmt_cbs));
    rmt_cbs.on_trans_done = rmt_tx_done_cb_;
    // Pass the address of this instance's flag as ctx.
    // Each RMT instance gets its own ctx pointer — no shared global state.
    this->rmt_done_ctx_.in_flight = &this->rmt_tx_in_flight_;
    this->rmt_done_ctx_.dma_enabled = this->rmt_dma_enabled_;
    if (rmt_tx_register_event_callbacks(this->channel_, &rmt_cbs,
                                        (void *)&this->rmt_done_ctx_) != ESP_OK) {
      // Non-fatal: flush_rmt_() falls back to rmt_tx_wait_all_done() when
      // rmt_tx_in_flight_ is never set (startup state is false).
      ESP_LOGW(TAG, "RMT done callback registration failed — using blocking wait");
    } else {
      ESP_LOGI(TAG, "RMT async-done callback active (GPIO%u)", this->pin_);
    }
  }
#endif

  this->reset_rmt_encoder_diag_();
}

// --- Parallel Transport Setup ---

size_t CFXLightOutput::get_parallel_frame_size_() const {
  return static_cast<size_t>(this->num_leds_) * this->get_pixel_stride_() * 8u *
             PARALLEL_SYMBOL_SAMPLES +
         PARALLEL_RESET_SAMPLES;
}

uint16_t CFXLightOutput::get_parallel_required_led_count_() const {
  if (!this->is_parallel_transport() || this->num_leds_ == 0) {
    return 0;
  }
  if (!this->outro_cbs_.empty()) {
    return this->num_leds_;
  }
  if (this->segment_light_states_.empty()) {
    if ((this->master_light_state_ != nullptr &&
         this->master_light_state_->remote_values.is_on()) ||
        this->is_effect_active()) {
      return this->num_leds_;
    }
    return 0;
  }

  uint16_t required = 0;
  const size_t count =
      std::min(this->segment_light_states_.size(), this->segment_defs_.size());
  for (size_t i = 0; i < count; i++) {
    auto *seg_state = this->segment_light_states_[i];
    if (seg_state == nullptr) {
      continue;
    }
    auto *effect = resolve_active_cfx_effect(seg_state);
    const bool active =
        seg_state->remote_values.is_on() ||
        (effect != nullptr && !effect->is_clean_mono_idle_output());
    if (!active) {
      continue;
    }
    const auto &def = this->segment_defs_[i];
    const uint16_t stop =
        static_cast<uint16_t>(std::min<int>(def.stop, this->num_leds_));
    if (stop > required) {
      required = stop;
    }
  }
  return required;
}

void CFXLightOutput::setup_parallel_() {
  auto &g_parallel_group = *parallel_group_for_output_(this);
  if (this->parallel_lane_count_ == 0 ||
      this->parallel_lane_count_ > PARALLEL_MAX_LANES ||
      this->parallel_group_index_ >= PARALLEL_MAX_GROUPS ||
      this->parallel_bit_offset_ + this->parallel_lane_count_ >
          PARALLEL_I80_BUS_WIDTH ||
      this->parallel_lane_index_ >= this->parallel_lane_count_) {
    ESP_LOGE(TAG,
             "Parallel group '%s' has invalid lane geometry "
             "(group_index=%u bit_offset=%u index=%u count=%u)",
             this->parallel_group_.c_str(), this->parallel_group_index_,
             this->parallel_bit_offset_, this->parallel_lane_index_,
             this->parallel_lane_count_);
    this->mark_failed();
    return;
  }

  const size_t frame_size = this->get_parallel_frame_size_();
  if (!g_parallel_group.configured) {
    g_parallel_group = CFXParallelGroupRuntime{};
    g_parallel_group.configured = true;
    g_parallel_group.name = this->parallel_group_;
    g_parallel_group.group_index = this->parallel_group_index_;
    g_parallel_group.bit_offset = this->parallel_bit_offset_;
    g_parallel_group.lane_count = this->parallel_lane_count_;
    g_parallel_group.strobe_pin = this->parallel_strobe_pin_;
    g_parallel_group.dc_pin = this->parallel_dc_pin_;
    g_parallel_group.frame_size = frame_size;
    // Full-frame single transaction: set chunk_leds = num_leds so each flush()
    // is one DMA transaction. Multiple transactions per frame cause I80
    // inter-transaction gaps (100-500 µs ISR re-arm) that exceed the SK6812
    // reset threshold (80 µs), scrambling the strip. With 234 KB DMA available
    // on ESP32-S3, even 680 LEDs (87 KB) fits comfortably.
#if defined(CONFIG_IDF_TARGET_ESP32)
    const uint16_t backend_chunk_leds = PARALLEL_CLASSIC_CHUNK_LEDS;
    g_parallel_group.chunk_leds = static_cast<uint16_t>(
        std::min<uint16_t>(this->num_leds_, backend_chunk_leds));
#else
    // S3/C3: always one chunk = full frame, no splitting.
    g_parallel_group.chunk_leds = this->num_leds_;
#endif
    g_parallel_group.chunk_frame_size =
        static_cast<size_t>(g_parallel_group.chunk_leds) *
        this->get_pixel_stride_() * 8u * PARALLEL_SYMBOL_SAMPLES;
    g_parallel_group.bus_max_transfer_size =
        g_parallel_group.chunk_frame_size + PARALLEL_RESET_SAMPLES;
    g_parallel_group.chunk_alloc_size =
        g_parallel_group.bus_max_transfer_size + PARALLEL_CANARY_BYTES;
#if defined(CONFIG_IDF_TARGET_ESP32)
    g_parallel_group.chunk_alloc_size =
        g_parallel_group.chunk_frame_size + PARALLEL_CANARY_BYTES;
    g_parallel_group.buffer_count = PARALLEL_TX_BUFFER_COUNT;
#else
    // S3/C3: Triple-buffered for <= 200 LEDs to guarantee smooth ~60 FPS target,
    // double-buffered <= 400 LEDs to optimize heap, and single-buffered above 400.
    if (this->num_leds_ <= 200) {
      g_parallel_group.buffer_count = 3;
    } else if (this->num_leds_ <= 400) {
      g_parallel_group.buffer_count = 2;
    } else {
      g_parallel_group.buffer_count = 1;
    }
#endif
    for (uint8_t i = 0; i < PARALLEL_I80_BUS_WIDTH; i++) {
      g_parallel_group.lane_pins[i] = this->parallel_lane_pins_[i];
    }

#if !defined(CONFIG_IDF_TARGET_ESP32)
    if (!GPIO_IS_VALID_OUTPUT_GPIO(g_parallel_group.strobe_pin) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(g_parallel_group.dc_pin) ||
        g_parallel_group.strobe_pin == g_parallel_group.dc_pin ||
        parallel_pin_used_(g_parallel_group.strobe_pin,
                           g_parallel_group.lane_pins,
                           PARALLEL_I80_BUS_WIDTH) ||
        parallel_pin_used_(g_parallel_group.dc_pin,
                           g_parallel_group.lane_pins,
                           PARALLEL_I80_BUS_WIDTH)) {
      ESP_LOGE(TAG,
               "Parallel group '%s' has invalid internal LCD/I80 pins "
               "(wr=GPIO%u dc=GPIO%u)",
               g_parallel_group.name.c_str(), g_parallel_group.strobe_pin,
               g_parallel_group.dc_pin);
      this->mark_failed();
      return;
    }
    for (uint8_t i = 0; i < PARALLEL_I80_BUS_WIDTH; i++) {
      if (GPIO_IS_VALID_OUTPUT_GPIO(g_parallel_group.lane_pins[i])) {
        continue;
      }
      bool assigned = false;
      for (uint8_t candidate : PARALLEL_DUMMY_PIN_CANDIDATES) {
        if (!GPIO_IS_VALID_OUTPUT_GPIO(candidate) ||
            candidate == g_parallel_group.strobe_pin ||
            candidate == g_parallel_group.dc_pin ||
            parallel_pin_used_(candidate, g_parallel_group.lane_pins, i)) {
          continue;
        }
        g_parallel_group.lane_pins[i] = candidate;
        assigned = true;
        break;
      }
      if (!assigned) {
        ESP_LOGE(TAG,
                 "Parallel group '%s' cannot reserve enough output-capable "
                 "I80 filler GPIOs",
                 g_parallel_group.name.c_str());
        this->mark_failed();
        return;
      }
    }
    for (uint8_t i = 0; i < PARALLEL_I80_BUS_WIDTH; i++) {
      if (!GPIO_IS_VALID_OUTPUT_GPIO(g_parallel_group.lane_pins[i])) {
        ESP_LOGE(TAG,
                 "Parallel group '%s' has invalid LCD/I80 data GPIO%u at "
                 "data[%u]",
                 g_parallel_group.name.c_str(), g_parallel_group.lane_pins[i],
                 i);
        this->mark_failed();
        return;
      }
    }
#endif

#if defined(CONFIG_IDF_TARGET_ESP32)
    ESP_LOGI(TAG,
             "Parallel backend %s group '%s' configured for deferred init: "
             "lanes=%u pclk=%" PRIu32
             "Hz frame=%u bytes chunk=%u leds/%u bytes alloc=%u "
             "data=[%u,%u,%u,%u]",
             PARALLEL_BACKEND_REV, g_parallel_group.name.c_str(),
             g_parallel_group.lane_count, PARALLEL_PCLK_HZ,
             static_cast<unsigned>(frame_size), g_parallel_group.chunk_leds,
             static_cast<unsigned>(g_parallel_group.chunk_frame_size),
             static_cast<unsigned>(g_parallel_group.chunk_alloc_size),
             g_parallel_group.lane_pins[0], g_parallel_group.lane_pins[1],
             g_parallel_group.lane_pins[2], g_parallel_group.lane_pins[3]);
#else
    ESP_LOGI(TAG,
             "Parallel backend %s group '%s' configured for deferred init: "
             "lanes=%u wr=GPIO%u internal_dc=GPIO%u pclk=%" PRIu32
             "Hz frame=%u bytes chunk=%u leds/%u bytes bus_max=%u alloc=%u "
             "data=[%u,%u,%u,%u,%u,%u,%u,%u]",
             PARALLEL_LCD_BACKEND_REV, g_parallel_group.name.c_str(),
             g_parallel_group.lane_count, g_parallel_group.strobe_pin,
             g_parallel_group.dc_pin, PARALLEL_PCLK_HZ,
             static_cast<unsigned>(frame_size),
             g_parallel_group.chunk_leds,
             static_cast<unsigned>(g_parallel_group.chunk_frame_size),
             static_cast<unsigned>(g_parallel_group.bus_max_transfer_size),
             static_cast<unsigned>(g_parallel_group.chunk_alloc_size),
             g_parallel_group.lane_pins[0], g_parallel_group.lane_pins[1],
             g_parallel_group.lane_pins[2], g_parallel_group.lane_pins[3],
             g_parallel_group.lane_pins[4], g_parallel_group.lane_pins[5],
             g_parallel_group.lane_pins[6], g_parallel_group.lane_pins[7]);
#endif
  } else {
    if (g_parallel_group.name != this->parallel_group_ ||
        g_parallel_group.group_index != this->parallel_group_index_ ||
        g_parallel_group.bit_offset != this->parallel_bit_offset_ ||
        g_parallel_group.lane_count != this->parallel_lane_count_) {
      ESP_LOGE(TAG, "Parallel group '%s' does not match existing group '%s'",
               this->parallel_group_.c_str(), g_parallel_group.name.c_str());
      this->mark_failed();
      return;
    }
#if !defined(CONFIG_IDF_TARGET_ESP32)
    if (g_parallel_group.strobe_pin != this->parallel_strobe_pin_ ||
        g_parallel_group.dc_pin != this->parallel_dc_pin_) {
      ESP_LOGE(TAG, "Parallel group '%s' does not match existing group '%s'",
               this->parallel_group_.c_str(), g_parallel_group.name.c_str());
      this->mark_failed();
      return;
    }
#endif
  }

  g_parallel_group.outputs[this->parallel_lane_index_] = this;
  if (this->has_segments()) {
    g_parallel_group.has_segments = true;
  }
  ESP_LOGI(TAG,
           "Parallel lane %u/%u registered: group='%s' group_index=%u "
           "bit_offset=%u data=GPIO%u",
           this->parallel_lane_index_ + 1, g_parallel_group.lane_count,
           g_parallel_group.name.c_str(), g_parallel_group.group_index,
           g_parallel_group.bit_offset, this->pin_);
  // Phase-1 diag: log segment definitions at boot so we can verify geometry.
  for (size_t si = 0; si < this->segment_defs_.size(); si++) {
    const auto &sd = this->segment_defs_[si];
    ESP_LOGI(TAG,
             "  lane %u seg[%u] id='%s' start=%u stop=%u len=%u stride=%u",
             this->parallel_lane_index_, static_cast<unsigned>(si),
             sd.id.c_str(), sd.start, sd.stop,
             static_cast<unsigned>(sd.stop - sd.start),
             this->get_pixel_stride_());
  }
}

bool CFXLightOutput::init_parallel_backend_() {
  auto &g_parallel_group = *parallel_group_for_output_(this);
  uint16_t max_leds = 0;
  bool has_segments = false;
  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    if (g_parallel_group.outputs[lane] != nullptr) {
      max_leds = std::max(max_leds, g_parallel_group.outputs[lane]->num_leds_);
      has_segments = has_segments || g_parallel_group.outputs[lane]->has_segments();
    }
  }
  g_parallel_group.max_leds = max_leds;
  g_parallel_group.has_segments = has_segments;

#if defined(CONFIG_IDF_TARGET_ESP32)
  if (g_parallel_group.ready) {
    return true;
  }
  if (!g_parallel_group.configured) {
    ESP_LOGE(TAG, "Parallel group '%s' was not configured before init",
             this->parallel_group_.c_str());
    return false;
  }
  if (max_leds > 0) {
    const uint16_t backend_chunk_leds = PARALLEL_CLASSIC_CHUNK_LEDS;
    g_parallel_group.chunk_leds = static_cast<uint16_t>(
        std::min<uint16_t>(max_leds, backend_chunk_leds));
    g_parallel_group.chunk_frame_size =
        static_cast<size_t>(g_parallel_group.chunk_leds) *
        this->get_pixel_stride_() * 8u * PARALLEL_SYMBOL_SAMPLES;
    g_parallel_group.bus_max_transfer_size =
        g_parallel_group.chunk_frame_size + PARALLEL_RESET_SAMPLES;
    g_parallel_group.chunk_alloc_size =
        g_parallel_group.chunk_frame_size + PARALLEL_CANARY_BYTES;
  }
  if (g_parallel_group.init_attempted) {
    ESP_LOGW(TAG, "Retrying parallel Classic I2S init for group '%s'",
             g_parallel_group.name.c_str());
  }
  g_parallel_group.init_attempted = true;

  const size_t dma_free =
      heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  const size_t dma_largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  const size_t waveform_dma_bytes =
      g_parallel_group.chunk_frame_size * PARALLEL_TX_BUFFER_COUNT;
  const size_t control_dma_bytes =
      PARALLEL_CLASSIC_SILENCE_BYTES + PARALLEL_RESET_SAMPLES +
      sizeof(lldesc_t) * PARALLEL_CLASSIC_DESC_COUNT;
  ESP_LOGI(TAG,
           "Parallel backend %s initializing group '%s': lanes=%u "
           "frame=%u bytes chunk=%u leds/%u bytes buffers=%u/%u bytes "
           "desc=%u control=%u dma_free=%u dma_largest=%u",
           PARALLEL_BACKEND_REV, g_parallel_group.name.c_str(),
           g_parallel_group.lane_count,
           static_cast<unsigned>(g_parallel_group.frame_size),
           g_parallel_group.chunk_leds,
           static_cast<unsigned>(g_parallel_group.chunk_frame_size),
           static_cast<unsigned>(PARALLEL_TX_BUFFER_COUNT),
           static_cast<unsigned>(waveform_dma_bytes),
           static_cast<unsigned>(PARALLEL_CLASSIC_DESC_COUNT),
           static_cast<unsigned>(control_dma_bytes),
           static_cast<unsigned>(dma_free), static_cast<unsigned>(dma_largest));

  g_parallel_group.classic_descs = static_cast<lldesc_t *>(heap_caps_calloc(
      PARALLEL_CLASSIC_DESC_COUNT, sizeof(lldesc_t),
      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  g_parallel_group.classic_silence_buf = static_cast<uint8_t *>(heap_caps_calloc(
      1, PARALLEL_CLASSIC_SILENCE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  g_parallel_group.classic_reset_buf = static_cast<uint8_t *>(heap_caps_calloc(
      1, PARALLEL_RESET_SAMPLES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (g_parallel_group.classic_descs == nullptr ||
      g_parallel_group.classic_silence_buf == nullptr ||
      g_parallel_group.classic_reset_buf == nullptr) {
    ESP_LOGE(TAG,
             "Cannot allocate Classic parallel I2S descriptor/control DMA "
             "memory for group '%s'",
             g_parallel_group.name.c_str());
    free(g_parallel_group.classic_descs);
    free(g_parallel_group.classic_silence_buf);
    free(g_parallel_group.classic_reset_buf);
    g_parallel_group.classic_descs = nullptr;
    g_parallel_group.classic_silence_buf = nullptr;
    g_parallel_group.classic_reset_buf = nullptr;
    g_parallel_group.init_attempted = false;
    return false;
  }

  for (uint8_t i = 0; i < PARALLEL_TX_BUFFER_COUNT; i++) {
    g_parallel_group.frame_bufs[i] = static_cast<uint8_t *>(heap_caps_malloc(
        g_parallel_group.chunk_alloc_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (g_parallel_group.frame_bufs[i] == nullptr) {
      ESP_LOGE(TAG,
               "Cannot allocate Classic parallel rolling DMA buffer %u/%u "
               "(%u bytes)",
               static_cast<unsigned>(i + 1),
               static_cast<unsigned>(PARALLEL_TX_BUFFER_COUNT),
               static_cast<unsigned>(g_parallel_group.chunk_alloc_size));
      for (uint8_t j = 0; j < i; j++) {
        free(g_parallel_group.frame_bufs[j]);
        g_parallel_group.frame_bufs[j] = nullptr;
      }
      free(g_parallel_group.classic_descs);
      free(g_parallel_group.classic_silence_buf);
      free(g_parallel_group.classic_reset_buf);
      g_parallel_group.classic_descs = nullptr;
      g_parallel_group.classic_silence_buf = nullptr;
      g_parallel_group.classic_reset_buf = nullptr;
      g_parallel_group.init_attempted = false;
      return false;
    }
    memset(g_parallel_group.frame_bufs[i], 0, g_parallel_group.chunk_alloc_size);
    memset(g_parallel_group.frame_bufs[i] + g_parallel_group.chunk_frame_size,
           PARALLEL_CANARY_VALUE, PARALLEL_CANARY_BYTES);
  }
  g_parallel_group.frame_buf = g_parallel_group.frame_bufs[0];

  auto *descs = g_parallel_group.classic_descs;
  parallel_dma_desc_init_(&descs[PARALLEL_CLASSIC_SILENCE_DESC_A],
                          g_parallel_group.classic_silence_buf,
                          PARALLEL_CLASSIC_SILENCE_BYTES,
                          &descs[PARALLEL_CLASSIC_SILENCE_DESC_B], false);
  parallel_dma_desc_init_(&descs[PARALLEL_CLASSIC_SILENCE_DESC_B],
                          g_parallel_group.classic_silence_buf,
                          PARALLEL_CLASSIC_SILENCE_BYTES,
                          &descs[PARALLEL_CLASSIC_SILENCE_DESC_A], false);
  parallel_dma_desc_init_(&descs[PARALLEL_CLASSIC_RESET_DESC],
                          g_parallel_group.classic_reset_buf,
                          PARALLEL_RESET_SAMPLES,
                          &descs[PARALLEL_CLASSIC_SILENCE_DESC_A], false);
  for (uint8_t i = 0; i < PARALLEL_TX_BUFFER_COUNT; i++) {
    const uint8_t first_desc =
        PARALLEL_CLASSIC_DATA_DESC_BASE + i * PARALLEL_CLASSIC_DESC_PER_BUFFER;
    g_parallel_group.classic_data_desc_tail[i] = first_desc;
    parallel_dma_desc_init_(&descs[first_desc], g_parallel_group.frame_bufs[i],
                            4, &descs[PARALLEL_CLASSIC_RESET_DESC], true);
    parallel_dma_desc_init_(&descs[first_desc + 1],
                            g_parallel_group.frame_bufs[i], 4,
                            &descs[PARALLEL_CLASSIC_RESET_DESC], false);
  }

  esp_err_t err = parallel_classic_configure_i2s_(&g_parallel_group);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Classic parallel I2S backend init failed for group '%s' "
             "(err=%d)",
             g_parallel_group.name.c_str(), static_cast<int>(err));
    for (uint8_t i = 0; i < PARALLEL_TX_BUFFER_COUNT; i++) {
      free(g_parallel_group.frame_bufs[i]);
      g_parallel_group.frame_bufs[i] = nullptr;
    }
    free(g_parallel_group.classic_descs);
    free(g_parallel_group.classic_silence_buf);
    free(g_parallel_group.classic_reset_buf);
    g_parallel_group.classic_descs = nullptr;
    g_parallel_group.classic_silence_buf = nullptr;
    g_parallel_group.classic_reset_buf = nullptr;
    g_parallel_group.frame_buf = nullptr;
    g_parallel_group.init_attempted = false;
    return false;
  }

  g_parallel_group.ready = true;
  ESP_LOGI(TAG,
           "Parallel backend %s group '%s' ready: lanes=%u chunk=%u leds/%u "
           "bytes dma_buffers=%u waveform_dma=%u underruns=%" PRIu32,
           PARALLEL_BACKEND_REV, g_parallel_group.name.c_str(),
           g_parallel_group.lane_count, g_parallel_group.chunk_leds,
           static_cast<unsigned>(g_parallel_group.chunk_frame_size),
           static_cast<unsigned>(PARALLEL_TX_BUFFER_COUNT),
           static_cast<unsigned>(waveform_dma_bytes),
           g_parallel_group.classic_underrun_count);
  return true;
#elif defined(CFX_PARALLEL_I80_ENABLED) && SOC_LCD_I80_SUPPORTED
  if (!g_parallel_group.configured) {
    ESP_LOGE(TAG, "Parallel group '%s' was not configured before init",
             this->parallel_group_.c_str());
    return false;
  }

  auto refresh_group_geometry = [&](CFXParallelGroupRuntime &group) -> uint8_t {
    uint16_t group_max_leds = 0;
    bool group_has_segments = false;
    uint8_t group_stride = this->get_pixel_stride_();
    bool stride_set = false;
    for (uint8_t lane = 0; lane < group.lane_count; lane++) {
      auto *output = group.outputs[lane];
      if (output == nullptr) {
        continue;
      }
      group_max_leds = std::max(group_max_leds, output->num_leds_);
      group_has_segments = group_has_segments || output->has_segments();
      if (!stride_set) {
        group_stride = output->get_pixel_stride_();
        stride_set = true;
      }
    }
    group.max_leds = group_max_leds;
    group.has_segments = group_has_segments;
    if (group_max_leds > 0) {
      group.frame_size =
          static_cast<size_t>(group_max_leds) * group_stride * 8u *
              PARALLEL_SYMBOL_SAMPLES +
          PARALLEL_RESET_SAMPLES;
    }
    return group_stride;
  };

  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    if (g_parallel_groups[gi].configured) {
      refresh_group_geometry(g_parallel_groups[gi]);
    }
  }
  uint16_t shared_max_leds = 0;
  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    if (g_parallel_groups[gi].configured) {
      shared_max_leds =
          std::max(shared_max_leds, g_parallel_groups[gi].max_leds);
    }
  }

  if (g_parallel_group.ready) {
    return true;
  }

  const bool multiple_groups = parallel_configured_group_count_() > 1;
  bool multiple_group_has_segments = false;
  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    if (g_parallel_groups[gi].configured &&
        g_parallel_groups[gi].has_segments) {
      multiple_group_has_segments = true;
      break;
    }
  }
  const bool segmented_high_lane =
      g_parallel_group.has_segments && g_parallel_group.lane_count >= 3;

  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    auto &group = g_parallel_groups[gi];
    if (!group.configured) {
      continue;
    }
    const bool group_segmented_high =
        group.has_segments && group.lane_count >= 3;
    group.buffer_policy =
        multiple_groups && !multiple_group_has_segments
            ? "shared_xgroup_whole"
            : multiple_groups
            ? "shared_two_group_heap"
            : (group_segmented_high ? "segmented_3plus_heap" : "default");
  }

  auto select_lcd_buffer_count = [&](uint16_t chunk_leds) -> uint8_t {
    if ((multiple_groups && multiple_group_has_segments) ||
        segmented_high_lane) {
      return 1;
    }
    if (multiple_groups) {
      return 2;
    }
    if (chunk_leds <= 200) {
      return 3;
    }
    return 2;
  };

  auto configure_lcd_geometry = [&](CFXParallelGroupRuntime &group,
                                    uint16_t requested_chunk_leds,
                                    uint8_t buffer_count) {
    const uint8_t group_stride = refresh_group_geometry(group);
    group.chunk_leds = static_cast<uint16_t>(
        std::min<uint16_t>(group.max_leds, requested_chunk_leds));
    group.chunk_frame_size =
        static_cast<size_t>(group.chunk_leds) * group_stride * 8u *
        PARALLEL_SYMBOL_SAMPLES;
    group.bus_max_transfer_size =
        group.chunk_frame_size + PARALLEL_RESET_SAMPLES;
    group.chunk_alloc_size =
        group.bus_max_transfer_size + PARALLEL_CANARY_BYTES;
    if (buffer_count == 0) {
      buffer_count = 1;
    }
    if (buffer_count > PARALLEL_TX_BUFFER_COUNT) {
      buffer_count = PARALLEL_TX_BUFFER_COUNT;
    }
    group.buffer_count = buffer_count;
    group.next_tx_buffer_index = 0;
    group.last_tx_buffer_index = 0;
  };

  auto attach_shared_backend = [&](CFXParallelGroupRuntime &group) {
    group.bus = g_parallel_i80.bus;
    group.io = g_parallel_i80.io;
    group.frame_buf = g_parallel_i80.frame_buf;
    for (uint8_t i = 0; i < PARALLEL_TX_BUFFER_COUNT; i++) {
      group.frame_bufs[i] = g_parallel_i80.frame_bufs[i];
    }
    group.buffer_count = g_parallel_i80.buffer_count;
    group.ready = true;
    group.init_attempted = true;
  };

  auto release_lcd_backend = [&]() {
    this->deinit_parallel_backend_();
  };

  if (g_parallel_i80.ready) {
    configure_lcd_geometry(g_parallel_group, g_parallel_group.max_leds,
                           g_parallel_i80.buffer_count);
    if (g_parallel_group.chunk_alloc_size <= g_parallel_i80.chunk_alloc_size &&
        g_parallel_group.bus_max_transfer_size <=
            g_parallel_i80.bus_max_transfer_size) {
      attach_shared_backend(g_parallel_group);
      return true;
    }
    uint32_t wait_us = 0;
    if (!wait_for_parallel_group_idle_(100000u, g_parallel_i80.active_group,
                                       &wait_us)) {
      ESP_LOGE(TAG,
               "Parallel shared I80 backend is too small for group '%s' and "
               "cannot resize while TX is active",
               g_parallel_group.name.c_str());
      return false;
    }
    release_lcd_backend();
  }

  if (g_parallel_i80.init_attempted) {
    ESP_LOGW(TAG, "Retrying shared parallel I80 backend init");
  }
  g_parallel_i80.init_attempted = true;
  g_parallel_group.init_attempted = true;

  const uint16_t chunk_attempts[] = {shared_max_leds,
                                     PARALLEL_LCD_CHUNK_LEDS,
                                     PARALLEL_LCD_SECONDARY_CHUNK_LEDS,
                                     PARALLEL_LCD_FALLBACK_CHUNK_LEDS};
  esp_err_t last_err = ESP_OK;
  uint16_t previous_attempt = 0;
  for (uint16_t requested_chunk_leds : chunk_attempts) {
    const uint16_t selected_chunk_leds =
        static_cast<uint16_t>(std::min<uint16_t>(shared_max_leds,
                                                requested_chunk_leds));
    if (selected_chunk_leds == 0 || selected_chunk_leds == previous_attempt) {
      continue;
    }
    previous_attempt = selected_chunk_leds;
    const uint8_t preferred_buffers =
        select_lcd_buffer_count(selected_chunk_leds);
    const uint8_t buffer_attempts[2] = {preferred_buffers, 1};
    const uint8_t buffer_attempt_count = preferred_buffers > 1 ? 2 : 1;

    for (uint8_t buffer_attempt = 0; buffer_attempt < buffer_attempt_count;
         buffer_attempt++) {
      if (buffer_attempt > 0 &&
          buffer_attempts[buffer_attempt] == buffer_attempts[buffer_attempt - 1]) {
        continue;
      }
      const uint8_t attempted_buffers = buffer_attempts[buffer_attempt];
      size_t shared_chunk_alloc_size = 0;
      size_t shared_bus_max_transfer_size = 0;
      bool data_pins_set = false;
      for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
        auto &group = g_parallel_groups[gi];
        if (!group.configured) {
          continue;
        }
        configure_lcd_geometry(group, selected_chunk_leds, attempted_buffers);
        shared_chunk_alloc_size =
            std::max(shared_chunk_alloc_size, group.chunk_alloc_size);
        shared_bus_max_transfer_size =
            std::max(shared_bus_max_transfer_size, group.bus_max_transfer_size);
        if (!data_pins_set) {
          g_parallel_i80.strobe_pin = group.strobe_pin;
          g_parallel_i80.dc_pin = group.dc_pin;
          for (uint8_t pin_index = 0; pin_index < PARALLEL_I80_BUS_WIDTH;
               pin_index++) {
            g_parallel_i80.lane_pins[pin_index] = group.lane_pins[pin_index];
          }
          data_pins_set = true;
        } else {
          for (uint8_t pin_index = 0; pin_index < PARALLEL_I80_BUS_WIDTH;
               pin_index++) {
            if (g_parallel_i80.lane_pins[pin_index] !=
                group.lane_pins[pin_index]) {
              ESP_LOGE(TAG,
                       "Parallel group '%s' does not match shared I80 data "
                       "pin map",
                       group.name.c_str());
              return false;
            }
          }
        }
      }

      g_parallel_i80.chunk_alloc_size = shared_chunk_alloc_size;
      g_parallel_i80.bus_max_transfer_size = shared_bus_max_transfer_size;
      g_parallel_i80.buffer_count = attempted_buffers;

      esp_lcd_i80_bus_config_t bus_config = {};
      bus_config.dc_gpio_num = g_parallel_i80.dc_pin;
      bus_config.wr_gpio_num = g_parallel_i80.strobe_pin;
      bus_config.clk_src = LCD_CLK_SRC_DEFAULT;
      bus_config.bus_width = PARALLEL_I80_BUS_WIDTH;
      bus_config.max_transfer_bytes = g_parallel_i80.bus_max_transfer_size;
      // Keep the ESP-IDF default burst setting on S3. Forcing 4 here trips a
      // GDMA burst-size assert inside esp_lcd_new_i80_bus() on ESP-IDF 5.5.x.
      for (uint8_t i = 0; i < PARALLEL_I80_BUS_WIDTH; i++) {
        bus_config.data_gpio_nums[i] = g_parallel_i80.lane_pins[i];
      }

      const size_t dma_free =
          heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
      const size_t dma_largest =
          heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
      const size_t waveform_dma_bytes =
          g_parallel_i80.chunk_alloc_size * g_parallel_i80.buffer_count;
      ESP_LOGI(TAG,
               "Parallel backend %s initializing group '%s': lanes=%u wr=GPIO%u "
               "internal_dc=GPIO%u frame=%u bytes chunk=%u leds/%u bytes "
               "buffers=%u/%u bytes queue_depth=%u bus_max=%u alloc=%u "
               "dma_free=%u dma_largest=%u buffer_policy=%s "
               "data=[%u,%u,%u,%u,%u,%u,%u,%u]",
               PARALLEL_LCD_BACKEND_REV, g_parallel_group.name.c_str(),
               g_parallel_group.lane_count, g_parallel_i80.strobe_pin,
               g_parallel_i80.dc_pin,
               static_cast<unsigned>(g_parallel_group.frame_size),
               g_parallel_group.chunk_leds,
               static_cast<unsigned>(g_parallel_group.chunk_frame_size),
               static_cast<unsigned>(g_parallel_i80.buffer_count),
               static_cast<unsigned>(waveform_dma_bytes),
               static_cast<unsigned>(g_parallel_i80.buffer_count),
               static_cast<unsigned>(g_parallel_i80.bus_max_transfer_size),
               static_cast<unsigned>(g_parallel_i80.chunk_alloc_size),
               static_cast<unsigned>(dma_free),
               static_cast<unsigned>(dma_largest),
               g_parallel_group.buffer_policy,
               g_parallel_i80.lane_pins[0], g_parallel_i80.lane_pins[1],
               g_parallel_i80.lane_pins[2], g_parallel_i80.lane_pins[3],
               g_parallel_i80.lane_pins[4], g_parallel_i80.lane_pins[5],
               g_parallel_i80.lane_pins[6], g_parallel_i80.lane_pins[7]);

      last_err = esp_lcd_new_i80_bus(&bus_config, &g_parallel_i80.bus);
      if (last_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Parallel I80 bus init failed for group '%s' at %u LED chunks "
                 "(err=%d, buffers=%u, wr=GPIO%u, internal_dc=GPIO%u); trying "
                 "fallback if available",
                 g_parallel_group.name.c_str(), g_parallel_group.chunk_leds,
                 (int) last_err,
                 static_cast<unsigned>(g_parallel_i80.buffer_count),
                 g_parallel_i80.strobe_pin, g_parallel_i80.dc_pin);
        release_lcd_backend();
        continue;
      }

      esp_lcd_panel_io_i80_config_t io_config = {};
      io_config.cs_gpio_num = -1;
      io_config.pclk_hz = PARALLEL_PCLK_HZ;
      io_config.trans_queue_depth = g_parallel_i80.buffer_count;
      io_config.on_color_trans_done = parallel_tx_done_cb_;
      io_config.user_ctx = &g_parallel_i80;
      // We must disable the command phase to prevent 8 dummy 0x00 bytes
      // from being inserted between chunks, which corrupts WS2812 bit timing.
      io_config.lcd_cmd_bits = 0;
      io_config.lcd_param_bits = 0;
      io_config.dc_levels.dc_idle_level = 0;
      io_config.dc_levels.dc_cmd_level = 0;
      io_config.dc_levels.dc_dummy_level = 0;
      io_config.dc_levels.dc_data_level = 1;
      io_config.flags.pclk_idle_low = 1;

      last_err = esp_lcd_new_panel_io_i80(g_parallel_i80.bus, &io_config,
                                          &g_parallel_i80.io);
      if (last_err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Parallel I80 panel IO init failed for group '%s' at %u LED "
                 "chunks (err=%d, buffers=%u); trying fallback if available",
                 g_parallel_group.name.c_str(), g_parallel_group.chunk_leds,
                 (int) last_err,
                 static_cast<unsigned>(g_parallel_i80.buffer_count));
        release_lcd_backend();
        continue;
      }

      bool buffers_ready = true;
      for (uint8_t i = 0; i < g_parallel_i80.buffer_count; i++) {
        g_parallel_i80.frame_bufs[i] = static_cast<uint8_t *>(
            esp_lcd_i80_alloc_draw_buffer(g_parallel_i80.io,
                                          g_parallel_i80.chunk_alloc_size,
                                          MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (g_parallel_i80.frame_bufs[i] == nullptr) {
          ESP_LOGW(TAG,
                   "Cannot allocate S3 parallel frame buffer %u/%u "
                   "(%u bytes, DMA, chunk=%u LEDs); largest DMA block before "
                   "init was %u bytes",
                   static_cast<unsigned>(i + 1),
                   static_cast<unsigned>(g_parallel_i80.buffer_count),
                   static_cast<unsigned>(g_parallel_i80.chunk_alloc_size),
                   g_parallel_group.chunk_leds,
                   static_cast<unsigned>(dma_largest));
          buffers_ready = false;
          break;
        }
        memset(g_parallel_i80.frame_bufs[i], 0, g_parallel_i80.chunk_alloc_size);
        memset(g_parallel_i80.frame_bufs[i] + g_parallel_group.chunk_frame_size,
               PARALLEL_CANARY_VALUE, PARALLEL_CANARY_BYTES);
      }
      if (!buffers_ready) {
        last_err = ESP_ERR_NO_MEM;
        release_lcd_backend();
        continue;
      }

      g_parallel_i80.frame_buf = g_parallel_i80.frame_bufs[0];
      g_parallel_i80.ready = true;
      for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
        auto &group = g_parallel_groups[gi];
        if (!group.configured) {
          continue;
        }
        attach_shared_backend(group);
        ESP_LOGI(TAG,
                 "Parallel backend %s group '%s' ready: lanes=%u group_index=%u "
                 "bit_offset=%u chunk=%u leds/%u bytes dma_buffers=%u "
                 "waveform_dma=%u queue_depth=%u buffer_policy=%s "
                 "data=[%u,%u,%u,%u,%u,%u,%u,%u]",
                 PARALLEL_LCD_BACKEND_REV, group.name.c_str(),
                 group.lane_count, group.group_index, group.bit_offset,
                 group.chunk_leds,
                 static_cast<unsigned>(group.chunk_frame_size),
                 static_cast<unsigned>(g_parallel_i80.buffer_count),
                 static_cast<unsigned>(waveform_dma_bytes),
                 static_cast<unsigned>(g_parallel_i80.buffer_count),
                 group.buffer_policy,
                 g_parallel_i80.lane_pins[0], g_parallel_i80.lane_pins[1],
                 g_parallel_i80.lane_pins[2], g_parallel_i80.lane_pins[3],
                 g_parallel_i80.lane_pins[4], g_parallel_i80.lane_pins[5],
                 g_parallel_i80.lane_pins[6], g_parallel_i80.lane_pins[7]);
      }
      return true;
    }
  }

  g_parallel_group.init_attempted = false;
  g_parallel_i80.init_attempted = false;
  ESP_LOGE(TAG,
           "Parallel S3 I80 backend init failed for group '%s' after full/%u/%u "
           "LED chunk attempts (last_err=%d)",
           g_parallel_group.name.c_str(), PARALLEL_LCD_SECONDARY_CHUNK_LEDS,
           PARALLEL_LCD_FALLBACK_CHUNK_LEDS, (int) last_err);
  return false;
#elif !defined(CFX_PARALLEL_I80_ENABLED)
  ESP_LOGE(TAG,
           "Parallel I80 backend is not compiled into this boot-safe "
           "diagnostic build (group '%s').",
           this->parallel_group_.c_str());
  return false;
#else
  ESP_LOGE(TAG,
           "Parallel transport requested for group '%s', but this ESP-IDF "
           "target does not report SOC_LCD_I80_SUPPORTED.",
           this->parallel_group_.c_str());
  return false;
#endif
}

void CFXLightOutput::deinit_parallel_backend_() {
  auto &g_parallel_group = *parallel_group_for_output_(this);
#if defined(CONFIG_IDF_TARGET_ESP32)
  for (uint8_t i = 0; i < PARALLEL_TX_BUFFER_COUNT; i++) {
    if (g_parallel_group.frame_bufs[i] != nullptr) {
      free(g_parallel_group.frame_bufs[i]);
      g_parallel_group.frame_bufs[i] = nullptr;
    }
  }
  g_parallel_group.frame_buf = nullptr;
  if (g_parallel_group.classic_descs != nullptr) {
    free(g_parallel_group.classic_descs);
    g_parallel_group.classic_descs = nullptr;
  }
  if (g_parallel_group.classic_silence_buf != nullptr) {
    free(g_parallel_group.classic_silence_buf);
    g_parallel_group.classic_silence_buf = nullptr;
  }
  if (g_parallel_group.classic_reset_buf != nullptr) {
    free(g_parallel_group.classic_reset_buf);
    g_parallel_group.classic_reset_buf = nullptr;
  }
#elif defined(CFX_PARALLEL_I80_ENABLED) && SOC_LCD_I80_SUPPORTED
  for (uint8_t i = 0; i < PARALLEL_TX_BUFFER_COUNT; i++) {
    if (g_parallel_i80.frame_bufs[i] != nullptr) {
      free(g_parallel_i80.frame_bufs[i]);
      g_parallel_i80.frame_bufs[i] = nullptr;
    }
  }
  g_parallel_i80.frame_buf = nullptr;
  if (g_parallel_i80.io != nullptr) {
    esp_lcd_panel_io_del(g_parallel_i80.io);
    g_parallel_i80.io = nullptr;
  }
  if (g_parallel_i80.bus != nullptr) {
    esp_lcd_del_i80_bus(g_parallel_i80.bus);
    g_parallel_i80.bus = nullptr;
  }
  g_parallel_i80.ready = false;
  g_parallel_i80.init_attempted = false;
  g_parallel_i80.active_group = nullptr;
  g_parallel_i80.active_group_mask = 0;
  g_parallel_i80.pending_group_mask = 0;
  g_parallel_i80.pending_group_first_ms = 0;
  g_parallel_i80.combined_tx_count = 0;
  g_parallel_i80.tx_in_flight_count = 0;
  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    auto &group = g_parallel_groups[gi];
    group.bus = nullptr;
    group.io = nullptr;
    group.frame_buf = nullptr;
    for (uint8_t i = 0; i < PARALLEL_TX_BUFFER_COUNT; i++) {
      group.frame_bufs[i] = nullptr;
    }
    group.init_attempted = false;
    group.ready = false;
    group.tx_in_flight = false;
    group.tx_in_flight_count = 0;
  }
#endif

  g_parallel_group.init_attempted = false;
  g_parallel_group.ready = false;

  portENTER_CRITICAL(&g_parallel_mux);
  g_parallel_group.tx_in_flight = false;
  g_parallel_group.tx_in_flight_count = 0;
  portEXIT_CRITICAL(&g_parallel_mux);
}

bool CFXLightOutput::force_parallel_shutdown_blackout_() {
  auto &g_parallel_group = *parallel_group_for_output_(this);
  if (!this->is_parallel_transport() || !g_parallel_group.configured) {
    return false;
  }
  if (g_parallel_group.shutdown_blackout_sent) {
    return true;
  }
  g_parallel_group.shutdown_blackout_sent = true;

  uint16_t max_leds = 0;
  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *output = g_parallel_group.outputs[lane];
    if (output == nullptr) {
      continue;
    }
    max_leds = std::max<uint16_t>(max_leds, output->num_leds_);
    if (output->buf_ != nullptr) {
      memset(output->buf_, 0, output->get_buffer_size_());
    }
  }
  if (max_leds == 0) {
    ESP_LOGI(TAG, "Parallel shutdown blackout skipped: group='%s' has no LEDs",
             g_parallel_group.name.c_str());
    return false;
  }

  g_parallel_group.pending_mask = 0;
  g_parallel_group.pending_first_ms = 0;

  if (!g_parallel_group.ready && !this->init_parallel_backend_()) {
    ESP_LOGW(TAG,
             "Parallel shutdown blackout failed: group='%s' backend not ready",
             g_parallel_group.name.c_str());
    return false;
  }

  const uint32_t tx_before = g_parallel_group.tx_count;
  g_parallel_group.force_full_span_next = true;
  this->flush_parallel_();
  const bool queued = g_parallel_group.tx_count != tx_before;
  if (!queued) {
    g_parallel_group.force_full_span_next = false;
  }

  uint32_t wait_us = 0;
  const bool completed =
      queued && wait_for_parallel_group_idle_(80000u, &g_parallel_group,
                                              &wait_us);
  const uint16_t tx_leds =
      g_parallel_group.max_leds > 0 ? g_parallel_group.max_leds : max_leds;
  const uint32_t tx_bytes =
      static_cast<uint32_t>(tx_leds) * this->get_pixel_stride_() * 8u *
          PARALLEL_SYMBOL_SAMPLES +
      static_cast<uint32_t>(PARALLEL_RESET_SAMPLES);

  ESP_LOGI(TAG,
           "Parallel shutdown blackout: group='%s' tx_leds=%u tx_bytes=%" PRIu32
           " queued=%u completed=%u wait=%" PRIu32 "us",
           g_parallel_group.name.c_str(), static_cast<unsigned>(tx_leds),
           tx_bytes, queued ? 1u : 0u, completed ? 1u : 0u, wait_us);
  if (queued && !completed) {
    this->status_set_warning();
  }
  return queued && completed;
}

bool CFXLightOutput::build_parallel_frame_(uint8_t *dest, size_t len,
                                           uint16_t start_led,
                                           uint16_t led_count,
                                           bool include_reset) {
  auto &g_parallel_group = *parallel_group_for_output_(this);
  const uint8_t stride = this->get_pixel_stride_();
  const size_t needed =
      static_cast<size_t>(led_count) * stride * 8u * PARALLEL_SYMBOL_SAMPLES +
      (include_reset ? PARALLEL_RESET_SAMPLES : 0u);
  const size_t led_data_size =
      static_cast<size_t>(led_count) * stride * 8u * PARALLEL_SYMBOL_SAMPLES;
  if (!g_parallel_group.ready || dest == nullptr || len < needed ||
      start_led >= g_parallel_group.max_leds || led_count == 0 ||
      static_cast<uint32_t>(start_led) + led_count > g_parallel_group.max_leds) {
    return false;
  }

  if (include_reset && PARALLEL_RESET_SAMPLES > 0) {
    memset(dest + led_data_size, 0, PARALLEL_RESET_SAMPLES);
  }
  if (len >= needed + PARALLEL_CANARY_BYTES) {
    memset(dest + needed, PARALLEL_CANARY_VALUE, PARALLEL_CANARY_BYTES);
  }
  uint8_t *lane_bufs[PARALLEL_MAX_LANES] = {};
  uint8_t lane_scales[PARALLEL_MAX_LANES] = {};
  uint8_t active_lane_mask = 0;
  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *lane_output = g_parallel_group.outputs[lane];
    if (lane_output != nullptr && lane_output->buf_ != nullptr) {
      lane_bufs[lane] = lane_output->buf_;
      lane_scales[lane] = lane_output->get_power_transmit_scale_();
      active_lane_mask |=
          static_cast<uint8_t>(1u << (g_parallel_group.bit_offset + lane));
    }
  }
  uint8_t *out = dest;
  uint8_t *const data_end = dest + led_data_size;

#if defined(CONFIG_IDF_TARGET_ESP32)
  static const uint8_t SAMPLE_OFFSET_MAP[4] = {2, 3, 0, 1};
#endif

  for (uint16_t offset = 0; offset < led_count; offset++) {
    const uint16_t led = start_led + offset;
    for (uint8_t byte_index = 0; byte_index < stride; byte_index++) {
      if (out + (8u * PARALLEL_SYMBOL_SAMPLES) > data_end) {
        ESP_LOGE(TAG,
                 "Parallel frame writer overflow guard tripped "
                 "(start=%u leds=%u data=%u total=%u)",
                 start_led, led_count, static_cast<unsigned>(led_data_size),
                 static_cast<unsigned>(needed));
        return false;
      }
      uint8_t *const symbol_out = out;
      const size_t byte_offset = static_cast<size_t>(led) * stride + byte_index;
      uint8_t lane_value0 = 0;
      uint8_t lane_value1 = 0;
      uint8_t lane_value2 = 0;
      uint8_t lane_value3 = 0;
      const uint8_t lane_bit0 =
          static_cast<uint8_t>(1u << (g_parallel_group.bit_offset + 0u));
      const uint8_t lane_bit1 =
          static_cast<uint8_t>(1u << (g_parallel_group.bit_offset + 1u));
      const uint8_t lane_bit2 =
          static_cast<uint8_t>(1u << (g_parallel_group.bit_offset + 2u));
      const uint8_t lane_bit3 =
          static_cast<uint8_t>(1u << (g_parallel_group.bit_offset + 3u));
      if (lane_bufs[0] != nullptr && led < g_parallel_group.outputs[0]->num_leds_) {
        lane_value0 = lane_bufs[0][byte_offset];
        if (lane_scales[0] < 255) {
          lane_value0 = static_cast<uint8_t>(
              (static_cast<uint16_t>(lane_value0) * lane_scales[0]) / 255u);
        }
      }
      if (lane_bufs[1] != nullptr && led < g_parallel_group.outputs[1]->num_leds_) {
        lane_value1 = lane_bufs[1][byte_offset];
        if (lane_scales[1] < 255) {
          lane_value1 = static_cast<uint8_t>(
              (static_cast<uint16_t>(lane_value1) * lane_scales[1]) / 255u);
        }
      }
      if (lane_bufs[2] != nullptr && led < g_parallel_group.outputs[2]->num_leds_) {
        lane_value2 = lane_bufs[2][byte_offset];
        if (lane_scales[2] < 255) {
          lane_value2 = static_cast<uint8_t>(
              (static_cast<uint16_t>(lane_value2) * lane_scales[2]) / 255u);
        }
      }
      if (lane_bufs[3] != nullptr && led < g_parallel_group.outputs[3]->num_leds_) {
        lane_value3 = lane_bufs[3][byte_offset];
        if (lane_scales[3] < 255) {
          lane_value3 = static_cast<uint8_t>(
              (static_cast<uint16_t>(lane_value3) * lane_scales[3]) / 255u);
        }
      }
      for (uint8_t bit = 0; bit < 8; bit++) {
        uint8_t one_mask = 0;
        const uint8_t bit_mask = static_cast<uint8_t>(0x80u >> bit);
        if ((lane_value0 & bit_mask) != 0) {
          one_mask |= lane_bit0;
        }
        if ((lane_value1 & bit_mask) != 0) {
          one_mask |= lane_bit1;
        }
        if ((lane_value2 & bit_mask) != 0) {
          one_mask |= lane_bit2;
        }
        if ((lane_value3 & bit_mask) != 0) {
          one_mask |= lane_bit3;
        }
        const uint8_t sample_base =
            static_cast<uint8_t>(bit * PARALLEL_SYMBOL_SAMPLES);
#if defined(CONFIG_IDF_TARGET_ESP32)
        symbol_out[(sample_base & 0xFCu) | SAMPLE_OFFSET_MAP[sample_base & 0x03u]] =
            active_lane_mask;
        symbol_out[((sample_base + 1u) & 0xFCu) |
                   SAMPLE_OFFSET_MAP[(sample_base + 1u) & 0x03u]] = one_mask;
        symbol_out[((sample_base + 2u) & 0xFCu) |
                   SAMPLE_OFFSET_MAP[(sample_base + 2u) & 0x03u]] = 0;
#else
        symbol_out[sample_base] = active_lane_mask;
        symbol_out[sample_base + 1u] = one_mask;
        symbol_out[sample_base + 2u] = 0;
#endif
      }
      out += 8u * PARALLEL_SYMBOL_SAMPLES;
    }
  }

  if (out != data_end) {
    ESP_LOGE(TAG,
             "Parallel frame writer size mismatch (wrote=%u data=%u total=%u)",
             static_cast<unsigned>(out - dest),
             static_cast<unsigned>(led_data_size), static_cast<unsigned>(needed));
    return false;
  }
  if (len >= needed + PARALLEL_CANARY_BYTES) {
    for (size_t i = 0; i < PARALLEL_CANARY_BYTES; i++) {
      if (dest[needed + i] != PARALLEL_CANARY_VALUE) {
        ESP_LOGE(TAG,
                 "Parallel frame canary corrupted while building chunk "
                 "(start=%u leds=%u at=%u)",
                 start_led, led_count, static_cast<unsigned>(i));
        return false;
      }
    }
  }
  return true;
}

bool CFXLightOutput::build_parallel_shared_frame_(
    uint8_t *dest, size_t len, uint32_t start_byte_slot,
    uint32_t byte_slot_count, const uint32_t *group_tx_byte_slots,
    bool include_reset) {
  const size_t led_data_size =
      static_cast<size_t>(byte_slot_count) * 8u * PARALLEL_SYMBOL_SAMPLES;
  const size_t needed =
      led_data_size + (include_reset ? PARALLEL_RESET_SAMPLES : 0u);
  if (dest == nullptr || group_tx_byte_slots == nullptr || byte_slot_count == 0 ||
      len < needed) {
    return false;
  }

  if (include_reset && PARALLEL_RESET_SAMPLES > 0) {
    memset(dest + led_data_size, 0, PARALLEL_RESET_SAMPLES);
  }
  if (len >= needed + PARALLEL_CANARY_BYTES) {
    memset(dest + needed, PARALLEL_CANARY_VALUE, PARALLEL_CANARY_BYTES);
  }

  uint8_t *out = dest;
  uint8_t *const data_end = dest + led_data_size;

#if defined(CONFIG_IDF_TARGET_ESP32)
  static const uint8_t SAMPLE_OFFSET_MAP[4] = {2, 3, 0, 1};
#endif

  struct SharedBuildGroup {
    bool active{false};
    uint8_t stride{0};
    uint8_t lane_count{0};
    uint8_t lane_mask{0};
    uint32_t tx_byte_slots{0};
    CFXLightOutput *outputs[PARALLEL_MAX_LANES]{};
    uint32_t byte_limits[PARALLEL_MAX_LANES]{};
    uint8_t bus_lanes[PARALLEL_MAX_LANES]{};
    uint8_t scales[PARALLEL_MAX_LANES]{};
  };

  SharedBuildGroup build_groups[PARALLEL_MAX_GROUPS] = {};
  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    auto &group = g_parallel_groups[gi];
    auto &build_group = build_groups[gi];
    if (!group.configured || group_tx_byte_slots[gi] == 0) {
      continue;
    }
    for (uint8_t lane = 0; lane < group.lane_count; lane++) {
      auto *lane_output = group.outputs[lane];
      if (lane_output != nullptr) {
        build_group.stride = lane_output->get_pixel_stride_();
        break;
      }
    }
    if (build_group.stride == 0) {
      continue;
    }
    build_group.active = true;
    build_group.lane_count = group.lane_count;
    build_group.tx_byte_slots = group_tx_byte_slots[gi];
    for (uint8_t lane = 0; lane < group.lane_count; lane++) {
      auto *lane_output = group.outputs[lane];
      const uint8_t bus_lane = static_cast<uint8_t>(group.bit_offset + lane);
      if (lane_output == nullptr || bus_lane >= PARALLEL_I80_BUS_WIDTH) {
        continue;
      }
      build_group.outputs[lane] = lane_output;
      build_group.byte_limits[lane] =
          static_cast<uint32_t>(lane_output->num_leds_) * build_group.stride;
      build_group.bus_lanes[lane] = bus_lane;
      build_group.scales[lane] = lane_output->get_power_transmit_scale_();
      build_group.lane_mask =
          static_cast<uint8_t>(build_group.lane_mask | (1u << bus_lane));
    }
    if (build_group.lane_mask == 0) {
      build_group.active = false;
    }
  }

  for (uint32_t offset = 0; offset < byte_slot_count; offset++) {
    const uint32_t byte_slot = start_byte_slot + offset;
    if (out + (8u * PARALLEL_SYMBOL_SAMPLES) > data_end) {
      ESP_LOGE(TAG,
               "Parallel shared frame writer overflow guard tripped "
               "(start_slot=%" PRIu32 " slots=%" PRIu32 " data=%u total=%u)",
               start_byte_slot, byte_slot_count,
               static_cast<unsigned>(led_data_size),
               static_cast<unsigned>(needed));
      return false;
    }

    uint8_t active_lane_mask = 0;
    uint8_t lane_values[PARALLEL_I80_BUS_WIDTH] = {};

    for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
      auto &build_group = build_groups[gi];
      if (!build_group.active || byte_slot >= build_group.tx_byte_slots) {
        continue;
      }
      active_lane_mask =
          static_cast<uint8_t>(active_lane_mask | build_group.lane_mask);
      for (uint8_t lane = 0; lane < build_group.lane_count; lane++) {
        auto *lane_output = build_group.outputs[lane];
        if (lane_output == nullptr || lane_output->buf_ == nullptr ||
            byte_slot >= build_group.byte_limits[lane]) {
          continue;
        }
        uint8_t lane_value = lane_output->buf_[byte_slot];
        const uint8_t scale = build_group.scales[lane];
        if (scale < 255) {
          lane_value = static_cast<uint8_t>(
              (static_cast<uint16_t>(lane_value) * scale) / 255u);
        }
        lane_values[build_group.bus_lanes[lane]] = lane_value;
      }
    }

    uint8_t *const symbol_out = out;
    for (uint8_t bit = 0; bit < 8; bit++) {
      uint8_t one_mask = 0;
      const uint8_t bit_mask = static_cast<uint8_t>(0x80u >> bit);
      if ((lane_values[0] & bit_mask) != 0) {
        one_mask |= 0x01u;
      }
      if ((lane_values[1] & bit_mask) != 0) {
        one_mask |= 0x02u;
      }
      if ((lane_values[2] & bit_mask) != 0) {
        one_mask |= 0x04u;
      }
      if ((lane_values[3] & bit_mask) != 0) {
        one_mask |= 0x08u;
      }
      if ((lane_values[4] & bit_mask) != 0) {
        one_mask |= 0x10u;
      }
      if ((lane_values[5] & bit_mask) != 0) {
        one_mask |= 0x20u;
      }
      if ((lane_values[6] & bit_mask) != 0) {
        one_mask |= 0x40u;
      }
      if ((lane_values[7] & bit_mask) != 0) {
        one_mask |= 0x80u;
      }
      const uint8_t sample_base =
          static_cast<uint8_t>(bit * PARALLEL_SYMBOL_SAMPLES);
#if defined(CONFIG_IDF_TARGET_ESP32)
      symbol_out[(sample_base & 0xFCu) | SAMPLE_OFFSET_MAP[sample_base & 0x03u]] =
          active_lane_mask;
      symbol_out[((sample_base + 1u) & 0xFCu) |
                 SAMPLE_OFFSET_MAP[(sample_base + 1u) & 0x03u]] = one_mask;
      symbol_out[((sample_base + 2u) & 0xFCu) |
                 SAMPLE_OFFSET_MAP[(sample_base + 2u) & 0x03u]] = 0;
#else
      symbol_out[sample_base] = active_lane_mask;
      symbol_out[sample_base + 1u] = one_mask;
      symbol_out[sample_base + 2u] = 0;
#endif
    }
    out += 8u * PARALLEL_SYMBOL_SAMPLES;
  }

  if (out != data_end) {
    ESP_LOGE(TAG,
             "Parallel shared frame writer size mismatch "
             "(wrote=%u data=%u total=%u)",
             static_cast<unsigned>(out - dest),
             static_cast<unsigned>(led_data_size),
             static_cast<unsigned>(needed));
    return false;
  }
  if (len >= needed + PARALLEL_CANARY_BYTES) {
    for (size_t i = 0; i < PARALLEL_CANARY_BYTES; i++) {
      if (dest[needed + i] != PARALLEL_CANARY_VALUE) {
        ESP_LOGE(TAG,
                 "Parallel shared frame canary corrupted while building chunk "
                 "(start_slot=%" PRIu32 " slots=%" PRIu32 " at=%u)",
                 start_byte_slot, byte_slot_count, static_cast<unsigned>(i));
        return false;
      }
    }
  }
  return true;
}

bool CFXLightOutput::flush_parallel_shared_groups_(uint8_t group_mask) {
#if !defined(CONFIG_IDF_TARGET_ESP32) && defined(CFX_PARALLEL_I80_ENABLED)
  group_mask = static_cast<uint8_t>(group_mask & ((1u << PARALLEL_MAX_GROUPS) - 1u));
  const bool shared_whole_mode = parallel_shared_whole_group_mode_enabled_();
  const bool shared_segment_mode =
      parallel_shared_segment_group_mode_enabled_();
  if (group_mask == 0 || (!shared_whole_mode && !shared_segment_mode)) {
    return false;
  }

  const uint32_t flush_start_us = micros();
  uint32_t tx_wait_us = 0;
  uint32_t build_us = 0;
  uint32_t queue_us = 0;

  auto wait_for_shared_tx_count = [&](uint8_t max_in_flight,
                                      uint32_t timeout_us) -> bool {
    const uint32_t wait_start_us = micros();
    uint32_t wdt_feed_us = wait_start_us;
    while (g_parallel_i80.tx_in_flight_count > max_in_flight) {
      const uint32_t now_us = micros();
      if ((now_us - wait_start_us) > timeout_us) {
        tx_wait_us += now_us - wait_start_us;
        for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
          if ((group_mask & static_cast<uint8_t>(1u << gi)) != 0) {
            g_parallel_groups[gi].wait_timeout_count++;
          }
        }
        ESP_LOGW(TAG,
                 "Parallel shared TX wait timeout "
                 "(mask=0x%02x in_flight=%u max=%u)",
                 group_mask,
                 static_cast<unsigned>(g_parallel_i80.tx_in_flight_count),
                 static_cast<unsigned>(max_in_flight));
        this->status_set_warning();
        this->deinit_parallel_backend_();
        return false;
      }
      esp_rom_delay_us(50);
      if ((micros() - wdt_feed_us) >= 1000u) {
        esphome::App.feed_wdt();
        wdt_feed_us = micros();
      }
    }
    tx_wait_us += micros() - wait_start_us;
    return true;
  };

  if (!wait_for_shared_tx_count(g_parallel_i80.buffer_count - 1u, 100000u)) {
    return false;
  }

  uint16_t active_required_leds[PARALLEL_MAX_GROUPS] = {};
  uint16_t tx_leds[PARALLEL_MAX_GROUPS] = {};
  uint8_t group_stride[PARALLEL_MAX_GROUPS] = {};
  uint32_t group_tx_byte_slots[PARALLEL_MAX_GROUPS] = {};
  bool tail_clear_frame[PARALLEL_MAX_GROUPS] = {};
  uint32_t max_byte_slots = 0;

  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    if ((group_mask & static_cast<uint8_t>(1u << gi)) == 0) {
      continue;
    }
    auto &group = g_parallel_groups[gi];
    if (!group.configured ||
        (group.has_segments && !shared_segment_mode) ||
        (!group.has_segments && !shared_whole_mode)) {
      continue;
    }
    for (uint8_t lane = 0; lane < group.lane_count; lane++) {
      auto *lane_output = group.outputs[lane];
      if (lane_output == nullptr) {
        continue;
      }
      lane_output->record_parallel_completed_led_frames_();
      lane_output->scrub_inactive_segments_();
      if (group_stride[gi] == 0) {
        group_stride[gi] = lane_output->get_pixel_stride_();
      }
      const uint16_t lane_required =
          lane_output->get_parallel_required_led_count_();
      active_required_leds[gi] =
          std::max<uint16_t>(active_required_leds[gi], lane_required);
      if (lane_required == 0 && lane_output->buf_ != nullptr) {
        memset(lane_output->buf_, 0, lane_output->get_buffer_size_());
      }
    }
    if (group_stride[gi] == 0) {
      continue;
    }
    if (active_required_leds[gi] > group.max_leds) {
      active_required_leds[gi] = group.max_leds;
    }
    if (group.force_full_span_next && group.max_leds > 0) {
      active_required_leds[gi] = group.max_leds;
    }
    group.force_full_span_next = false;

    tx_leds[gi] = active_required_leds[gi];
    tail_clear_frame[gi] = group.last_tx_leds > tx_leds[gi];
    if (tail_clear_frame[gi]) {
      tx_leds[gi] = group.last_tx_leds;
    }
    if (tx_leds[gi] > group.max_leds) {
      tx_leds[gi] = group.max_leds;
    }
    if (tx_leds[gi] == 0 && group.max_leds > 0) {
      tx_leds[gi] = 1;
    }
    group_tx_byte_slots[gi] =
        static_cast<uint32_t>(tx_leds[gi]) * group_stride[gi];
    max_byte_slots = std::max<uint32_t>(max_byte_slots,
                                        group_tx_byte_slots[gi]);
  }

  if (max_byte_slots == 0) {
    return false;
  }

  const uint32_t bytes_per_slot = 8u * PARALLEL_SYMBOL_SAMPLES;
  if (g_parallel_i80.bus_max_transfer_size <= PARALLEL_RESET_SAMPLES) {
    ESP_LOGE(TAG, "Parallel shared geometry invalid: bus_max=%u reset=%u",
             static_cast<unsigned>(g_parallel_i80.bus_max_transfer_size),
             static_cast<unsigned>(PARALLEL_RESET_SAMPLES));
    this->status_set_warning();
    return false;
  }
  const uint32_t chunk_byte_slots =
      static_cast<uint32_t>((g_parallel_i80.bus_max_transfer_size -
                             PARALLEL_RESET_SAMPLES) /
                            bytes_per_slot);
  if (chunk_byte_slots == 0) {
    ESP_LOGE(TAG, "Parallel shared chunk has zero byte slots");
    this->status_set_warning();
    return false;
  }

  const bool parallel_diag_log_due =
      g_parallel_i80.combined_tx_count < 12u ||
      ((g_parallel_i80.combined_tx_count + 1u) % 120u) == 0u;
  uint32_t chunks_this_frame = 0;
  uint32_t tx_bytes_this_frame = 0;
  uint8_t last_buffer_index = 0;

  for (uint32_t byte_start = 0; byte_start < max_byte_slots;) {
    const uint32_t remaining = max_byte_slots - byte_start;
    const uint32_t slots_this_chunk =
        std::min<uint32_t>(remaining, chunk_byte_slots);
    const bool final_chunk = (byte_start + slots_this_chunk) >= max_byte_slots;
    const size_t chunk_size =
        static_cast<size_t>(slots_this_chunk) * bytes_per_slot +
        (final_chunk ? PARALLEL_RESET_SAMPLES : 0u);
    if (chunk_size > g_parallel_i80.bus_max_transfer_size ||
        chunk_size + PARALLEL_CANARY_BYTES > g_parallel_i80.chunk_alloc_size) {
      ESP_LOGE(TAG,
               "Parallel shared chunk geometry invalid "
               "(chunk_size=%u max=%u alloc=%u slots=%" PRIu32 ")",
               static_cast<unsigned>(chunk_size),
               static_cast<unsigned>(g_parallel_i80.bus_max_transfer_size),
               static_cast<unsigned>(g_parallel_i80.chunk_alloc_size),
               slots_this_chunk);
      this->status_set_warning();
      return false;
    }

    if (!wait_for_shared_tx_count(g_parallel_i80.buffer_count - 1u,
                                  100000u)) {
      return false;
    }

    const uint8_t buffer_index = static_cast<uint8_t>(
        (g_parallel_i80.combined_tx_count + chunks_this_frame) %
        g_parallel_i80.buffer_count);
    uint8_t *const frame_buf = g_parallel_i80.frame_bufs[buffer_index];
    if (frame_buf == nullptr) {
      ESP_LOGE(TAG,
               "Parallel shared frame buffer %u is unavailable "
               "(mask=0x%02x)",
               static_cast<unsigned>(buffer_index), group_mask);
      this->status_set_warning();
      return false;
    }

    const uint32_t build_start_us = micros();
    if (!this->build_parallel_shared_frame_(
            frame_buf, g_parallel_i80.chunk_alloc_size, byte_start,
            slots_this_chunk, group_tx_byte_slots, final_chunk)) {
      ESP_LOGE(TAG,
               "Parallel shared frame build failed "
               "(mask=0x%02x start_slot=%" PRIu32 " slots=%" PRIu32 ")",
               group_mask, byte_start, slots_this_chunk);
      this->status_set_warning();
      return false;
    }
    build_us += micros() - build_start_us;

    for (size_t i = 0; i < PARALLEL_CANARY_BYTES; i++) {
      if (frame_buf[chunk_size + i] != PARALLEL_CANARY_VALUE) {
        ESP_LOGE(TAG,
                 "Parallel shared DMA buffer canary corrupted before TX "
                 "(chunk=%" PRIu32 " at=%u)",
                 chunks_this_frame, static_cast<unsigned>(i));
        this->status_set_warning();
        return false;
      }
    }

    CFXParallelGroupRuntime *primary_group = nullptr;
    for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
      if ((group_mask & static_cast<uint8_t>(1u << gi)) != 0) {
        primary_group = &g_parallel_groups[gi];
        break;
      }
    }

    portENTER_CRITICAL(&g_parallel_mux);
    g_parallel_i80.active_group = primary_group;
    g_parallel_i80.active_group_mask = group_mask;
    g_parallel_i80.tx_in_flight_count++;
    for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
      if ((group_mask & static_cast<uint8_t>(1u << gi)) == 0) {
        continue;
      }
      auto &group = g_parallel_groups[gi];
      group.tx_in_flight = true;
      group.tx_in_flight_count++;
    }
    portEXIT_CRITICAL(&g_parallel_mux);

    const uint32_t queue_start_us = micros();
    esp_err_t err = esp_lcd_panel_io_tx_color(g_parallel_i80.io, 0,
                                              frame_buf, chunk_size);
    queue_us += micros() - queue_start_us;
    if (err != ESP_OK) {
      portENTER_CRITICAL(&g_parallel_mux);
      if (g_parallel_i80.tx_in_flight_count > 0) {
        g_parallel_i80.tx_in_flight_count--;
      }
      if (g_parallel_i80.tx_in_flight_count == 0) {
        g_parallel_i80.active_group_mask = 0;
      }
      for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
        if ((group_mask & static_cast<uint8_t>(1u << gi)) == 0) {
          continue;
        }
        auto &group = g_parallel_groups[gi];
        if (group.tx_in_flight_count > 0) {
          group.tx_in_flight_count--;
        }
        group.tx_in_flight = group.tx_in_flight_count > 0;
        group.queue_error_count++;
      }
      portEXIT_CRITICAL(&g_parallel_mux);
      ESP_LOGE(TAG,
               "Parallel shared TX queue failed "
               "(err=%d, mask=0x%02x, start_slot=%" PRIu32
               " slots=%" PRIu32 ")",
               (int) err, group_mask, byte_start, slots_this_chunk);
      this->status_set_warning();
      return false;
    }

    chunks_this_frame++;
    last_buffer_index = buffer_index;
    tx_bytes_this_frame += static_cast<uint32_t>(chunk_size);
    byte_start += slots_this_chunk;
  }

  g_parallel_i80.combined_tx_count++;
  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    if ((group_mask & static_cast<uint8_t>(1u << gi)) == 0) {
      continue;
    }
    auto &group = g_parallel_groups[gi];
    group.tx_count++;
    group.chunk_tx_count += chunks_this_frame;
    group.last_tx_buffer_index = last_buffer_index;
    group.last_tx_leds = active_required_leds[gi];
  }

  if (parallel_diag_log_due) {
    ESP_LOGI(TAG,
             "Parallel XGroup TX queued: mask=0x%02x tx=%" PRIu32
             " group_tx=[%" PRIu32 ",%" PRIu32 "] "
             "done=[%" PRIu32 ",%" PRIu32 "] tx_bytes=%" PRIu32
             " byte_slots=%" PRIu32 " tx_leds=[%u,%u] active_leds=[%u,%u] "
             "clear_tail=[%u,%u] chunk_slots=%" PRIu32 "/%" PRIu32
             " buf=%u/%u in_flight=%u build+queue=%" PRIu32
             "us build=%" PRIu32 "us wait=%" PRIu32 "us queue=%" PRIu32
             "us timeouts=[%" PRIu32 ",%" PRIu32 "] "
             "queue_errors=[%" PRIu32 ",%" PRIu32 "]",
             group_mask, g_parallel_i80.combined_tx_count,
             g_parallel_groups[0].tx_count, g_parallel_groups[1].tx_count,
             static_cast<uint32_t>(g_parallel_groups[0].tx_done_count),
             static_cast<uint32_t>(g_parallel_groups[1].tx_done_count),
             tx_bytes_this_frame, max_byte_slots, tx_leds[0], tx_leds[1],
             active_required_leds[0], active_required_leds[1],
             tail_clear_frame[0] ? 1u : 0u,
             tail_clear_frame[1] ? 1u : 0u, chunk_byte_slots,
             chunks_this_frame,
             static_cast<unsigned>(last_buffer_index) + 1u,
             static_cast<unsigned>(g_parallel_i80.buffer_count),
             static_cast<unsigned>(g_parallel_i80.tx_in_flight_count),
             micros() - flush_start_us, build_us, tx_wait_us, queue_us,
             g_parallel_groups[0].timeout_flush_count,
             g_parallel_groups[1].timeout_flush_count,
             g_parallel_groups[0].queue_error_count,
             g_parallel_groups[1].queue_error_count);
  }

  this->record_parallel_completed_led_frames_();
  this->perf_diag_flush_count_++;
  this->perf_diag_last_flush_total_us_ = micros() - flush_start_us;
  this->perf_diag_last_flush_tx_us_ = 0;
  this->perf_diag_last_flush_valid_ = true;
  return true;
#else
  return false;
#endif
}

void CFXLightOutput::flush_parallel_() {
  auto &g_parallel_group = *parallel_group_for_output_(this);
  const uint32_t flush_start_us = micros();
  uint32_t tx_wait_us = 0;
  uint32_t build_us = 0;
  uint32_t queue_us = 0;
  this->record_parallel_completed_led_frames_();
  auto wait_for_parallel_tx_count = [&](uint8_t max_in_flight,
                                        uint32_t timeout_us) -> bool {
    const uint32_t wait_start_us = micros();
    uint32_t wdt_feed_us = wait_start_us;
    while (g_parallel_group.tx_in_flight_count > max_in_flight) {
      const uint32_t now_us = micros();
      if ((now_us - wait_start_us) > timeout_us) {
        tx_wait_us += now_us - wait_start_us;
        g_parallel_group.wait_timeout_count++;
        ESP_LOGW(TAG,
                 "Parallel TX wait timeout for group '%s' "
                 "(in_flight=%u max=%u timeouts=%" PRIu32 "). "
                 "Forcefully recovering by deinitializing backend.",
                 g_parallel_group.name.c_str(),
                 static_cast<unsigned>(g_parallel_group.tx_in_flight_count),
                 static_cast<unsigned>(max_in_flight),
                 g_parallel_group.wait_timeout_count);
        this->status_set_warning();
        this->deinit_parallel_backend_();
        g_parallel_group.ready = false;
        return false;
      }
      // Tight spin: 50 µs polling keeps the I80 DMA pipeline continuously fed.
      // vTaskDelay(1) would sleep ≥10 ms (1 FreeRTOS tick @ 100 Hz), which is
      // 125× the 80 µs SK6812 reset threshold and would force a mid-frame reset.
      esp_rom_delay_us(50);
      // Feed WDT at most once per ms to avoid call overhead.
      if ((micros() - wdt_feed_us) >= 1000u) {
        esphome::App.feed_wdt();
        wdt_feed_us = micros();
      }
    }
    tx_wait_us += micros() - wait_start_us;
    return true;
  };

  if (!g_parallel_group.ready) {
    if (!this->init_parallel_backend_()) {
      ESP_LOGE(TAG, "Parallel group '%s' could not initialize backend",
               this->parallel_group_.c_str());
      this->status_set_warning();
      this->mark_failed();
      return;
    }
  }
#if defined(CONFIG_IDF_TARGET_ESP32)
  if (g_parallel_group.classic_descs == nullptr ||
      g_parallel_group.frame_buf == nullptr) {
    ESP_LOGE(TAG, "Parallel group '%s' Classic backend is not ready",
             this->parallel_group_.c_str());
    this->mark_failed();
    return;
  }
#else
  if (g_parallel_group.io == nullptr || g_parallel_group.frame_buf == nullptr) {
    ESP_LOGE(TAG, "Parallel group '%s' is not ready", this->parallel_group_.c_str());
    this->mark_failed();
    return;
  }
#endif

#if defined(CONFIG_IDF_TARGET_ESP32)
  (void) wait_for_parallel_tx_count;
#endif

  const bool force_full_span_frame = g_parallel_group.force_full_span_next;
  g_parallel_group.force_full_span_next = false;

#ifdef CFX_PARALLEL_TEST_PATTERN
  // Phase-1 diag: deterministic test pattern — bypasses effect engine.
  // Writes directly to each lane's buf_ using segment defs.
  // Segment 0 = RED, 1 = GREEN, 2 = BLUE, 3 = WHITE.
  {
    static const esphome::Color seg_colors[4] = {
        esphome::Color(255, 0, 0, 0),    // RED
        esphome::Color(0, 255, 0, 0),    // GREEN
        esphome::Color(0, 0, 255, 0),    // BLUE
        esphome::Color(255, 255, 255, 255),  // WHITE
    };
    for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
      auto *lane_output = g_parallel_group.outputs[lane];
      if (lane_output == nullptr || lane_output->buf_ == nullptr) {
        continue;
      }
      // Zero the entire buffer first.
      const size_t buf_bytes =
          static_cast<size_t>(lane_output->num_leds_) *
          lane_output->get_pixel_stride_();
      memset(lane_output->buf_, 0, buf_bytes);
      // Paint each segment with its test color.
      const size_t seg_count =
          std::min<size_t>(lane_output->segment_defs_.size(), 4);
      for (size_t si = 0; si < seg_count; si++) {
        const auto &def = lane_output->segment_defs_[si];
        for (int p = def.start; p < def.stop && p < lane_output->size(); p++) {
          (*lane_output)[p] = seg_colors[si];
        }
      }
      // Log once per boot cycle.
      if (g_parallel_group.tx_count < 2) {
        ESP_LOGI(TAG,
                 "Test pattern applied: lane %u segs=%u num_leds=%d",
                 lane, static_cast<unsigned>(seg_count),
                 lane_output->size());
      }
    }
  }
#endif

  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *lane_output = g_parallel_group.outputs[lane];
    if (lane_output != nullptr) {
      lane_output->scrub_inactive_segments_();
    }
  }

  uint16_t active_required_leds = 0;
  uint16_t lane_required_leds[PARALLEL_MAX_LANES] = {};
  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *lane_output = g_parallel_group.outputs[lane];
    if (lane_output == nullptr) {
      continue;
    }
    lane_required_leds[lane] = lane_output->get_parallel_required_led_count_();
    active_required_leds =
        std::max<uint16_t>(active_required_leds, lane_required_leds[lane]);
    if (lane_required_leds[lane] == 0 && lane_output->buf_ != nullptr) {
      memset(lane_output->buf_, 0, lane_output->get_buffer_size_());
    }
  }
  if (active_required_leds > g_parallel_group.max_leds) {
    active_required_leds = g_parallel_group.max_leds;
  }
  if (force_full_span_frame && g_parallel_group.max_leds > 0) {
    active_required_leds = g_parallel_group.max_leds;
  }
  uint16_t tx_leds = active_required_leds;
  const bool tail_clear_frame = g_parallel_group.last_tx_leds > tx_leds;
  if (tail_clear_frame) {
    tx_leds = g_parallel_group.last_tx_leds;
  }
  if (tx_leds > g_parallel_group.max_leds) {
    tx_leds = g_parallel_group.max_leds;
  }
  if (tx_leds == 0 && g_parallel_group.max_leds > 0) {
    tx_leds = 1;
  }

  const bool parallel_diag_log_due =
      g_parallel_group.tx_count < 12u ||
      ((g_parallel_group.tx_count + 1u) % 120u) == 0u;
  uint32_t source_nonzero = 0;
  uint32_t lane_nonzero[PARALLEL_MAX_LANES] = {};
  uint32_t lane_segment_nonzero[PARALLEL_MAX_LANES][4] = {};
  uint8_t first_pixel[PARALLEL_MAX_LANES][4] = {};
  uint16_t first_nonzero_led[PARALLEL_MAX_LANES] = {};
  uint8_t first_nonzero_byte[PARALLEL_MAX_LANES] = {};
  uint8_t first_nonzero_value[PARALLEL_MAX_LANES] = {};
  uint8_t encoded_probe0[PARALLEL_MAX_LANES] = {};
  uint8_t encoded_probe1[PARALLEL_MAX_LANES] = {};
  uint8_t diag_probe_buffer_index = 0;
  bool first_nonzero_seen[PARALLEL_MAX_LANES] = {};
  const uint8_t stride = this->get_pixel_stride_();
  if (parallel_diag_log_due) {
    for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
      auto *lane_output = g_parallel_group.outputs[lane];
      if (lane_output == nullptr || lane_output->buf_ == nullptr) {
        continue;
      }
      const uint8_t lane_stride = lane_output->get_pixel_stride_();
      for (uint8_t byte_index = 0; byte_index < lane_stride && byte_index < 4;
           byte_index++) {
        first_pixel[lane][byte_index] = lane_output->buf_[byte_index];
      }
      const size_t lane_leds =
          std::min<size_t>(lane_output->num_leds_, tx_leds);
      const size_t lane_size = lane_leds * lane_stride;
      for (size_t i = 0; i < lane_size; i++) {
        if (lane_output->buf_[i] != 0) {
          source_nonzero++;
          lane_nonzero[lane]++;
          if (!first_nonzero_seen[lane]) {
            first_nonzero_seen[lane] = true;
            first_nonzero_led[lane] =
                static_cast<uint16_t>(i / lane_stride);
            first_nonzero_byte[lane] =
                static_cast<uint8_t>(i % lane_stride);
            first_nonzero_value[lane] = lane_output->buf_[i];
          }
        }
      }
      const size_t segment_buckets =
          std::min<size_t>(lane_output->segment_defs_.size(), 4);
      for (size_t seg = 0; seg < segment_buckets; seg++) {
        const auto &def = lane_output->segment_defs_[seg];
        const size_t start = std::min<size_t>(
            static_cast<size_t>(def.start) * lane_stride, lane_size);
        const size_t stop = std::min<size_t>(
            static_cast<size_t>(def.stop) * lane_stride, lane_size);
        for (size_t i = start; i < stop; i++) {
          if (lane_output->buf_[i] != 0) {
            lane_segment_nonzero[lane][seg]++;
          }
        }
      }
    }
  }

  // Phase-1 diag: capture lane_scales for the TX log.
  uint8_t diag_lane_scales[PARALLEL_MAX_LANES] = {};
  // Phase-1 diag: capture the buf_ value at the exact byte_offset the encoder
  // uses, AFTER scrub but BEFORE frame build - to verify data survives.
  uint8_t diag_buf_at_encode[PARALLEL_MAX_LANES] = {};
  if (parallel_diag_log_due) {
    for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
      auto *lo = g_parallel_group.outputs[lane];
      if (lo != nullptr) {
        diag_lane_scales[lane] = lo->get_power_transmit_scale_();
        if (lo->buf_ != nullptr && first_nonzero_seen[lane]) {
          const size_t bo =
              static_cast<size_t>(first_nonzero_led[lane]) *
                  lo->get_pixel_stride_() +
              first_nonzero_byte[lane];
          const size_t buf_len =
              static_cast<size_t>(lo->num_leds_) * lo->get_pixel_stride_();
          if (bo < buf_len) {
            diag_buf_at_encode[lane] = lo->buf_[bo];
          }
        }
      }
    }
  }

  auto probe_first_encoded_samples = [&]() {
    if (diag_probe_buffer_index >= PARALLEL_TX_BUFFER_COUNT) {
      diag_probe_buffer_index = 0;
    }
    uint8_t *const first_frame =
        g_parallel_group.frame_bufs[diag_probe_buffer_index] != nullptr
            ? g_parallel_group.frame_bufs[diag_probe_buffer_index]
            : g_parallel_group.frame_buf;
    if (first_frame == nullptr) {
      return;
    }
    for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
      if (!first_nonzero_seen[lane] ||
          first_nonzero_led[lane] >= g_parallel_group.chunk_leds) {
        continue;
      }
      const size_t byte_offset =
          (static_cast<size_t>(first_nonzero_led[lane]) * stride +
           first_nonzero_byte[lane]) *
          8u * PARALLEL_SYMBOL_SAMPLES;
      if (byte_offset + 1u < g_parallel_group.chunk_frame_size) {
        encoded_probe0[lane] = first_frame[byte_offset];
        encoded_probe1[lane] = first_frame[byte_offset + 1u];
      }
    }
  };

#if defined(CONFIG_IDF_TARGET_ESP32)
  if (g_parallel_group.classic_sending) {
    ESP_LOGW(TAG, "Parallel Classic frame already in flight for group '%s'",
             g_parallel_group.name.c_str());
    this->status_set_warning();
    return;
  }

  auto *descs = g_parallel_group.classic_descs;
  const uint16_t total_chunks = static_cast<uint16_t>(
      (this->num_leds_ + g_parallel_group.chunk_leds - 1u) /
      g_parallel_group.chunk_leds);
  const uint16_t initial_chunks = static_cast<uint16_t>(
      std::min<uint16_t>(total_chunks, PARALLEL_TX_BUFFER_COUNT));
  uint16_t next_chunk_to_build = 0;

  auto build_classic_chunk = [&](uint16_t chunk_index, uint8_t buffer_index) -> bool {
    const uint16_t led_start =
        static_cast<uint16_t>(chunk_index * g_parallel_group.chunk_leds);
    const uint16_t remaining = this->num_leds_ - led_start;
    const uint16_t chunk_leds = static_cast<uint16_t>(
        std::min<uint16_t>(remaining, g_parallel_group.chunk_leds));
    uint8_t *const frame_buf = g_parallel_group.frame_bufs[buffer_index];
    if (frame_buf == nullptr) {
      ESP_LOGE(TAG,
               "Parallel Classic rolling buffer %u is unavailable for group '%s'",
               static_cast<unsigned>(buffer_index),
               g_parallel_group.name.c_str());
      return false;
    }
    const uint32_t build_start_us = micros();
    if (!this->build_parallel_frame_(frame_buf,
                                     g_parallel_group.chunk_alloc_size,
                                     led_start, chunk_leds, false)) {
      ESP_LOGE(TAG,
               "Parallel Classic chunk build failed for group '%s' "
               "(chunk=%u start=%u leds=%u)",
               g_parallel_group.name.c_str(), chunk_index, led_start, chunk_leds);
      return false;
    }
    build_us += micros() - build_start_us;
    for (size_t i = 0; i < PARALLEL_CANARY_BYTES; i++) {
      if (frame_buf[g_parallel_group.chunk_frame_size + i] !=
          PARALLEL_CANARY_VALUE) {
        ESP_LOGE(TAG,
                 "Parallel Classic DMA buffer canary corrupted before TX "
                 "(chunk=%u at=%u)",
                 chunk_index, static_cast<unsigned>(i));
        return false;
      }
    }
    const size_t chunk_bytes =
        static_cast<size_t>(chunk_leds) * stride * 8u * PARALLEL_SYMBOL_SAMPLES;
    const uint8_t first_desc =
        PARALLEL_CLASSIC_DATA_DESC_BASE +
        buffer_index * PARALLEL_CLASSIC_DESC_PER_BUFFER;
    const size_t first_len = std::min(chunk_bytes, PARALLEL_CLASSIC_DMA_MAX_LEN);
    const size_t second_len = chunk_bytes - first_len;
    if (second_len > PARALLEL_CLASSIC_DMA_MAX_LEN) {
      ESP_LOGE(TAG,
               "Parallel Classic chunk is too large for two DMA descriptors "
               "(chunk=%u bytes=%u)",
               chunk_index, static_cast<unsigned>(chunk_bytes));
      return false;
    }
    if (second_len > 0) {
      parallel_dma_desc_init_(&descs[first_desc], frame_buf, first_len,
                              &descs[first_desc + 1], false);
      parallel_dma_desc_init_(&descs[first_desc + 1], frame_buf + first_len,
                              second_len, &descs[PARALLEL_CLASSIC_RESET_DESC],
                              true);
      g_parallel_group.classic_data_desc_tail[buffer_index] = first_desc + 1;
    } else {
      parallel_dma_desc_init_(&descs[first_desc], frame_buf, first_len,
                              &descs[PARALLEL_CLASSIC_RESET_DESC], true);
      parallel_dma_desc_init_(&descs[first_desc + 1], frame_buf, 4,
                              &descs[PARALLEL_CLASSIC_RESET_DESC], false);
      g_parallel_group.classic_data_desc_tail[buffer_index] = first_desc;
    }
    return true;
  };

  for (; next_chunk_to_build < initial_chunks; next_chunk_to_build++) {
    if (!build_classic_chunk(next_chunk_to_build,
                             next_chunk_to_build % PARALLEL_TX_BUFFER_COUNT)) {
      this->status_set_warning();
      return;
    }
  }

  for (uint8_t i = 0; i < initial_chunks; i++) {
    auto *next = (i + 1u < initial_chunks)
                     ? &descs[PARALLEL_CLASSIC_DATA_DESC_BASE +
                              (i + 1u) * PARALLEL_CLASSIC_DESC_PER_BUFFER]
                     : &descs[PARALLEL_CLASSIC_RESET_DESC];
    descs[g_parallel_group.classic_data_desc_tail[i]].qe.stqe_next = next;
  }

  g_parallel_group.classic_completed_chunks = 0;
  g_parallel_group.classic_expected_chunks = total_chunks;
  g_parallel_group.classic_auto_relink = true;
  g_parallel_group.classic_dma_errors = 0;
  g_parallel_group.classic_sending = true;
  g_parallel_group.tx_in_flight = true;
  g_parallel_group.tx_in_flight_count = 1;
  uint16_t prepared_chunks = initial_chunks;
  uint16_t recycled_chunks = 0;

  const uint32_t queue_start_us = micros();
  descs[PARALLEL_CLASSIC_SILENCE_DESC_B].qe.stqe_next =
      &descs[PARALLEL_CLASSIC_DATA_DESC_BASE];
  queue_us += micros() - queue_start_us;

  bool underrun = false;
  const uint32_t stream_start_us = micros();
  while (g_parallel_group.classic_completed_chunks < total_chunks) {
    if ((micros() - stream_start_us) > 500000u) {
      underrun = true;
      break;
    }
    if (next_chunk_to_build < total_chunks &&
        g_parallel_group.classic_completed_chunks > recycled_chunks) {
      const uint8_t free_index =
          static_cast<uint8_t>(recycled_chunks % PARALLEL_TX_BUFFER_COUNT);
      if (!build_classic_chunk(next_chunk_to_build, free_index)) {
        this->status_set_warning();
        descs[PARALLEL_CLASSIC_SILENCE_DESC_B].qe.stqe_next =
            &descs[PARALLEL_CLASSIC_SILENCE_DESC_A];
        g_parallel_group.classic_sending = false;
        g_parallel_group.classic_auto_relink = false;
        g_parallel_group.classic_expected_chunks = 0;
        g_parallel_group.tx_in_flight = false;
        g_parallel_group.tx_in_flight_count = 0;
        return;
      }
      if (g_parallel_group.classic_completed_chunks >= prepared_chunks) {
        underrun = true;
        break;
      }
      const uint8_t previous_buffer =
          static_cast<uint8_t>((prepared_chunks - 1u) %
                               PARALLEL_TX_BUFFER_COUNT);
      descs[g_parallel_group.classic_data_desc_tail[previous_buffer]]
          .qe.stqe_next =
          &descs[PARALLEL_CLASSIC_DATA_DESC_BASE +
                 free_index * PARALLEL_CLASSIC_DESC_PER_BUFFER];
      descs[g_parallel_group.classic_data_desc_tail[free_index]].qe.stqe_next =
          &descs[PARALLEL_CLASSIC_RESET_DESC];
      prepared_chunks++;
      recycled_chunks++;
      next_chunk_to_build++;
      continue;
    }

    if (next_chunk_to_build < total_chunks &&
        g_parallel_group.classic_completed_chunks >= prepared_chunks) {
      underrun = true;
      break;
    }

    esphome::App.feed_wdt();
    esp_rom_delay_us(50);
  }

  descs[PARALLEL_CLASSIC_SILENCE_DESC_B].qe.stqe_next =
      &descs[PARALLEL_CLASSIC_SILENCE_DESC_A];
  esp_rom_delay_us(150);
  g_parallel_group.classic_sending = false;
  g_parallel_group.classic_auto_relink = false;
  g_parallel_group.classic_expected_chunks = 0;
  g_parallel_group.tx_in_flight = false;
  g_parallel_group.tx_in_flight_count = 0;

  if (underrun || g_parallel_group.classic_dma_errors != 0) {
    g_parallel_group.classic_underrun_count++;
    ESP_LOGE(TAG,
             "Parallel Classic stream underrun for group '%s' "
             "(chunks=%u prepared=%u completed=%" PRIu32
             " dma_errors=%" PRIu32 " underruns=%" PRIu32 ")",
             g_parallel_group.name.c_str(), total_chunks, prepared_chunks,
             g_parallel_group.classic_completed_chunks,
             g_parallel_group.classic_dma_errors,
             g_parallel_group.classic_underrun_count);
    this->status_set_warning();
    return;
  }

  g_parallel_group.tx_count++;
  g_parallel_group.classic_stream_count++;
  if (parallel_diag_log_due) {
    probe_first_encoded_samples();
    ESP_LOGI(TAG,
             "Parallel Classic TX streamed: group='%s' tx=%" PRIu32
             " done=%" PRIu32 " source_nonzero=%" PRIu32
             " lane_nonzero=[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "]"
             " seg_nonzero=[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
             "|%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "|%" PRIu32
             ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "|%" PRIu32 ",%" PRIu32
             ",%" PRIu32 ",%" PRIu32 "]"
             " first=[%02x,%02x,%02x,%02x]/[%02x,%02x,%02x,%02x] "
             "nz=[%u.%u=%02x>%02x/%02x|%u.%u=%02x>%02x/%02x|"
             "%u.%u=%02x>%02x/%02x|%u.%u=%02x>%02x/%02x] "
             "bytes=%u chunk=%u/%u build+stream=%" PRIu32
             "us build=%" PRIu32 "us wait=%" PRIu32 "us queue=%" PRIu32
             "us coalesced=%" PRIu32 " timeouts=%" PRIu32
             " underruns=%" PRIu32 " relinks=%" PRIu32
             " scales=[%u,%u,%u,%u]"
             " enc_buf=[%02x,%02x,%02x,%02x]",
             g_parallel_group.name.c_str(), g_parallel_group.tx_count,
             static_cast<uint32_t>(g_parallel_group.tx_done_count),
             source_nonzero, lane_nonzero[0], lane_nonzero[1],
             lane_nonzero[2], lane_nonzero[3],
             lane_segment_nonzero[0][0], lane_segment_nonzero[0][1],
             lane_segment_nonzero[0][2], lane_segment_nonzero[0][3],
             lane_segment_nonzero[1][0], lane_segment_nonzero[1][1],
             lane_segment_nonzero[1][2], lane_segment_nonzero[1][3],
             lane_segment_nonzero[2][0], lane_segment_nonzero[2][1],
             lane_segment_nonzero[2][2], lane_segment_nonzero[2][3],
             lane_segment_nonzero[3][0], lane_segment_nonzero[3][1],
             lane_segment_nonzero[3][2], lane_segment_nonzero[3][3],
             first_pixel[0][0], first_pixel[0][1],
             first_pixel[0][2], first_pixel[0][3], first_pixel[1][0],
             first_pixel[1][1], first_pixel[1][2], first_pixel[1][3],
             first_nonzero_led[0], first_nonzero_byte[0],
             first_nonzero_value[0], encoded_probe0[0], encoded_probe1[0],
             first_nonzero_led[1], first_nonzero_byte[1],
             first_nonzero_value[1], encoded_probe0[1], encoded_probe1[1],
             first_nonzero_led[2], first_nonzero_byte[2],
             first_nonzero_value[2], encoded_probe0[2], encoded_probe1[2],
             first_nonzero_led[3], first_nonzero_byte[3],
             first_nonzero_value[3], encoded_probe0[3], encoded_probe1[3],
             static_cast<unsigned>(g_parallel_group.frame_size),
             static_cast<unsigned>(g_parallel_group.chunk_frame_size),
             total_chunks, micros() - flush_start_us, build_us, tx_wait_us,
             queue_us, g_parallel_group.coalesced_count,
             g_parallel_group.timeout_flush_count,
             g_parallel_group.classic_underrun_count,
             static_cast<uint32_t>(g_parallel_group.classic_relink_count),
             diag_lane_scales[0], diag_lane_scales[1],
             diag_lane_scales[2], diag_lane_scales[3],
             diag_buf_at_encode[0], diag_buf_at_encode[1],
             diag_buf_at_encode[2], diag_buf_at_encode[3]);
  }
  for (uint8_t lane = 0; lane < g_parallel_group.lane_count; lane++) {
    auto *lane_output = g_parallel_group.outputs[lane];
    if (lane_output != nullptr) {
      lane_output->record_led_frame_();
      if (lane_output->power_manager_ != nullptr) {
        lane_output->power_manager_->record_output_frame(lane_output);
      }
    }
  }
  this->perf_diag_flush_count_++;
  this->perf_diag_last_flush_total_us_ = micros() - flush_start_us;
  this->perf_diag_last_flush_tx_us_ = micros() - flush_start_us - build_us;
  this->perf_diag_last_flush_valid_ = true;
#elif defined(CFX_PARALLEL_I80_ENABLED)
  if (g_parallel_i80.active_group != nullptr &&
      g_parallel_i80.active_group != &g_parallel_group &&
      g_parallel_i80.tx_in_flight_count > 0) {
    uint32_t switch_wait_us = 0;
    if (!wait_for_parallel_group_idle_(100000u, g_parallel_i80.active_group,
                                       &switch_wait_us)) {
      g_parallel_group.wait_timeout_count++;
      ESP_LOGW(TAG,
               "Parallel TX group switch timeout from '%s' to '%s' "
               "(wait=%" PRIu32 "us timeouts=%" PRIu32 ")",
               g_parallel_i80.active_group->name.c_str(),
               g_parallel_group.name.c_str(), switch_wait_us,
               g_parallel_group.wait_timeout_count);
      this->status_set_warning();
      this->deinit_parallel_backend_();
      return;
    }
  }
  g_parallel_i80.active_group = &g_parallel_group;

  const uint16_t group_max_leds = tx_leds;
  uint16_t led_start = 0;
  uint32_t chunks_this_frame = 0;
  uint32_t tx_bytes_this_frame = 0;
  while (led_start < group_max_leds) {
    const uint16_t remaining = group_max_leds - led_start;
    const uint16_t chunk_leds =
        static_cast<uint16_t>(std::min<uint16_t>(remaining,
                                                 g_parallel_group.chunk_leds));
    const bool final_chunk = (led_start + chunk_leds) >= group_max_leds;
    const size_t chunk_size =
        static_cast<size_t>(chunk_leds) * stride * 8u * PARALLEL_SYMBOL_SAMPLES +
        (final_chunk ? PARALLEL_RESET_SAMPLES : 0u);
    if (!wait_for_parallel_tx_count(g_parallel_group.buffer_count - 1, 100000u)) {
      return;
    }
    const uint8_t buffer_index = g_parallel_group.next_tx_buffer_index;
    g_parallel_group.next_tx_buffer_index =
        static_cast<uint8_t>((g_parallel_group.next_tx_buffer_index + 1u) %
                             g_parallel_group.buffer_count);
    if (led_start == 0) {
      diag_probe_buffer_index = buffer_index;
    }
    uint8_t *const frame_buf = g_parallel_group.frame_bufs[buffer_index];
    if (frame_buf == nullptr) {
      ESP_LOGE(TAG,
               "Parallel frame buffer %u is not available for group '%s'",
               static_cast<unsigned>(buffer_index),
               g_parallel_group.name.c_str());
      this->status_set_warning();
      return;
    }

    const uint32_t build_start_us = micros();
    if (!this->build_parallel_frame_(frame_buf,
                                     g_parallel_group.chunk_alloc_size,
                                     led_start, chunk_leds, final_chunk)) {
      ESP_LOGE(TAG,
               "Parallel frame chunk build failed for group '%s' "
               "(start=%u leds=%u final=%s)",
               g_parallel_group.name.c_str(), led_start, chunk_leds,
               final_chunk ? "yes" : "no");
      this->status_set_warning();
      return;
    }
    build_us += micros() - build_start_us;
    if (chunk_size > g_parallel_group.bus_max_transfer_size ||
        chunk_size + PARALLEL_CANARY_BYTES >
            g_parallel_group.chunk_alloc_size) {
      ESP_LOGE(TAG,
               "Parallel chunk geometry invalid "
               "(chunk_size=%u max=%u alloc=%u)",
               static_cast<unsigned>(chunk_size),
               static_cast<unsigned>(g_parallel_group.bus_max_transfer_size),
               static_cast<unsigned>(g_parallel_group.chunk_alloc_size));
      this->status_set_warning();
      return;
    }
    for (size_t i = 0; i < PARALLEL_CANARY_BYTES; i++) {
      if (frame_buf[chunk_size + i] !=
          PARALLEL_CANARY_VALUE) {
        ESP_LOGE(TAG,
                 "Parallel DMA buffer canary corrupted before TX "
                 "(chunk=%" PRIu32 " at=%u)",
                 chunks_this_frame, static_cast<unsigned>(i));
        this->status_set_warning();
        return;
      }
    }

    portENTER_CRITICAL(&g_parallel_mux);
    g_parallel_i80.active_group = &g_parallel_group;
    g_parallel_i80.active_group_mask =
        static_cast<uint8_t>(1u << g_parallel_group.group_index);
    g_parallel_group.tx_in_flight = true;
    g_parallel_group.tx_in_flight_count++;
    g_parallel_i80.tx_in_flight_count++;
    portEXIT_CRITICAL(&g_parallel_mux);
    const uint32_t queue_start_us = micros();
    esp_err_t err = esp_lcd_panel_io_tx_color(g_parallel_group.io, 0,
                                              frame_buf, chunk_size);
    queue_us += micros() - queue_start_us;
    if (err != ESP_OK) {
      portENTER_CRITICAL(&g_parallel_mux);
      if (g_parallel_group.tx_in_flight_count > 0) {
        g_parallel_group.tx_in_flight_count--;
      }
      g_parallel_group.tx_in_flight = g_parallel_group.tx_in_flight_count > 0;
      if (g_parallel_i80.tx_in_flight_count > 0) {
        g_parallel_i80.tx_in_flight_count--;
      }
      if (g_parallel_i80.tx_in_flight_count == 0) {
        g_parallel_i80.active_group_mask = 0;
      }
      portEXIT_CRITICAL(&g_parallel_mux);
      g_parallel_group.queue_error_count++;
      ESP_LOGE(TAG,
               "Parallel TX queue failed for group '%s' "
               "(err=%d, errors=%" PRIu32 ", start=%u leds=%u)",
               g_parallel_group.name.c_str(), (int) err,
               g_parallel_group.queue_error_count, led_start, chunk_leds);
      this->status_set_warning();
      return;
    }

    chunks_this_frame++;
    g_parallel_group.last_tx_buffer_index = buffer_index;
    g_parallel_group.chunk_tx_count++;
    tx_bytes_this_frame += static_cast<uint32_t>(chunk_size);
    led_start += chunk_leds;
  }

  g_parallel_group.tx_count++;
  g_parallel_group.last_tx_leds = active_required_leds;
  if (parallel_diag_log_due) {
    probe_first_encoded_samples();
    ESP_LOGI(TAG,
             "Parallel TX queued: group='%s' tx=%" PRIu32
             " done=%" PRIu32 " nz=%" PRIu32
             " lane_nz=[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "] "
             "tx_bytes=%" PRIu32 " frame_bytes=%u tx_leds=%u active_leds=%u "
             "max_leds=%u clear_tail=%u "
             "chunk=%u/%" PRIu32 " buf=%u/%u in_flight=%u build+queue=%" PRIu32
             "us build=%" PRIu32 "us wait=%" PRIu32 "us queue=%" PRIu32
             "us coalesced=%" PRIu32 " timeouts=%" PRIu32
             " queue_errors=%" PRIu32
             " scales=[%u,%u,%u,%u]"
             " enc_buf=[%02x,%02x,%02x,%02x]",
             g_parallel_group.name.c_str(), g_parallel_group.tx_count,
             static_cast<uint32_t>(g_parallel_group.tx_done_count),
             source_nonzero, lane_nonzero[0], lane_nonzero[1],
             lane_nonzero[2], lane_nonzero[3],
             tx_bytes_this_frame,
             static_cast<unsigned>(g_parallel_group.frame_size),
             tx_leds, active_required_leds, g_parallel_group.max_leds,
             tail_clear_frame ? 1u : 0u,
             static_cast<unsigned>(g_parallel_group.chunk_frame_size),
             chunks_this_frame,
             static_cast<unsigned>(g_parallel_group.last_tx_buffer_index) + 1u,
             static_cast<unsigned>(g_parallel_group.buffer_count),
             static_cast<unsigned>(g_parallel_group.tx_in_flight_count),
             micros() - flush_start_us,
             build_us, tx_wait_us, queue_us, g_parallel_group.coalesced_count,
             g_parallel_group.timeout_flush_count,
             g_parallel_group.queue_error_count,
             diag_lane_scales[0], diag_lane_scales[1],
             diag_lane_scales[2], diag_lane_scales[3],
             diag_buf_at_encode[0], diag_buf_at_encode[1],
             diag_buf_at_encode[2], diag_buf_at_encode[3]);
  }
  this->record_parallel_completed_led_frames_();
  this->perf_diag_flush_count_++;
  this->perf_diag_last_flush_total_us_ = micros() - flush_start_us;
  this->perf_diag_last_flush_tx_us_ = 0;
  this->perf_diag_last_flush_valid_ = true;
#else
  ESP_LOGE(TAG,
           "Parallel I80 backend is not compiled into this boot-safe "
           "diagnostic build (group '%s').",
           this->parallel_group_.c_str());
  this->status_set_warning();
  this->mark_failed();
#endif
}

static bool has_active_rendering_cfx_effect(CFXLightOutput *output) {
  if (output == nullptr || output->get_master_light_state() == nullptr ||
      !output->get_master_light_state()->remote_values.is_on()) {
    return false;
  }
  auto *master_effect = resolve_active_cfx_effect(output->get_master_light_state());
  if (master_effect != nullptr && !master_effect->is_clean_mono_idle_output()) {
    return true;
  }
  for (auto *seg_state : output->get_segment_light_states()) {
    auto *effect = resolve_active_cfx_effect(seg_state);
    if (effect != nullptr && !effect->is_clean_mono_idle_output()) {
      return true;
    }
  }
  return false;
}

static uint8_t active_parallel_lanes_mask_(CFXParallelGroupRuntime &group,
                                           uint8_t fallback_mask) {
  uint8_t active_lanes_mask = 0;
  for (uint8_t lane = 0; lane < group.lane_count; lane++) {
    if (has_active_rendering_cfx_effect(group.outputs[lane])) {
      active_lanes_mask |= static_cast<uint8_t>(1u << lane);
    }
  }
  return static_cast<uint8_t>(active_lanes_mask | fallback_mask);
}

static bool parallel_group_has_active_rendering_(
    CFXParallelGroupRuntime &group) {
  for (uint8_t lane = 0; lane < group.lane_count; lane++) {
    if (has_active_rendering_cfx_effect(group.outputs[lane])) {
      return true;
    }
  }
  return false;
}

static uint8_t active_parallel_group_mask_() {
  uint8_t active_mask = 0;
  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    auto &group = g_parallel_groups[gi];
    if (group.configured && parallel_group_has_active_rendering_(group)) {
      active_mask |= static_cast<uint8_t>(1u << gi);
    }
  }
  return active_mask;
}

bool CFXLightOutput::should_request_high_frequency_loop_() {
  if (!this->setup_completed_ || this->is_failed()) {
    return false;
  }

  // Governor: request high-frequency loop only while transport/service work is
  // pending. On single-core ESP32 variants (C3/S2/H2), active effects and
  // long outro phases share the same core with API, WiFi, OTA, and sensors, so
  // keep the persistent high-frequency request for concrete flush work only.
  if ((!cfx_unicore_build_() && this->has_outro()) || this->rmt_flush_pending_ ||
      this->seg_flush_pending_) {
    return true;
  }

  // SPI and Classic RMT need active-effect cadence restoration under ESPHome
  // 2026.5+. Parallel remains governed by pending group work to preserve the
  // validated S3 behavior and avoid monopolizing dense parallel nodes.
  if (!cfx_unicore_build_() &&
      (this->is_spi_transport() || this->is_rmt_transport()) &&
      has_active_rendering_cfx_effect(this)) {
    return true;
  }

  if (this->is_parallel_transport()) {
    auto &group = *parallel_group_for_output_(this);
    if (group.pending_mask != 0 || group.pending_first_ms != 0) {
      return true;
    }
#ifdef CFX_PARALLEL_I80_ENABLED
    const uint8_t group_bit =
        static_cast<uint8_t>(1u << group.group_index);
    if ((g_parallel_i80.pending_group_mask & group_bit) != 0) {
      return true;
    }
#endif
  }

  return false;
}

void CFXLightOutput::update_high_frequency_loop_request_() {
  if (this->should_request_high_frequency_loop_()) {
    this->high_freq_loop_requester_.start();
  } else {
    this->high_freq_loop_requester_.stop();
  }
}

static bool parallel_shared_whole_group_mode_enabled_() {
#if !defined(CONFIG_IDF_TARGET_ESP32) && defined(CFX_PARALLEL_I80_ENABLED)
  if (!g_parallel_i80.ready || parallel_configured_group_count_() < 2) {
    return false;
  }
  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    auto &group = g_parallel_groups[gi];
    if (!group.configured) {
      continue;
    }
    if (!group.ready || group.has_segments) {
      return false;
    }
  }
  return true;
#else
  return false;
#endif
}

static bool parallel_group_active_outputs_are_segmented_(
    CFXParallelGroupRuntime &group) {
  bool has_active = false;
  for (uint8_t lane = 0; lane < group.lane_count; lane++) {
    auto *output = group.outputs[lane];
    if (!has_active_rendering_cfx_effect(output)) {
      continue;
    }
    has_active = true;
    if (output == nullptr || !output->has_segments()) {
      return false;
    }
  }
  return has_active;
}

static bool parallel_shared_segment_group_mode_enabled_() {
#if !defined(CONFIG_IDF_TARGET_ESP32) && defined(CFX_PARALLEL_I80_ENABLED)
  if (!g_parallel_i80.ready || parallel_configured_group_count_() < 2) {
    return false;
  }
  uint8_t active_segment_groups = 0;
  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    auto &group = g_parallel_groups[gi];
    if (!group.configured) {
      continue;
    }
    if (!group.ready || !group.has_segments) {
      return false;
    }
    if (parallel_group_active_outputs_are_segmented_(group)) {
      active_segment_groups++;
    }
  }
  return active_segment_groups >= 2;
#else
  return false;
#endif
}

bool CFXLightOutput::request_parallel_group_flush_() {
  auto &g_parallel_group = *parallel_group_for_output_(this);
  if (!this->is_parallel_transport() || this->parallel_lane_count_ <= 1) {
    return true;
  }
  const uint8_t lane_bit = static_cast<uint8_t>(1u << this->parallel_lane_index_);
  if ((g_parallel_group.pending_mask & lane_bit) == 0) {
    g_parallel_group.pending_mask |= lane_bit;
    if (g_parallel_group.pending_first_ms == 0) {
      g_parallel_group.pending_first_ms = esphome::millis();
    }
  }

  const uint8_t active_lanes_mask =
      active_parallel_lanes_mask_(g_parallel_group, lane_bit);

  if ((g_parallel_group.pending_mask & active_lanes_mask) != active_lanes_mask) {
    this->update_high_frequency_loop_request_();
    return false;
  }

  g_parallel_group.pending_mask = 0;
  g_parallel_group.pending_first_ms = 0;
  g_parallel_group.coalesced_count++;
  return true;
}

void CFXLightOutput::service_parallel_group_flush_() {
  auto &g_parallel_group = *parallel_group_for_output_(this);
  if (!this->is_parallel_transport() || g_parallel_group.pending_mask == 0 ||
      g_parallel_group.pending_first_ms == 0) {
    return;
  }
  const uint32_t flush_timeout_ms =
#if defined(CONFIG_IDF_TARGET_ESP32)
      g_parallel_group.has_segments ? PARALLEL_CLASSIC_FLUSH_TIMEOUT_MS
                                    : PARALLEL_CLASSIC_WHOLE_FLUSH_TIMEOUT_MS;
#else
      g_parallel_group.has_segments ? PARALLEL_LCD_FLUSH_TIMEOUT_MS
                                    : PARALLEL_LCD_WHOLE_FLUSH_TIMEOUT_MS;
#endif
  const uint8_t pending_mask = g_parallel_group.pending_mask;
  const uint8_t active_lanes_mask =
      active_parallel_lanes_mask_(g_parallel_group, pending_mask);
  if ((pending_mask & active_lanes_mask) == active_lanes_mask) {
    g_parallel_group.pending_mask = 0;
    g_parallel_group.pending_first_ms = 0;
    g_parallel_group.coalesced_count++;
    this->commit_transmit_();
    return;
  }
  if ((esphome::millis() - g_parallel_group.pending_first_ms) <
      flush_timeout_ms) {
    return;
  }

  g_parallel_group.pending_mask = 0;
  g_parallel_group.pending_first_ms = 0;
  g_parallel_group.timeout_flush_count++;
  ESP_LOGV(TAG,
           "Parallel group '%s' timeout flush: mask=0x%02x timeouts=%" PRIu32,
           g_parallel_group.name.c_str(), pending_mask,
           g_parallel_group.timeout_flush_count);
  this->commit_transmit_();
}

bool CFXLightOutput::request_parallel_shared_group_flush_() {
#if !defined(CONFIG_IDF_TARGET_ESP32) && defined(CFX_PARALLEL_I80_ENABLED)
  auto &g_parallel_group = *parallel_group_for_output_(this);
  const bool shared_whole_mode = parallel_shared_whole_group_mode_enabled_();
  const bool shared_segment_mode =
      parallel_shared_segment_group_mode_enabled_();
  if (!this->is_parallel_transport() ||
      (!shared_whole_mode && !shared_segment_mode)) {
    return true;
  }

  const uint8_t group_bit =
      static_cast<uint8_t>(1u << g_parallel_group.group_index);
  uint8_t active_groups = active_parallel_group_mask_();
  active_groups = static_cast<uint8_t>(active_groups | group_bit);

  // A single active logical group should keep the ordinary group flush path.
  if ((active_groups & static_cast<uint8_t>(active_groups - 1u)) == 0) {
    return true;
  }

  if ((g_parallel_i80.pending_group_mask & group_bit) == 0) {
    g_parallel_i80.pending_group_mask =
        static_cast<uint8_t>(g_parallel_i80.pending_group_mask | group_bit);
    if (g_parallel_i80.pending_group_first_ms == 0) {
      g_parallel_i80.pending_group_first_ms = esphome::millis();
    }
  }

  if ((g_parallel_i80.pending_group_mask & active_groups) == active_groups) {
    const uint8_t flush_mask = active_groups;
    g_parallel_i80.pending_group_mask =
        static_cast<uint8_t>(g_parallel_i80.pending_group_mask & ~flush_mask);
    if (g_parallel_i80.pending_group_mask == 0) {
      g_parallel_i80.pending_group_first_ms = 0;
    }
    this->flush_parallel_shared_groups_(flush_mask);
  }
  this->update_high_frequency_loop_request_();
  return false;
#else
  return true;
#endif
}

void CFXLightOutput::service_parallel_shared_group_flush_() {
#if !defined(CONFIG_IDF_TARGET_ESP32) && defined(CFX_PARALLEL_I80_ENABLED)
  const bool shared_whole_mode = parallel_shared_whole_group_mode_enabled_();
  const bool shared_segment_mode =
      parallel_shared_segment_group_mode_enabled_();
  if (!this->is_parallel_transport() ||
      (!shared_whole_mode && !shared_segment_mode) ||
      g_parallel_i80.pending_group_mask == 0 ||
      g_parallel_i80.pending_group_first_ms == 0) {
    return;
  }

  uint8_t active_groups = active_parallel_group_mask_();
  if (active_groups == 0) {
    active_groups = g_parallel_i80.pending_group_mask;
  }

  if ((g_parallel_i80.pending_group_mask & active_groups) == active_groups) {
    const uint8_t flush_mask = active_groups;
    g_parallel_i80.pending_group_mask =
        static_cast<uint8_t>(g_parallel_i80.pending_group_mask & ~flush_mask);
    if (g_parallel_i80.pending_group_mask == 0) {
      g_parallel_i80.pending_group_first_ms = 0;
    }
    this->flush_parallel_shared_groups_(flush_mask);
    return;
  }

  if ((esphome::millis() - g_parallel_i80.pending_group_first_ms) <
      PARALLEL_LCD_WHOLE_FLUSH_TIMEOUT_MS) {
    return;
  }

  const uint8_t flush_mask = g_parallel_i80.pending_group_mask;
  g_parallel_i80.pending_group_mask = 0;
  g_parallel_i80.pending_group_first_ms = 0;
  for (uint8_t gi = 0; gi < PARALLEL_MAX_GROUPS; gi++) {
    if ((flush_mask & static_cast<uint8_t>(1u << gi)) != 0) {
      g_parallel_groups[gi].timeout_flush_count++;
    }
  }
  ESP_LOGV(TAG, "Parallel shared group timeout flush: mask=0x%02x",
           flush_mask);
  this->flush_parallel_shared_groups_(flush_mask);
#endif
}

// --- SPI Transport Setup ---

spi_host_device_t CFXLightOutput::resolve_spi_host_(CFXSPIHost host) {
  switch (host) {
  case SPI_HOST_3:
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2) || \
    defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
    return SPI3_HOST;
#else
    ESP_LOGE(TAG, "SPI3_HOST not available on this chip variant, falling back to SPI2_HOST");
    return SPI2_HOST;
#endif
  case SPI_HOST_2:
  default:
    return SPI2_HOST;
  }
}

size_t CFXLightOutput::get_spi_end_frame_size_() const {
  if (this->chipset_ == CHIPSET_SK9822) {
    // SK9822: ceil(num_leds / 2) + 1 bytes of 0x00
    return (this->num_leds_ + 1) / 2 + 1;
  } else {
    // APA102: ceil(num_leds / 16) + 1 bytes of 0xFF
    return (this->num_leds_ + 15) / 16 + 1;
  }
}

uint8_t CFXLightOutput::get_spi_end_frame_byte_() const {
  return (this->chipset_ == CHIPSET_SK9822) ? 0x00 : 0xFF;
}

size_t CFXLightOutput::get_spi_frame_size_() const {
  // Start frame (4) + LED frames (num_leds * 4) + end frame
  size_t raw = 4 + (this->num_leds_ * 4) + this->get_spi_end_frame_size_();
  // Round up to 4-byte alignment for DMA
  return (raw + 3) & ~3;
}

uint32_t CFXLightOutput::get_spi_frame_timeout_ms_() const {
  const size_t frame_bits = this->get_spi_frame_size_() * 8;
  uint32_t tx_ms = 0;

  if (this->spi_speed_hz_ > 0) {
    tx_ms = static_cast<uint32_t>(
        (frame_bits * 1000ULL + this->spi_speed_hz_ - 1) / this->spi_speed_hz_);
  }

  // Add FRAMETIME + 15 ms of headroom so the DMA completion ISR has time to
  // fire even when delayed by concurrent RMT interrupt processing on Classic
  // ESP32. The minimum of 40 ms is well below a 2-frame budget at 25 FPS,
  // ensuring the timeout never fires during normal mixed-protocol operation.
  tx_ms += FRAMETIME + 15;
  if (tx_ms < 40)
    tx_ms = 40;
  return tx_ms;
}

bool CFXLightOutput::wait_for_rmt_tx_(uint32_t timeout_ms, const char *context) {
  // Fast path: ISR already cleared the flag — nothing to wait for.
  if (!this->rmt_tx_in_flight_) {
    return true;
  }

  // Slow path: previous frame DMA still in flight (fast refresh or first call).
  // Spin in 100µs slices; feed WDT every ~5ms to prevent watchdog resets on
  // very long strips. Using micros() polling keeps us out of FreeRTOS scheduler
  // overhead for what is usually a sub-millisecond wait.
  const uint32_t wait_start_us = micros();
  const uint32_t deadline_us   = (uint32_t)timeout_ms * 1000u;

  while (this->rmt_tx_in_flight_) {
    const uint32_t elapsed = micros() - wait_start_us;
    if (elapsed >= deadline_us) {
      // ISR never fired within the budget — attempt blocking recovery.
      this->rmt_wait_timeout_count_++;
      const uint32_t wait_us = micros() - wait_start_us;
      this->perf_diag_total_wait_us_ += wait_us;
      if (wait_us > this->perf_diag_max_wait_us_) {
        this->perf_diag_max_wait_us_ = wait_us;
      }
      ESP_LOGW(TAG, "RMT TX wait timeout (%u ms) during %s (waits=%" PRIu32
               ", timeouts=%" PRIu32 ")",
               timeout_ms, context, this->rmt_wait_count_,
               this->rmt_wait_timeout_count_);
      this->status_set_warning();
      return false;
    }
    // Yield in 100µs increments; feed WDT every ~5ms.
    if ((elapsed % 5000u) < 200u) {
      esphome::App.feed_wdt();
    }
    esp_rom_delay_us(100);
  }

  const uint32_t wait_us = micros() - wait_start_us;
  this->perf_diag_total_wait_us_ += wait_us;
  if (wait_us > this->perf_diag_max_wait_us_) {
    this->perf_diag_max_wait_us_ = wait_us;
  }
  this->rmt_wait_count_++;
  return true;
}

bool CFXLightOutput::wait_for_spi_tx_(uint32_t timeout_ms, const char *context) {
  if (!this->spi_tx_in_flight_ || this->spi_device_ == nullptr) {
    return true;
  }

  const uint32_t wait_start_us = micros();
  spi_transaction_t *ret_trans = nullptr;
  esp_err_t err = spi_device_get_trans_result(
      this->spi_device_, &ret_trans, pdMS_TO_TICKS(timeout_ms));
  const uint32_t wait_us = micros() - wait_start_us;

  if (err == ESP_OK) {
    this->spi_tx_in_flight_ = false;
    if (g_spi_dma_active_count > 0) {
      g_spi_dma_active_count--;
    }
    this->spi_wait_count_++;
    // Capture actual wire-time for perf diag (time spent waiting = DMA transfer time).
    if (wait_us > this->perf_diag_max_wait_us_) {
      this->perf_diag_max_wait_us_ = wait_us;
    }
    this->perf_diag_total_wait_us_ += wait_us;
    if (ret_trans != &this->spi_trans_) {
      ESP_LOGW(TAG,
               "SPI TX completion mismatch during %s (expected=%p got=%p)",
               context, &this->spi_trans_, ret_trans);
    }
    return true;
  }

  if (err == ESP_ERR_TIMEOUT) {
    this->spi_wait_timeout_count_++;
    ESP_LOGW(TAG,
             "SPI TX wait timeout during %s (% " PRIu32 " ms, waits=% " PRIu32
             ", timeouts=% " PRIu32 ", queue_err=% " PRIu32 ")",
             context, timeout_ms, this->spi_wait_count_,
             this->spi_wait_timeout_count_, this->spi_queue_error_count_);
  } else {
    ESP_LOGW(TAG, "SPI TX wait failed during %s (err=%d)", context, err);
  }

  this->status_set_warning();
  return false;
}

void CFXLightOutput::setup_spi_() {
  size_t frame_size = this->get_spi_frame_size_();

  // Allocate DMA-capable frame buffer (must be 32-bit aligned internal RAM)
  this->spi_frame_buf_ = (uint8_t *)heap_caps_malloc(frame_size, MALLOC_CAP_DMA);
  if (this->spi_frame_buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate SPI frame buffer (%u bytes, DMA)!", frame_size);
    this->mark_failed();
    return;
  }
  memset(this->spi_frame_buf_, 0, frame_size);

  spi_host_device_t host = resolve_spi_host_(this->spi_host_);

  spi_bus_config_t bus_cfg = {};
  bus_cfg.mosi_io_num = this->spi_data_pin_;
  bus_cfg.miso_io_num = -1;   // not needed
  bus_cfg.sclk_io_num = this->spi_clock_pin_;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  bus_cfg.max_transfer_sz = frame_size;

  esp_err_t err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI bus init failed (err=%d, data=GPIO%u clock=GPIO%u)",
             err, this->spi_data_pin_, this->spi_clock_pin_);
    this->mark_failed();
    return;
  }

  spi_device_interface_config_t dev_cfg = {};
  dev_cfg.clock_speed_hz = this->spi_speed_hz_;
  dev_cfg.mode = 0;             // CPOL=0, CPHA=0
  dev_cfg.spics_io_num = -1;    // APA102/SK9822 have no CS line
  dev_cfg.queue_size = 1;
  dev_cfg.flags = SPI_DEVICE_NO_DUMMY;  // required for long strips
  dev_cfg.post_cb = spi_tx_done_cb_;

  err = spi_bus_add_device(host, &dev_cfg, &this->spi_device_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI device add failed (err=%d)", err);
    this->mark_failed();
    return;
  }

  const int spi_host_num = (host == SPI2_HOST) ? 2 : 3;

  ESP_LOGI(TAG,
           "SPI transport ready: host=SPI%d data=GPIO%u clock=GPIO%u "
           "speed=%" PRIu32 " Hz frame=%u bytes est_tx_timeout=%" PRIu32
           " ms mode=async_queue",
           spi_host_num, this->spi_data_pin_, this->spi_clock_pin_,
           this->spi_speed_hz_, static_cast<unsigned>(frame_size),
           this->get_spi_frame_timeout_ms_());
}

// --- Dynamic State Synchronization ---

void CFXLightOutput::maybe_apply_turn_on_defaults_(light::LightState *state,
                                                   bool &prev_on_state) {
  if (state == nullptr) {
    return;
  }

  bool is_on = state->remote_values.is_on();
  bool turned_on = is_on && !prev_on_state;
  prev_on_state = is_on;

  if (!turned_on || this->applying_turn_on_defaults_) {
    return;
  }

  if (!this->turn_on_defaults_.should_apply(state, this->has_white_channel())) {
    return;
  }

  auto call = state->make_call();
  auto *active_cfx_effect = resolve_active_cfx_effect(state);
  const bool preserve_transition =
      active_cfx_effect != nullptr &&
      active_cfx_effect->uses_default_transition();
  this->turn_on_defaults_.apply(call, this->has_white_channel(),
                                !preserve_transition);
  this->applying_turn_on_defaults_ = true;
  call.perform();
  this->applying_turn_on_defaults_ = false;
}

void CFXLightOutput::on_master_update() {
  this->invalidate_segment_coord_schedule_();

  if (this->master_light_state_ == nullptr) {
    return;
  }
  this->wake_mono_idle_light_state_(this->master_light_state_);

  this->maybe_apply_turn_on_defaults_(this->master_light_state_,
                                      this->prev_master_defaults_state_);

  if (this->segment_light_states_.empty()) {
    return;
  }
  if (this->has_active_parent_owned_segments_()) {
    this->refresh_parent_owned_segment_slots_();
  }

  if (this->is_syncing_) {
    return;
  }
  this->is_syncing_ = true; // Lock recursion

  bool master_on = this->master_light_state_->remote_values.is_on();
  float master_brightness =
      this->master_light_state_->remote_values.get_brightness();

  bool master_state_changed = (master_on != this->prev_master_state_);
  this->prev_master_state_ = master_on;

  // TOP-DOWN SYNC
  for (auto *seg_state : this->segment_light_states_) {
    // Only force ON/OFF cascade if the Master state actually flipped
    bool state_changed =
        master_state_changed && (seg_state->remote_values.is_on() != master_on);

    // Only update brightness if the segment is currently ON (or is becoming ON)
    bool is_seg_on =
        state_changed ? master_on : seg_state->remote_values.is_on();
    bool bright_changed = master_on && is_seg_on &&
                          std::abs(seg_state->remote_values.get_brightness() -
                                   master_brightness) > 0.01f;

    if (state_changed || bright_changed) {
      this->wake_mono_idle_light_state_(seg_state);
      auto call = seg_state->make_call();
      // BUG 11 FIX: Suppress ESPHome's AddressableLightTransformer which
      // paints RGB white directly into the pixel buffer during transitions.
      // CFX effects handle their own visual transitions (intros/outros).
      call.set_transition_length(0);
      if (state_changed)
        call.set_state(master_on);
      if (bright_changed)
        call.set_brightness(master_brightness);

      call.perform();
    }
  }

  this->is_syncing_ = false; // Unlock
}

void CFXLightOutput::on_segment_update() {
  this->invalidate_segment_coord_schedule_();

  if (this->segment_light_states_.empty()) {
    return;
  }
  if (this->has_active_parent_owned_segments_()) {
    this->refresh_parent_owned_segment_slots_();
  }
  if (this->master_light_state_ == nullptr) {
    for (auto *seg_state : this->segment_light_states_) {
      this->wake_mono_idle_light_state_(seg_state);
    }
    return;
  }

  if (this->is_syncing_)
    return;
  this->is_syncing_ = true; // Lock recursion

  for (auto *seg_state : this->segment_light_states_) {
    this->wake_mono_idle_light_state_(seg_state);
  }

  bool master_on = this->master_light_state_->remote_values.is_on();

  // BOTTOM-UP SYNC (A segment changed)
  bool is_any_segment_on = false;
  for (auto *s : this->segment_light_states_) {
    if (s->remote_values.is_on()) {
      is_any_segment_on = true;
      break;
    }
  }

  if (master_on != is_any_segment_on) {
    // Bottom-up sync for all transports (RMT, SPI, and Parallel).
    // The master reflects the aggregate ON/OFF state of the segments.
    this->wake_mono_idle_light_state_(this->master_light_state_);
    // We are commanding the master to change state.
    // Update prev_master_state_ so that unexpected/deferred incoming Master
    // listener callbacks don't overreact and force all segments ON!
    this->prev_master_state_ = is_any_segment_on;

    auto call = this->master_light_state_->make_call();
    call.set_state(is_any_segment_on);
    // BUG 11 FIX: Suppress ESPHome's AddressableLightTransformer.
    // The master has no effect_active_ flag, so the transformer iterates
    // ALL parent pixels and paints RGB white — contaminating segment buffers.
    call.set_transition_length(0);
    call.perform();
  }

  this->is_syncing_ = false; // Unlock
}

// --- Component Loop (Intercepts Outro Playback) ---

void CFXLightOutput::loop() {
  this->update_high_frequency_loop_request_();
  this->record_parallel_completed_led_frames_();

  if (runtime_debug_enabled_for_output(this)) {
    this->log_spi_cadence_diag_();
    this->log_rmt_cadence_diag_();
  }

  if (this->transport_ == TRANSPORT_RMT && this->rmt_flush_pending_ &&
      !this->rmt_tx_in_flight_) {
    this->rmt_flush_pending_ = false;
    g_last_rmt_launch_us = micros();
    this->perf_diag_last_launch_slot_ =
        static_cast<uint8_t>(g_rmt_launch_seq & 0x3);
    g_rmt_launch_seq++;
    this->flush_rmt_();
  }

  // P3: drain any outputs whose barrier window expired before all outputs
  // arrived (e.g. an inactive output that skipped write_state this tick).
  CFXTransmitBarrier::get().service(this);
  this->service_parallel_group_flush_();
  this->service_parallel_shared_group_flush_();
  if (!this->outro_cbs_.empty()) {
    const uint32_t now_ms = esphome::millis();
    if (this->outro_last_frame_ms_ != 0 &&
        (now_ms - this->outro_last_frame_ms_) < FRAMETIME) {
      goto segment_flush_done;
    }
    this->outro_last_frame_ms_ = now_ms;

    // Light is technically 'Off' so we must restore full local brightness
    // so our pixel buffers aren't multiplied by 0 implicitly.
    this->correction_.set_local_brightness(255);

    for (auto it = this->outro_cbs_.begin(); it != this->outro_cbs_.end();) {
      bool done = (*it)();
      if (done) {
        it = this->outro_cbs_.erase(it);
      } else {
        ++it;
      }
    }

    uint8_t outro_segment_mask = 0;
    uint8_t outro_segment_count = 0;
    if (this->render_segment_coordinator_epoch_(outro_segment_mask,
                                                outro_segment_count, false,
                                                true)) {
      this->finalize_segment_coordinator_epoch_(outro_segment_mask,
                                                outro_segment_count, false);
    }

    if (!this->outro_cbs_.empty()) {
      // Force direct DMA flush of the frame. We cannot use schedule_show()
      // here because ESPHome's LightState loop is disabled when the light is
      // turned off, meaning it will never poll us.
      this->outro_parent_flush_allowed_ = true;
      this->write_state(nullptr);
      this->outro_parent_flush_allowed_ = false;
    }

    if (this->outro_cbs_.empty()) {
      // Outro finished. Black out only the pixels that belong to segments
      // that are now OFF. In a segmented strip, other segments may still be
      // running their own effects — zeroing the entire DMA buffer would
      // make them go dark even though they are still ON.
      //
      // Without segments, fall back to blacking the whole strip (original
      // behaviour) so non-segmented setups are unaffected.
      if (this->has_segments()) {
        const auto &defs   = this->get_segment_defs();
        const auto &states = this->get_segment_light_states();
        for (size_t si = 0; si < defs.size() && si < states.size(); si++) {
          if (!states[si]->remote_values.is_on()) {
            for (int p = defs[si].start; p < defs[si].stop; p++) {
              (*this)[p] = Color::BLACK;
            }
          }
        }
      } else {
        for (int i = 0; i < this->size(); i++) {
          (*this)[i] = Color::BLACK;
        }
      }
      this->outro_parent_flush_allowed_ = true;
      this->write_state(nullptr);
      this->outro_parent_flush_allowed_ = false;
      this->outro_last_frame_ms_ = 0;
      this->release_outro_callback_storage_();
    }
    goto segment_flush_done;
  }

  if (this->service_segment_render_coordinator_()) {
    goto segment_flush_done;
  }

  // Fix-2: Drain the coalesced segment flush.
  // request_segment_flush() sets a dirty flag instead of calling write_state()
  // immediately. Here we fire exactly ONE DMA call once all segments in the
  // same ESPHome loop tick have contributed their updates.
  if (this->seg_flush_pending_) {
    const size_t segment_count = this->segment_light_states_.size();
    uint8_t active_count = 0;
    uint8_t ready_count = 0;
    for (size_t i = 0; i < segment_count && i < MAX_CFX_SEGMENTS; i++) {
      if (!segment_participates_in_barrier(this->segment_light_states_[i])) {
        continue;
      }
      const uint8_t bit = static_cast<uint8_t>(1u << i);
      if ((this->segment_coord_owned_mask_ & bit) != 0 &&
          this->seg_request_generation_[i] ==
              this->seg_flushed_generation_[i]) {
        continue;
      }
      active_count++;
      if (this->seg_request_generation_[i] != this->seg_flushed_generation_[i]) {
        ready_count++;
      }
    }
    uint32_t wait_target_ms = active_count == ready_count ? 0 : 2;
    // Segmented parents are visibly less tolerant of partial-frame
    // presentation, so bias them harder toward waiting for full convergence
    // without ever blocking indefinitely.
    if (wait_target_ms != 0 && segment_count > 0) {
      wait_target_ms = (active_count >= 2 && ready_count + 1 >= active_count)
                           ? 2
                           : 3;
    }
    uint32_t elapsed = esphome::millis() - this->seg_flush_first_ms_;
    if (elapsed >= wait_target_ms) {
      this->seg_last_flush_mask_ = this->seg_flush_pending_mask_;
      this->seg_last_flush_count_ = ready_count;
      if (active_count > ready_count) {
        const uint32_t missing = static_cast<uint32_t>(active_count - ready_count);
        this->perf_diag_total_partial_flushes_++;
        this->seg_partial_frame_suppressed_++;
        this->seg_missed_epoch_count_ += missing;
        if (missing > this->perf_diag_max_partial_missing_) {
          this->perf_diag_max_partial_missing_ = missing;
        }
        for (size_t i = 0; i < segment_count && i < MAX_CFX_SEGMENTS; i++) {
          this->seg_flushed_generation_[i] = this->seg_request_generation_[i];
        }
        this->seg_flush_pending_mask_ = 0;
        this->seg_flush_dirty_mask_ = 0;
        this->seg_flush_pending_ = false;
        this->seg_flush_first_ms_ = 0;
        this->seg_coord_collect_start_us_ = 0;
        this->log_segment_coordinator_diag_();
        goto segment_flush_done;
      }
      for (size_t i = 0; i < segment_count && i < MAX_CFX_SEGMENTS; i++) {
        this->seg_flushed_generation_[i] = this->seg_request_generation_[i];
      }
      this->seg_flush_pending_mask_ = 0;
      const uint8_t dirty_mask = this->seg_flush_dirty_mask_;
      this->seg_flush_dirty_mask_ = 0;
      this->seg_flush_pending_ = false;
      this->seg_flush_first_ms_ = 0;
      if (dirty_mask == 0) {
        this->seg_clean_epoch_suppressed_++;
        this->seg_coord_collect_start_us_ = 0;
        this->log_segment_coordinator_diag_();
        goto segment_flush_done;
      }
      this->write_state(nullptr);
    }
  }

segment_flush_done:
  this->update_high_frequency_loop_request_();
#ifdef USE_CFX_EVENTS
  chimera_fx::CFXEventManager::get().flush_pending();
#endif
}

// --- Power Estimation / Manual Reduction ---

float CFXLightOutput::estimate_power_current_ma(
    const CFXPowerModel &model, float dynamic_scale) const {
  if (this->buf_ == nullptr || this->num_leds_ == 0) {
    return 0.0f;
  }

  const bool has_white = this->has_white_channel();
  const uint8_t stride = has_white ? 4 : 3;
  const uint8_t *src = this->buf_;
  float dynamic_ma = 0.0f;

  for (uint16_t i = 0; i < this->num_leds_; i++) {
    const uint8_t rgb_offset = this->is_wrgb_ ? 1 : 0;
    dynamic_ma +=
        model.rgb_channel_ma *
        (static_cast<float>(src[rgb_offset]) +
         static_cast<float>(src[rgb_offset + 1]) +
         static_cast<float>(src[rgb_offset + 2])) /
        255.0f;
    if (has_white) {
      const uint8_t white_offset = this->is_wrgb_ ? 0 : 3;
      dynamic_ma += model.white_channel_ma *
                    static_cast<float>(src[white_offset]) / 255.0f;
    }
    src += stride;
  }

  if (dynamic_scale < 0.0f) {
    dynamic_scale = 0.0f;
  } else if (dynamic_scale > 1.0f) {
    dynamic_scale = 1.0f;
  }
  return (model.idle_ma * static_cast<float>(this->num_leds_)) +
         (dynamic_ma * dynamic_scale);
}

uint8_t CFXLightOutput::get_power_transmit_scale_() const {
  if (this->power_manager_ == nullptr) {
    return 255;
  }
  return this->power_manager_->get_transmit_scale();
}

void CFXLightOutput::request_power_reduction_refresh() {
  if (this->has_segments() && !this->has_outro()) {
    this->write_state(nullptr);
    return;
  }
  this->schedule_show();
}

void CFXLightOutput::apply_power_scale_to_buffer_(uint8_t *data,
                                                  size_t len) const {
  const uint8_t scale = this->get_power_transmit_scale_();
  if (data == nullptr || len == 0 || scale >= 255) {
    return;
  }
  for (size_t i = 0; i < len; i++) {
    data[i] =
        static_cast<uint8_t>((static_cast<uint16_t>(data[i]) * scale + 127u) /
                             255u);
  }
}

void CFXLightOutput::fill_buffer_solid_(const Color &color) {
  if (this->buf_ == nullptr || this->effect_data_ == nullptr ||
      this->num_leds_ == 0) {
    return;
  }

  int32_t r = 0, g = 0, b = 0;
  switch (this->rgb_order_) {
  case ORDER_RGB:
    r = 0;
    g = 1;
    b = 2;
    break;
  case ORDER_RBG:
    r = 0;
    g = 2;
    b = 1;
    break;
  case ORDER_GRB:
    r = 1;
    g = 0;
    b = 2;
    break;
  case ORDER_GBR:
    r = 2;
    g = 0;
    b = 1;
    break;
  case ORDER_BGR:
    r = 2;
    g = 1;
    b = 0;
    break;
  case ORDER_BRG:
    r = 1;
    g = 2;
    b = 0;
    break;
  }

  const bool has_white = this->has_white_channel();
  const uint8_t multiplier = has_white ? 4 : 3;
  const uint8_t offset = this->is_wrgb_ ? 1 : 0;
  const uint8_t white = this->is_wrgb_ ? 0 : 3;
  const uint8_t cr = this->correction_.color_correct_red(color.r);
  const uint8_t cg = this->correction_.color_correct_green(color.g);
  const uint8_t cb = this->correction_.color_correct_blue(color.b);
  const uint8_t cw = this->correction_.color_correct_white(color.w);

  for (uint16_t i = 0; i < this->num_leds_; i++) {
    uint8_t *base = this->buf_ + (static_cast<size_t>(i) * multiplier);
    base[r + offset] = cr;
    base[g + offset] = cg;
    base[b + offset] = cb;
    if (has_white) {
      base[white] = cw;
    }
    this->effect_data_[i] = 0;
  }
}

void CFXLightOutput::scrub_inactive_segments_() {
  if (this->outro_cbs_.empty() && !this->segment_light_states_.empty()) {
    esphome::App.feed_wdt();
    const size_t count =
        std::min(this->segment_light_states_.size(), this->segment_defs_.size());
    for (size_t i = 0; i < count; i++) {
      auto *seg_state = this->segment_light_states_[i];
      if (seg_state == nullptr || seg_state->remote_values.is_on()) {
        continue;
      }
      const auto &def = this->segment_defs_[i];
      for (int p = def.start; p < def.stop; p++) {
        if (p < this->size()) {
          (*this)[p] = esphome::Color::BLACK;
        }
      }
    }
  }
}

// --- Update State (Handles Brightness & Solid Colors) ---

void CFXLightOutput::update_state(light::LightState *state) {
  if (state == nullptr) {
    return;
  }
  if (this->buf_ == nullptr || this->effect_data_ == nullptr ||
      this->num_leds_ == 0) {
    return;
  }
  auto val = state->current_values;

  if (this->has_segments()) {
    // CFX-032: correction_ is shared by every segment's ESPColorView.
    // Never derive it from master current_values: the master may be OFF
    // (get_state()==0) the frame a segment first turns ON, zero-gating
    // all pixels. Hold gate at 255; each segment's update_state() bakes
    // its own brightness into pixel values via color channel scaling.
    this->correction_.set_local_brightness(255);
    this->tracked_brightness_ = 255;
    return;
  }

  // ALWAYS update the hardware brightness gate. If we don't, and an effect
  // starts while the light is OFF, the gate stays at 0 and the strip stays
  // black.
  uint8_t max_brightness = 0;
  auto *active_cfx_effect = resolve_active_cfx_effect(state);
  const bool effect_uses_default_transition =
      active_cfx_effect != nullptr &&
      active_cfx_effect->uses_default_transition();
  if (this->is_effect_active()) {
    const auto &gate_values =
        effect_uses_default_transition ? state->current_values
                                       : state->remote_values;
    max_brightness = light::to_uint8_scale(gate_values.get_brightness() *
                                           gate_values.get_state());
    if (max_brightness == 0 && state->remote_values.is_on() &&
        !chimera_fx::LightStateProxy::has_active_transformer(state)) {
      max_brightness = 255;
    }
  } else {
    max_brightness =
        light::to_uint8_scale(val.get_brightness() * val.get_state());
  }
  this->tracked_brightness_ = max_brightness;
  this->correction_.set_local_brightness(max_brightness);

  if (this->is_effect_active()) {
    // Effect handles its own pixel math in apply().
    return;
  }

  // Solid-color rendering for non-segmented lights. For default transitions
  // we use the generic LightTransitionTransformer, so current_values already
  // contain the in-progress brightness/color step and should be painted
  // directly into the buffer.
  Color c = light::color_from_light_color_values(val);

  if (this->force_white_sw_ != nullptr && this->force_white_sw_->state &&
      cfx::should_auto_disable_force_white(c.r, c.g, c.b)) {
    this->force_white_sw_->turn_off();
  }

  // BUG 13 FIX: Apply force_white to solid colors BEFORE they hit the buffer
  if (this->is_force_white_active_for(state))
    cfx::apply_force_white(c.r, c.g, c.b, c.w);

  this->fill_buffer_solid_(c);
  this->schedule_show();
}

// --- Segment Flush: Counter-Based Multi-Segment Synchronization ---

// Each VirtualSegmentLight::write_state() calls this instead of directly
// firing the DMA. We count how many segments have reported their render done
// for the current frame. Only when ALL N segments are ready (or a 50ms safety
// timeout expires for the solid-color / mixed path), we flush once with the
// complete buffer. This prevents partial-frame DMA which causes random color
// artifacts on segments with misaligned update_interval_ phases.
void CFXLightOutput::request_segment_flush(light::LightState *state) {
  if (active_cfx_effect_is_clean_mono_idle(state)) {
    this->seg_clean_epoch_suppressed_++;
    this->log_segment_coordinator_diag_();
    return;
  }
  if (state != nullptr && this->segment_coordinator_owns(state)) {
    this->note_segment_coord_write_skip();
    return;
  }

  if (state != nullptr) {
    for (size_t i = 0; i < this->segment_light_states_.size() && i < 8; i++) {
      if (this->segment_light_states_[i] == state) {
        this->seg_flush_pending_mask_ |= static_cast<uint8_t>(1u << i);
        this->seg_flush_dirty_mask_ |= static_cast<uint8_t>(1u << i);
        this->seg_generation_counter_++;
        if (this->seg_generation_counter_ == 0) {
          this->seg_generation_counter_ = 1;
        }
        this->seg_request_generation_[i] = this->seg_generation_counter_;
        break;
      }
    }
  }
  // CFX-032: Coalesced Segment Flush.
  // Instead of calling write_state(nullptr) immediately (which blocks the
  // loop N-times per frame), we set a dirty flag and record the timestamp.
  // loop() will fire a single DMA call for all pending segments in one tick.
  if (!this->seg_flush_pending_) {
    this->seg_flush_pending_ = true;
    this->seg_flush_first_ms_ = esphome::millis();
    this->update_high_frequency_loop_request_();
  }

  const size_t segment_count = this->segment_light_states_.size();
  if (segment_count == 0 || segment_count > 8) {
    return;
  }

  uint8_t active_count = 0;
  uint8_t ready_count = 0;
  for (size_t i = 0; i < segment_count && i < MAX_CFX_SEGMENTS; i++) {
    if (!segment_participates_in_barrier(this->segment_light_states_[i])) {
      continue;
    }
    active_count++;
    if (this->seg_request_generation_[i] != this->seg_flushed_generation_[i]) {
      ready_count++;
    }
  }
  if (active_count == 0 || ready_count != active_count) {
    return;
  }

  // All active segment effect entities have already contributed to this frame.
  // Flush immediately instead of idling in the fallback window.
  this->seg_last_flush_mask_ = this->seg_flush_pending_mask_;
  this->seg_last_flush_count_ = ready_count;
  for (size_t i = 0; i < segment_count && i < MAX_CFX_SEGMENTS; i++) {
    this->seg_flushed_generation_[i] = this->seg_request_generation_[i];
  }
  this->seg_flush_pending_mask_ = 0;
  const uint8_t dirty_mask = this->seg_flush_dirty_mask_;
  this->seg_flush_dirty_mask_ = 0;
  this->seg_flush_pending_ = false;
  this->seg_flush_first_ms_ = 0;
  if (dirty_mask == 0) {
    this->seg_clean_epoch_suppressed_++;
    this->log_segment_coordinator_diag_();
    return;
  }
  this->write_state(nullptr);
}

// --- Write State (Fire-and-Forget DMA) ---

// P3: Called by CFXTransmitBarrier when all registered outputs are ready.
// Encapsulates the transport-specific DMA fire sequence so the barrier can
// trigger it on any output without knowing its transport type.
void CFXLightOutput::commit_transmit_() {
  if (this->transport_ == TRANSPORT_SPI) {
    esphome::App.feed_wdt();
    this->flush_spi_();
  } else if (this->transport_ == TRANSPORT_PARALLEL) {
    esphome::App.feed_wdt();
    this->flush_parallel_();
  } else {
    const uint32_t stagger_gap_us = rmt_launch_stagger_gap_us();
    uint32_t launch_us = micros();
    if (stagger_gap_us > 0 && g_last_rmt_launch_us != 0) {
      const uint32_t since_last_launch = launch_us - g_last_rmt_launch_us;
      if (since_last_launch < stagger_gap_us) {
        const uint32_t gate_us = stagger_gap_us - since_last_launch;
        this->perf_diag_total_gate_us_ += gate_us;
        if (gate_us > this->perf_diag_max_gate_us_) {
          this->perf_diag_max_gate_us_ = gate_us;
        }
        esp_rom_delay_us(gate_us);
        launch_us = micros();
      }
    }
    g_last_rmt_launch_us = launch_us;
    this->perf_diag_last_launch_slot_ =
        static_cast<uint8_t>(g_rmt_launch_seq & 0x3);
    g_rmt_launch_seq++;
    this->flush_rmt_();
  }
}

void CFXLightOutput::write_state(light::LightState *state) {
  chimera_fx::CFXAddressableLightEffect *active_cfx_effect =
      resolve_perf_diag_effect(this);
  const bool perf_diag_enabled = perf_diag_enabled_for_effect(active_cfx_effect);
  const bool spi_cadence_diag_enabled =
      this->is_spi_transport() && runtime_debug_enabled_for_output(this);
  const bool rmt_cadence_diag_enabled =
      this->is_rmt_transport() && runtime_debug_enabled_for_output(this);
  const uint32_t write_start_us = micros();
  this->update_high_frequency_loop_request_();
  this->perf_diag_last_flush_valid_ = false;
  this->perf_diag_last_flush_total_us_ = 0;
  this->perf_diag_last_flush_tx_us_ = 0;

  if (!this->has_outro() &&
      ((state != nullptr && active_cfx_effect_is_clean_mono_idle(state)) ||
       (state == nullptr && this->seg_last_flush_mask_ == 0 &&
        all_active_cfx_effects_clean_mono_idle(this)))) {
    this->seg_clean_epoch_suppressed_++;
    this->log_segment_coordinator_diag_();
    
    // Evaluate and log idle state even when the rendering pipeline is fully suppressed
    this->refresh_segment_coordination_mask_();
    const uint8_t segment_idle_mask =
        this->collect_clean_mono_idle_segment_mask_();
    this->apply_mono_idle_loop_state_(segment_idle_mask);
    return;
  }

  // 1. Master Mute: When segments are active, the Master LightState's
  // rendering pipeline is suppressed. The Master paints the entire strip with
  // its ON-state color (white) on every frame — this overwrites segment pixels
  // and creates a white flash. Muting it here forces all pixel rendering to
  // go through the segment-driven flush path (write_state(nullptr)) instead.
  // Non-segmented lights and outro DMA (nullptr calls) pass through unchanged.
  if (state != nullptr && this->has_segments()) {
    // CFX: Even though the master buffer is muted, we must validate its
    // idle output so the effect can transition to IDLE state and sleep.
    mark_committed_mono_idle_outputs(this);
    return; // Master muted — segments own the pixel buffer
  }
  if (state != nullptr && !this->outro_cbs_.empty()) {
    return; // Block Master during outro on non-segmented lights
  }
  if (state == nullptr && this->is_spi_transport() &&
      this->has_active_parent_owned_segments_(this->has_outro()) &&
      (!this->has_outro() || !this->outro_parent_flush_allowed_)) {
    // Parent-coordinated SPI segments flush through the segment coordinator or
    // the single intentional outro parent flush. A generic nullptr write can
    // still be queued by ESPHome/legacy segment paths in the same visual frame,
    // causing extra SPI DMA frames and inflated LedFPS.
    this->note_segment_coord_write_skip();
    return;
  }

  // 1.2 Prevent Master paint from bleeding into OFF segments.
  // Skip during outro — the outro renders into the OFF segment's range;
  // scrubbing would erase the outro pixels before the DMA fires.
  if (!this->outro_cbs_.empty()) {
    // Outro is rendering — let it own the buffer, no scrub.
  } else if (!this->segment_light_states_.empty()) {
    // CFX-057: Feed WDT before segment scrub — at 8+ active runners the
    // scrub + SPI transmit chain can stall for 15ms+ per frame.
    esphome::App.feed_wdt();
    for (size_t i = 0; i < this->segment_light_states_.size(); i++) {
      auto *seg_state = this->segment_light_states_[i];
      // CFX-032: scrub on remote_values only; current_values may lag
      // by a frame with 0ms transitions, leaving stale lit pixels.
      if (!seg_state->remote_values.is_on()) {
        const auto &def = this->segment_defs_[i];
        for (int p = def.start; p < def.stop; p++) {
          if (p < this->size()) {
            (*this)[p] = esphome::Color::BLACK;
          }
        }
      }
    }
  }

  if (this->is_spi_transport() && this->spi_diag_write_logs_ < 6) {
    const char *light_name =
        (this->state_parent_ != nullptr) ? this->state_parent_->get_name().c_str()
                                         : "<spi>";
    ESP_LOGV(TAG,
             "SPI diag write_state[%u]: light=%s state_ptr=%p effect_active=%d "
             "outro=%u segs=%u in_flight=%d",
             static_cast<unsigned>(this->spi_diag_write_logs_), light_name,
             state, this->is_effect_active(),
             static_cast<unsigned>(this->outro_cbs_.size()),
             static_cast<unsigned>(this->segment_light_states_.size()),
             this->spi_tx_in_flight_);
    this->spi_diag_write_logs_++;
  }

  this->status_clear_warning();

  // Protect from refreshing too often
  uint32_t now = micros();
  if (*this->max_refresh_rate_ != 0 &&
      (now - this->last_refresh_) < *this->max_refresh_rate_) {
    this->perf_diag_total_refresh_defers_++;
    if (this->perf_diag_total_refresh_defers_ > this->perf_diag_max_refresh_defers_) {
      this->perf_diag_max_refresh_defers_ =
          static_cast<uint32_t>(this->perf_diag_total_refresh_defers_);
    }
    this->schedule_show();
    return;
  }

  if (this->transport_ == TRANSPORT_RMT) {
    const uint32_t stagger_gap_us = rmt_launch_stagger_gap_us();
    if (stagger_gap_us > 0 && g_last_rmt_launch_us != 0) {
      const uint32_t since_last_launch = now - g_last_rmt_launch_us;
      if (since_last_launch < stagger_gap_us) {
        const uint32_t gate_us = stagger_gap_us - since_last_launch;
        this->perf_diag_total_gate_us_ += gate_us;
        if (gate_us > this->perf_diag_max_gate_us_) {
          this->perf_diag_max_gate_us_ = gate_us;
        }
        if (this->perf_diag_pending_gate_defers_ < 255) {
          this->perf_diag_pending_gate_defers_++;
        }
        this->schedule_show();
        return;
      }
    }
  }

  this->last_refresh_ = now;
  this->mark_shown_();

#if defined(CFX_VISUALIZER_ENABLED) && defined(USE_WIFI)
  // Visualizer UDP broadcast. Runs BEFORE rmt_tx_wait_all_done so the
  // sendto() syscall cannot stall between the DMA wait and rmt_transmit.
  // Internal dev tool only -- not compiled unless CFX_VISUALIZER_ENABLED.
  if (this->visualizer_enabled_ && !this->visualizer_ip_.empty()) {
    if (this->socket_fd_ < 0)
      this->socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (this->socket_fd_ >= 0) {
      struct sockaddr_in dest_addr;
      dest_addr.sin_addr.s_addr = ::inet_addr(this->visualizer_ip_.c_str());
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = ::htons(this->visualizer_port_);
      size_t buf_len = this->get_buffer_size_();
      this->visualizer_pkt_.clear();
      this->visualizer_pkt_.reserve(buf_len + 1);
      this->visualizer_pkt_.push_back(VISUALIZER_TYPE_PIXELS);
      this->visualizer_pkt_.insert(this->visualizer_pkt_.end(), this->buf_,
                                   this->buf_ + buf_len);
      ::sendto(this->socket_fd_, this->visualizer_pkt_.data(),
               this->visualizer_pkt_.size(), 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
  }
#endif // CFX_VISUALIZER_ENABLED

  // Segment-owned parallel frames are coalesced per-lane by request_segment_flush().
  // We still need group-level coalescing so all lanes share a single DMA transfer.
  // request_parallel_group_flush_() fires immediately when every lane is ready
  // (zero extra latency), and service_parallel_group_flush_() provides the 2ms
  // safety timeout for mixed active/inactive lane configurations.
  // CFX-FIX: old code reset pending_mask and fired per-lane, producing 4 separate
  // DMA transfers per visual frame → Time ~40ms, LedFPS ~4× real, jitter 100%.
  if (this->is_parallel_transport() && state == nullptr && this->has_segments()) {
    if (!this->request_parallel_group_flush_()) {
      // This lane is ready but peer lanes have not finished their segments yet.
      // The group flush fires when the last lane calls write_state(nullptr).
      mark_committed_mono_idle_outputs(this);
      this->log_segment_coordinator_diag_();
      this->record_perf_diag_flush_(write_start_us, perf_diag_enabled,
                                    spi_cadence_diag_enabled,
                                    rmt_cadence_diag_enabled);
      return;
    }
    // All lanes ready — fire exactly once.
    this->commit_transmit_();
    mark_committed_mono_idle_outputs(this);
    this->log_segment_coordinator_diag_();
    goto record_write_perf;
  }

  // P3: Request coordinated DMA launch. The barrier fires commit_transmit_()
  // on all registered outputs once they have all requested in this tick window,
  // or after BARRIER_TIMEOUT_MS if some outputs are inactive. Single-output
  // setups: request_transmit() returns true immediately (no-op barrier).
  if (this->is_parallel_transport() && !this->request_parallel_group_flush_()) {
    mark_committed_mono_idle_outputs(this);
    this->log_segment_coordinator_diag_();
    this->record_perf_diag_flush_(write_start_us, perf_diag_enabled,
                                  spi_cadence_diag_enabled,
                                  rmt_cadence_diag_enabled);
    return;
  }
  if (this->is_parallel_transport() &&
      !this->request_parallel_shared_group_flush_()) {
    mark_committed_mono_idle_outputs(this);
    this->log_segment_coordinator_diag_();
    this->record_perf_diag_flush_(write_start_us, perf_diag_enabled,
                                  spi_cadence_diag_enabled,
                                  rmt_cadence_diag_enabled);
    return;
  }

  if (this->is_rmt_transport() && state == nullptr && this->has_segments() &&
      this->seg_last_flush_mask_ != 0 && !this->has_outro()) {
    // Parent-owned segment epochs are already converged inside this output.
    // Waiting for peer RMT outputs can phase-lock independent segment parents
    // into a slower cadence, especially on Classic ESP32 non-DMA RMT.
    this->commit_transmit_();
    mark_committed_mono_idle_outputs(this);
    this->log_segment_coordinator_diag_();
    goto record_write_perf;
  }

  if (!CFXTransmitBarrier::get().request_transmit(this)) {
    // Deferred or already fired from inside the barrier — do not double-flush.
    mark_committed_mono_idle_outputs(this);
    this->log_segment_coordinator_diag_();
    this->record_perf_diag_flush_(write_start_us, perf_diag_enabled,
                                  spi_cadence_diag_enabled,
                                  rmt_cadence_diag_enabled);
    return;
  }
  // Barrier not active (count_ < 2) — fire directly without coordination.
  this->commit_transmit_();
  mark_committed_mono_idle_outputs(this);
  this->log_segment_coordinator_diag_();

record_write_perf:
  if ((perf_diag_enabled || spi_cadence_diag_enabled ||
       rmt_cadence_diag_enabled) &&
      this->perf_diag_last_flush_valid_) {
    uint32_t queue_us = 0;
    if (this->perf_diag_last_show_request_us_ != 0) {
      queue_us = write_start_us - this->perf_diag_last_show_request_us_;
      this->perf_diag_total_queue_us_ += queue_us;
      if (queue_us > this->perf_diag_max_queue_us_) {
        this->perf_diag_max_queue_us_ = queue_us;
      }
    }
    this->perf_diag_last_show_request_us_ = 0;
    const uint32_t write_us = micros() - write_start_us;
    this->perf_diag_total_write_us_ += write_us;
    this->perf_diag_total_flush_us_ += this->perf_diag_last_flush_total_us_;
    this->perf_diag_total_tx_us_ += this->perf_diag_last_flush_tx_us_;
    this->perf_diag_total_seg_contrib_ += this->seg_last_flush_count_;
    this->perf_diag_total_gate_defers_ += this->perf_diag_pending_gate_defers_;
    if (write_us > this->perf_diag_max_write_us_) {
      this->perf_diag_max_write_us_ = write_us;
    }
    if (this->perf_diag_last_flush_total_us_ > this->perf_diag_max_flush_us_) {
      this->perf_diag_max_flush_us_ = this->perf_diag_last_flush_total_us_;
    }
    if (this->perf_diag_last_flush_tx_us_ > this->perf_diag_max_tx_us_) {
      this->perf_diag_max_tx_us_ = this->perf_diag_last_flush_tx_us_;
    }
    if (this->seg_last_flush_count_ > this->perf_diag_max_seg_contrib_) {
      this->perf_diag_max_seg_contrib_ = this->seg_last_flush_count_;
    }
    if (this->perf_diag_pending_gate_defers_ > this->perf_diag_max_gate_defers_) {
      this->perf_diag_max_gate_defers_ = this->perf_diag_pending_gate_defers_;
    }
    this->perf_diag_pending_gate_defers_ = 0;

    const uint32_t now_ms = esphome::millis();
    if (this->perf_diag_last_log_ms_ == 0) {
      this->perf_diag_last_log_ms_ = now_ms;
    } else if ((now_ms - this->perf_diag_last_log_ms_) >= 2000 &&
               this->perf_diag_flush_count_ > 0) {
      if (spi_cadence_diag_enabled) {
        this->log_spi_cadence_diag_();
      } else if (this->is_spi_transport()) {
        this->log_spi_cadence_diag_(true);
      } else if (perf_diag_enabled || rmt_cadence_diag_enabled) {
        this->log_rmt_cadence_diag_(true);
      } else {
        this->reset_perf_diag_();
      }
      this->perf_diag_last_log_ms_ = now_ms;
    }
  }
  this->seg_last_flush_count_ = 0;
  this->seg_last_flush_mask_ = 0;
  this->update_high_frequency_loop_request_();
}

// --- RMT Transport Flush ---

void CFXLightOutput::flush_rmt_() {
  const uint32_t flush_start_us = micros();

  if (this->is_failed() || this->buf_ == nullptr || this->rmt_buf_ == nullptr ||
      this->channel_ == nullptr || this->encoder_ == nullptr ||
      this->num_leds_ == 0) {
    this->perf_diag_last_flush_valid_ = false;
    return;
  }

  // P2: use non-blocking flag poll (fast path: ISR already cleared the flag).
  // Dynamic timeout tracks the physical wire time with extra callback headroom.
  const uint32_t physical_leds = this->get_rmt_physical_led_count_();
  uint32_t timeout_ms = (physical_leds * 30u / 1000u) + 20u;
  if (timeout_ms < 15u) timeout_ms = 15u;

  if (this->rmt_tx_in_flight_) {
    const bool segment_epoch_preflush =
        this->has_segments() && this->seg_last_flush_mask_ != 0 &&
        !this->has_outro();
    const bool whole_frame_preflush =
        this->seg_last_flush_mask_ == 0 && !this->has_outro();
    if (!this->rmt_dma_enabled_ &&
        (CFXTransmitBarrier::get().rmt_output_count() < 2 ||
         segment_epoch_preflush || whole_frame_preflush) &&
        this->wait_for_rmt_tx_(timeout_ms,
                               segment_epoch_preflush
                                   ? "segment-rmt-preflush"
                               : whole_frame_preflush
                                   ? "whole-rmt-preflush"
                                   : "single-rmt-preflush")) {
      this->rmt_flush_pending_ = false;
    } else {
      this->rmt_flush_pending_ = true;
      this->perf_diag_total_rmt_coalesced_flushes_++;
      this->perf_diag_last_flush_valid_ = false;
      this->update_high_frequency_loop_request_();
      return;
    }
  }

  if (!this->wait_for_rmt_tx_(timeout_ms, "flush")) {
    ESP_LOGE(TAG, "RMT TX timeout (Wait: %" PRIu32 "ms, physical LEDs: %" PRIu32 ")",
             timeout_ms, physical_leds);
    this->status_set_warning();
    return;
  }
  this->harvest_rmt_encoder_diag_();
  this->reset_rmt_encoder_diag_();

  if (this->rmt_dma_enabled_) {
    const uint32_t dma_guard_us =
        wait_for_dma_counter_idle_(&g_spi_dma_active_count, 6000u);
    if (dma_guard_us > 0) {
      this->perf_diag_total_dma_guard_wait_us_ += dma_guard_us;
      this->perf_diag_total_dma_guard_hits_++;
      if (dma_guard_us > this->perf_diag_max_dma_guard_wait_us_) {
        this->perf_diag_max_dma_guard_wait_us_ = dma_guard_us;
      }
      if (g_spi_dma_active_count > 0) {
        this->perf_diag_total_dma_guard_timeouts_++;
      }
    }
  }

  // Copy pixel buffer → RMT buffer and fire
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  const size_t logical_buffer_size = this->get_buffer_size_();
  const size_t transmit_buffer_size = this->get_rmt_transmit_buffer_size_();
  const uint8_t pixel_stride = this->get_pixel_stride_();
  uint8_t *rmt_dest = this->rmt_buf_;
  if (this->sacrificial_pixel_) {
    memset(rmt_dest, 0, pixel_stride);
    rmt_dest += pixel_stride;
  }
  memcpy(rmt_dest, this->buf_, logical_buffer_size);
  this->apply_power_scale_to_buffer_(this->rmt_buf_, transmit_buffer_size);
#else
  // Pre-5.3: encode bytes → RMT symbols manually
  const size_t transmit_buffer_size = this->get_rmt_transmit_buffer_size_();
  const uint8_t pixel_stride = this->get_pixel_stride_();
  size_t sz = 0;
  uint8_t *psrc = this->buf_;
  rmt_symbol_word_t *pdest = this->rmt_buf_;
  const uint8_t power_scale = this->get_power_transmit_scale_();
  while (this->sacrificial_pixel_ && sz < pixel_stride) {
    for (int i = 0; i < 8; i++) {
      pdest->val = this->params_.bit0.val;
      pdest++;
    }
    sz++;
  }
  while (sz < transmit_buffer_size) {
    uint8_t b = *psrc;
    if (power_scale < 255) {
      b = static_cast<uint8_t>((static_cast<uint16_t>(b) *
                                    power_scale +
                                127u) /
                               255u);
    }
    for (int i = 0; i < 8; i++) {
      pdest->val = (b & (1 << (7 - i))) ? this->params_.bit1.val
                                        : this->params_.bit0.val;
      pdest++;
    }
    sz++;
    psrc++;
  }
  if (this->params_.reset.duration0 > 0 || this->params_.reset.duration1 > 0) {
    pdest->val = this->params_.reset.val;
    pdest++;
  }
#endif

  // Fire-and-forget: rmt_transmit returns immediately; RMT handles the rest.
  rmt_transmit_config_t config;
  memset(&config, 0, sizeof(config));
  esp_err_t error = ESP_OK;
  this->rmt_tx_in_flight_ = true;
  if (this->rmt_dma_enabled_) {
    g_rmt_dma_active_count++;
  }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  error = rmt_transmit(this->channel_, this->encoder_, this->rmt_buf_,
                       transmit_buffer_size, &config);
#else
  size_t len =
      transmit_buffer_size * 8 +
      ((this->params_.reset.duration0 > 0 || this->params_.reset.duration1 > 0)
           ? 1
           : 0);
  error = rmt_transmit(this->channel_, this->encoder_, this->rmt_buf_,
                       len * sizeof(rmt_symbol_word_t), &config);
#endif

  if (error != ESP_OK) {
    this->rmt_tx_in_flight_ = false;
    if (this->rmt_dma_enabled_ && g_rmt_dma_active_count > 0) {
      g_rmt_dma_active_count--;
    }
    ESP_LOGE(TAG, "RMT TX error");
    this->status_set_warning();
    return;
  }
  // P2: flag was armed before transmit so a short transaction cannot complete
  // before the ISR has valid in-flight state to clear.
  const uint32_t rmt_launch_us = micros();
  if (this->perf_diag_last_rmt_tx_launch_us_ != 0) {
    const uint32_t interval_us =
        rmt_launch_us - this->perf_diag_last_rmt_tx_launch_us_;
    this->perf_diag_total_rmt_tx_launch_interval_us_ += interval_us;
    this->perf_diag_rmt_tx_launch_interval_count_++;
    if (interval_us > this->perf_diag_max_rmt_tx_launch_interval_us_) {
      this->perf_diag_max_rmt_tx_launch_interval_us_ = interval_us;
    }
  }
  this->perf_diag_last_rmt_tx_launch_us_ = rmt_launch_us;
  this->perf_diag_total_rmt_tx_launches_++;
  this->perf_diag_flush_count_++;
  this->record_led_frame_();
  if (this->power_manager_ != nullptr) {
    this->power_manager_->record_output_frame(this);
  }
  this->status_clear_warning();
  this->perf_diag_last_flush_total_us_ = micros() - flush_start_us;
  this->perf_diag_last_flush_tx_us_ = 0;
  this->perf_diag_last_flush_valid_ = true;
}

// --- SPI Transport Flush ---

void CFXLightOutput::flush_spi_() {
  // CFX-057: One-shot crash telemetry — logs reset reason on first flush call.
  // Must be here (not setup) because API isn't connected during setup.
  static bool telemetry_logged = false;
  if (!telemetry_logged) {
    telemetry_logged = true;
    const char *rst = "?";
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON:  rst = "POWER_ON"; break;
      case ESP_RST_SW:       rst = "SW"; break;
      case ESP_RST_PANIC:    rst = "PANIC"; break;
      case ESP_RST_INT_WDT:  rst = "INT_WDT"; break;
      case ESP_RST_TASK_WDT: rst = "TASK_WDT"; break;
      case ESP_RST_WDT:      rst = "WDT"; break;
      case ESP_RST_BROWNOUT: rst = "BROWNOUT"; break;
      default: break;
    }
    ESP_LOGW(TAG, "CFX-057 TELEMETRY: reset_reason=%s(%d) stack_hwm=%u heap=%u",
             rst, (int)esp_reset_reason(),
             (unsigned)uxTaskGetStackHighWaterMark(nullptr),
             (unsigned)esp_get_free_heap_size());
  }

  const uint32_t timeout_ms = this->get_spi_frame_timeout_ms_();
  const uint32_t flush_start_us = micros();
  if (this->has_active_parent_owned_segments_(this->has_outro()) &&
      this->perf_diag_last_spi_flush_start_us_ != 0) {
    constexpr uint32_t SPI_SEGMENT_DUPLICATE_WINDOW_US =
        (FRAMETIME * 1000u * 3u) / 4u;
    if ((flush_start_us - this->perf_diag_last_spi_flush_start_us_) <
        SPI_SEGMENT_DUPLICATE_WINDOW_US) {
      this->note_segment_coord_write_skip();
      return;
    }
  }
  this->perf_diag_flush_count_++;
  if (this->perf_diag_last_spi_flush_start_us_ != 0) {
    const uint32_t interval_us =
        flush_start_us - this->perf_diag_last_spi_flush_start_us_;
    this->perf_diag_total_spi_flush_interval_us_ += interval_us;
    this->perf_diag_spi_flush_interval_count_++;
    if (interval_us > this->perf_diag_max_spi_flush_interval_us_) {
      this->perf_diag_max_spi_flush_interval_us_ = interval_us;
    }
  }
  this->perf_diag_last_spi_flush_start_us_ = flush_start_us;
  if (this->spi_diag_flush_logs_ < 6) {
    const char *light_name =
        (this->state_parent_ != nullptr) ? this->state_parent_->get_name().c_str()
                                         : "<spi>";
    ESP_LOGV(TAG,
             "SPI diag flush[%u]: light=%s frame=%u timeout=%" PRIu32
             " in_flight=%d bri=%u",
             static_cast<unsigned>(this->spi_diag_flush_logs_), light_name,
             static_cast<unsigned>(this->get_spi_frame_size_()), timeout_ms,
             this->spi_tx_in_flight_, static_cast<unsigned>(this->tracked_brightness_));
    this->spi_diag_flush_logs_++;
  }
  if (!this->wait_for_spi_tx_(timeout_ms, "flush")) {
    return;
  }

  // Do not serialize SPI behind a full long RMT frame: a 600 LED RGBW RMT
  // transfer is ~24ms on the wire, which would pull every mixed transport down
  // to ~40 FPS. A short guard protects the visible leading edge from GDMA/SPI
  // overlap while allowing SPI to proceed during the tail of oversized RMT
  // frames.
  const uint32_t dma_guard_us =
      wait_for_dma_counter_idle_(&g_rmt_dma_active_count, 3500u);
  if (dma_guard_us > 0) {
    this->perf_diag_total_dma_guard_wait_us_ += dma_guard_us;
    this->perf_diag_total_dma_guard_hits_++;
    if (dma_guard_us > this->perf_diag_max_dma_guard_wait_us_) {
      this->perf_diag_max_dma_guard_wait_us_ = dma_guard_us;
    }
    if (g_rmt_dma_active_count > 0) {
      this->perf_diag_total_dma_guard_timeouts_++;
    }
  }

  const uint32_t pack_start_us = micros();
  uint8_t *ptr = this->spi_frame_buf_;
  const uint8_t power_scale = this->get_power_transmit_scale_();

  // 1. Start frame: 32 bits of 0x00
  *ptr++ = 0x00;
  *ptr++ = 0x00;
  *ptr++ = 0x00;
  *ptr++ = 0x00;

  // 2. LED frames: 0xFF, Blue, Green, Red
  uint8_t *src = this->buf_;
  for (int i = 0; i < this->num_leds_; i++) {
    *ptr++ = 0xFF; // Global brightness: max (11111111)

    // Source buffer `buf_` is already ordered according to `rgb_order_`
    // (handled by get_view_internal / ESPColorView).
    // For APA102/SK9822 matching fastled behavior, we default to BGR order
    // in light.py, so buf_[0] is B, buf_[1] is G, buf_[2] is R.
    for (uint8_t c = 0; c < 3; c++) {
      uint8_t value = *src++;
      if (power_scale < 255) {
        value = static_cast<uint8_t>((static_cast<uint16_t>(value) *
                                          power_scale +
                                      127u) /
                                     255u);
      }
      *ptr++ = value;
    }
  }

  // 3. End frame
  size_t end_size = this->get_spi_end_frame_size_();
  uint8_t end_byte = this->get_spi_end_frame_byte_();
  for (size_t i = 0; i < end_size; i++) {
    *ptr++ = end_byte;
  }
  const uint32_t pack_us = micros() - pack_start_us;
  this->perf_diag_total_spi_pack_us_ += pack_us;
  if (pack_us > this->perf_diag_max_spi_pack_us_) {
    this->perf_diag_max_spi_pack_us_ = pack_us;
  }


  // Async fire-and-forget: queue_trans returns immediately (~5us).
  // DMA drives the wire in hardware while the CPU is free for the next frame.
  // The previous transaction was drained by wait_for_spi_tx_() at the top of
  // this function, guaranteeing spi_frame_buf_ is safe to overwrite.
  const uint32_t tx_start_us = micros();
  esphome::App.feed_wdt();

  memset(&this->spi_trans_, 0, sizeof(this->spi_trans_));
  this->spi_trans_.length = this->get_spi_frame_size_() * 8;
  this->spi_trans_.tx_buffer = this->spi_frame_buf_;
  g_spi_dma_active_count++;
  esp_err_t err = spi_device_queue_trans(this->spi_device_, &this->spi_trans_, 0);

  const uint32_t tx_queue_us = micros();
  esphome::App.feed_wdt();

  if (err != ESP_OK) {
    if (g_spi_dma_active_count > 0) {
      g_spi_dma_active_count--;
    }
    this->spi_queue_error_count_++;
    ESP_LOGW(TAG,
             "SPI TX queue failed (err=%d, frame=%u bytes, waits=%" PRIu32
             ", timeouts=%" PRIu32 ", queue_err=%" PRIu32 ")",
             err, static_cast<unsigned>(this->get_spi_frame_size_()),
             this->spi_wait_count_, this->spi_wait_timeout_count_,
             this->spi_queue_error_count_);
    this->status_set_warning();
    // spi_tx_in_flight_ stays false: next flush skips wait and retries.
  } else {
    // Mark in-flight: wait_for_spi_tx_() at next flush start drains the result
    // and records actual wire time via the DMA completion timestamp.
    this->spi_tx_in_flight_ = true;
    this->spi_last_flush_ms_ = esphome::millis();
    this->record_led_frame_();
    if (this->power_manager_ != nullptr) {
      this->power_manager_->record_output_frame(this);
    }
    this->status_clear_warning();
    // Record queue-submit latency (not wire time — that is in wait_for_spi_tx_).
    const uint32_t queue_us = tx_queue_us - tx_start_us;
    this->perf_diag_total_spi_queue_us_ += queue_us;
    if (queue_us > this->perf_diag_max_spi_queue_us_) {
      this->perf_diag_max_spi_queue_us_ = queue_us;
    }
    this->perf_diag_last_flush_tx_us_ = queue_us;
    this->perf_diag_last_flush_total_us_ = tx_queue_us - flush_start_us;
    this->perf_diag_last_flush_valid_ = true;
  }
}

void CFXLightOutput::send_visualizer_metadata(const std::string &name,
                                              const std::string &palette) {
#if defined(CFX_VISUALIZER_ENABLED) && defined(USE_WIFI)
  if (this->visualizer_enabled_ && !this->visualizer_ip_.empty()) {
    if (this->socket_fd_ < 0) {
      this->socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    }
    if (this->socket_fd_ >= 0) {
      struct sockaddr_in dest_addr;
      dest_addr.sin_addr.s_addr = ::inet_addr(this->visualizer_ip_.c_str());
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = ::htons(this->visualizer_port_);

      std::vector<uint8_t> pkt;
      pkt.push_back(VISUALIZER_TYPE_METADATA);
      pkt.push_back('C');
      pkt.push_back('F');
      pkt.push_back('X'); // Magic Header
      pkt.insert(pkt.end(), name.begin(), name.end());
      if (!palette.empty()) {
        pkt.push_back(':');
        pkt.insert(pkt.end(), palette.begin(), palette.end());
      }

      ::sendto(this->socket_fd_, pkt.data(), pkt.size(), 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
  }
#endif // CFX_VISUALIZER_ENABLED
}

// --- Color View (Maps ESPHome pixel access to our buffer) ---

light::ESPColorView CFXLightOutput::get_view_internal(int32_t index) const {
  static uint8_t dummy_r = 0;
  static uint8_t dummy_g = 0;
  static uint8_t dummy_b = 0;
  static uint8_t dummy_w = 0;
  static uint8_t dummy_effect = 0;

  if (this->buf_ == nullptr || this->effect_data_ == nullptr || index < 0 ||
      index >= this->size()) {
    if (!this->unsafe_view_logged_) {
      ESP_LOGW(TAG,
               "Unsafe pixel view redirected to dummy pixel "
               "(pin=%u, idx=%d, size=%d, buf=%p, effect=%p, failed=%d)",
               this->pin_, index, this->size(), this->buf_,
               this->effect_data_, this->is_failed());
      this->unsafe_view_logged_ = true;
    }
    return {&dummy_r, &dummy_g, &dummy_b, &dummy_w, &dummy_effect,
            &this->correction_};
  }

  int32_t r = 0, g = 0, b = 0;
  switch (this->rgb_order_) {
  case ORDER_RGB:
    r = 0;
    g = 1;
    b = 2;
    break;
  case ORDER_RBG:
    r = 0;
    g = 2;
    b = 1;
    break;
  case ORDER_GRB:
    r = 1;
    g = 0;
    b = 2;
    break;
  case ORDER_GBR:
    r = 2;
    g = 0;
    b = 1;
    break;
  case ORDER_BGR:
    r = 2;
    g = 1;
    b = 0;
    break;
  case ORDER_BRG:
    r = 1;
    g = 2;
    b = 0;
    break;
  }
  uint8_t multiplier = (this->is_rgbw_ || this->is_wrgb_) ? 4 : 3;
  uint8_t white = this->is_wrgb_ ? 0 : 3;

  return {this->buf_ + (index * multiplier) + r + this->is_wrgb_,
          this->buf_ + (index * multiplier) + g + this->is_wrgb_,
          this->buf_ + (index * multiplier) + b + this->is_wrgb_,
          (this->is_rgbw_ || this->is_wrgb_)
              ? this->buf_ + (index * multiplier) + white
              : nullptr,
          &this->effect_data_[index],
          &this->correction_};
}

// --- Config Dump ---

void CFXLightOutput::dump_config() {
  const char *chipset_str;
  switch (this->chipset_) {
  case CHIPSET_WS2812X:
    chipset_str = "WS2812X";
    break;
  case CHIPSET_SK6812:
    chipset_str = "SK6812";
    break;
  case CHIPSET_WS2811:
    chipset_str = "WS2811";
    break;
  case CHIPSET_APA102:
    chipset_str = "APA102";
    break;
  case CHIPSET_SK9822:
    chipset_str = "SK9822";
    break;
  default:
    chipset_str = "UNKNOWN";
    break;
  }
  const char *order_str;
  switch (this->rgb_order_) {
  case ORDER_RGB:
    order_str = "RGB";
    break;
  case ORDER_RBG:
    order_str = "RBG";
    break;
  case ORDER_GRB:
    order_str = "GRB";
    break;
  case ORDER_GBR:
    order_str = "GBR";
    break;
  case ORDER_BGR:
    order_str = "BGR";
    break;
  case ORDER_BRG:
    order_str = "BRG";
    break;
  default:
    order_str = "UNKNOWN";
    break;
  }
  
  if (this->transport_ == TRANSPORT_SPI) {
    const int spi_host_num =
        (resolve_spi_host_(this->spi_host_) == SPI2_HOST) ? 2 : 3;
    ESP_LOGCONFIG(TAG,
                  "CFXLight (SPI):\n"
                  "  Data Pin: GPIO%u\n"
                  "  Clock Pin: GPIO%u\n"
                  "  SPI Host: SPI%d_HOST\n"
                  "  Speed: %" PRIu32 " Hz\n"
                  "  Frame Size: %u bytes\n"
                  "  TX Timeout: %" PRIu32 " ms\n"
                  "  TX Mode: async queue (GDMA-backed on S3/S2)\n"
                  "  Chipset: %s\n"
                  "  LEDs: %u\n"
                  "  RGB Order: %s",
                  this->spi_data_pin_, this->spi_clock_pin_,
                  spi_host_num,
                  this->spi_speed_hz_,
                  static_cast<unsigned>(this->get_spi_frame_size_()),
                  this->get_spi_frame_timeout_ms_(), chipset_str,
                  this->num_leds_, order_str);
  } else if (this->transport_ == TRANSPORT_PARALLEL) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    ESP_LOGCONFIG(TAG,
                  "CFXLight (Parallel):\n"
                  "  Group: %s\n"
                  "  Lane Pin: GPIO%u\n"
                  "  Chipset: %s\n"
                  "  LEDs: %u\n"
                  "  RGBW: %s\n"
                  "  RGB Order: %s\n"
                  "  Backend: Classic I2S/LCD streaming\n"
                  "  Chunk: %u LEDs",
                  this->parallel_group_.c_str(), this->pin_, chipset_str,
                  this->num_leds_, this->is_rgbw_ ? "yes" : "no",
                  order_str, PARALLEL_CLASSIC_CHUNK_LEDS);
#else
    ESP_LOGCONFIG(TAG,
                  "CFXLight (Parallel):\n"
                  "  Group: %s\n"
                  "  Lane Pin: GPIO%u\n"
                  "  Chipset: %s\n"
                  "  LEDs: %u\n"
                  "  RGBW: %s\n"
                  "  RGB Order: %s\n"
                  "  Backend: LCD/I80\n"
                  "  Internal WR: GPIO%u (%s)\n"
                  "  Internal D/C: GPIO%u (%s)",
                  this->parallel_group_.c_str(), this->pin_, chipset_str,
                  this->num_leds_, this->is_rgbw_ ? "yes" : "no",
                  order_str, this->parallel_strobe_pin_,
                  this->parallel_strobe_pin_auto_ ? "auto" : "user",
                  this->parallel_dc_pin_,
                  this->parallel_dc_pin_auto_ ? "auto" : "user");
#endif
  } else {
    ESP_LOGCONFIG(TAG,
                  "CFXLight (RMT):\n"
                  "  Pin: GPIO%u\n"
                  "  Chipset: %s\n"
                  "  Visible LEDs: %u\n"
                  "  Physical LEDs: %" PRIu32 "\n"
                  "  Sacrificial Pixel: %s\n"
                  "  RGBW: %s\n"
                  "  RGB Order: %s\n"
                  "  RMT Symbols: %" PRIu32 "\n"
                  "  RMT TX Mode: %s\n"
                  "  RMT mem_block_symbols: %" PRIu32,
                  this->pin_, chipset_str, this->num_leds_,
                  this->get_rmt_physical_led_count_(),
                  this->sacrificial_pixel_ ? "yes" : "no",
                  this->is_rgbw_ ? "yes" : "no", order_str,
                  this->rmt_symbols_,
                  this->rmt_dma_enabled_ ? rmt_dma_backend_label() : "non-DMA",
                  this->rmt_mem_block_symbols_);
  }

  // Segment layout
  if (!this->segment_defs_.empty()) {
    ESP_LOGCONFIG(TAG, "  Segments: %u", this->segment_defs_.size());
    for (size_t i = 0; i < this->segment_defs_.size(); i++) {
      const auto &seg = this->segment_defs_[i];
      ESP_LOGCONFIG(
          TAG, "    [%u] \"%s\": %u→%u (%u LEDs)%s  intro=%u outro=%u", i,
          seg.id.c_str(), seg.start, seg.stop, seg.stop - seg.start,
          seg.mirror ? " [MIRROR]" : "", this->resolve_intro_mode(seg),
          this->resolve_outro_mode(seg));
    }
  }

  constexpr uint32_t cfx_heap_floor = 15000   // Base System Margin
#ifdef USE_WIFI
                                      + 30000 // Wi-Fi TX/RX buffers + LwIP
#endif
#ifdef USE_API
                                      + 10000 // ESPHome HA API overhead
#endif
#if defined(USE_BLUETOOTH_PROXY) || defined(USE_ESP32_BLE_SERVER) || defined(USE_ESP32_BLE_TRACKER) || defined(USE_ESP32_BLE_CLIENT)
                                      + 20000 // BLE Dynamic Buffers
#endif
      ;
  ESP_LOGCONFIG(TAG, "  System CFX Heap Floor dynamically set to: %u B", (unsigned)cfx_heap_floor);
}

float CFXLightOutput::get_setup_priority() const {
  return setup_priority::HARDWARE;
}

} // namespace cfx_light
} // namespace esphome

#endif // USE_ESP32
