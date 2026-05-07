/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * CFXLight - Async DMA LED Strip Driver for ESPHome
 *
 * Phase 1: Skeleton — pixel buffer, get_view_internal, basic setup/show.
 * Phase 2 will add async DMA fire-and-forget via rmt_transmit.
 */

#include "cfx_light.h"
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
#include <cmath>
#include <driver/gpio.h>
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

std::vector<CFXVirtualSegmentLight *> CFXVirtualSegmentLight::all_segments;

static const size_t RMT_SYMBOLS_PER_BYTE = 8;
static uint32_t g_last_rmt_launch_us = 0;
static uint32_t g_rmt_launch_seq = 0;
static volatile uint32_t g_rmt_dma_active_count = 0;
static volatile uint32_t g_spi_dma_active_count = 0;

static uint32_t rmt_launch_stagger_gap_us() {
#if defined(CONFIG_IDF_TARGET_ESP32)
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
  this->perf_diag_total_dma_guard_wait_us_ = 0;
  this->perf_diag_total_dma_guard_hits_ = 0;
  this->perf_diag_total_dma_guard_timeouts_ = 0;
  this->perf_diag_show_request_interval_count_ = 0;
  this->perf_diag_last_show_request_interval_us_ = 0;
  this->perf_diag_spi_flush_interval_count_ = 0;
  this->perf_diag_last_spi_flush_start_us_ = 0;
}

void CFXLightOutput::log_spi_cadence_diag_(bool force) {
  if (!this->is_spi_transport()) {
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

  ESP_LOGI(TAG,
           "CFX spi_cad[%s] frames=%" PRIu32
           " avg_us(dt=%" PRIu32 " show_q=%" PRIu32
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
           light_name, frames, avg_flush_dt_us, avg_show_queue_us,
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

void CFXLightOutput::log_rmt_cadence_diag_() {
  if (this->is_spi_transport()) {
    return;
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

  ESP_LOGI(TAG,
           "CFX rmt_cad[%s] frames=%" PRIu32
           " avg_us(show_q=%" PRIu32 " write=%" PRIu32
           " flush=%" PRIu32 " wait=%" PRIu32 ")"
           " max_us(show_q=%" PRIu32 " write=%" PRIu32
           " flush=%" PRIu32 " wait=%" PRIu32 ")"
           " req_us(avg=%" PRIu32 " max=%" PRIu32 ")"
           " guard(avg=%" PRIu32 " max=%" PRIu32
           " hits=%" PRIu64 " timeout=%" PRIu64 ")"
           " rmt(tx=%" PRIu64 " coalesce=%" PRIu64 " wait=%" PRIu32
           " timeout=%" PRIu32 " cb=%" PRIu64 " starve=%" PRIu64
           "/%" PRIu32 " reset=%" PRIu64 "/%" PRIu32
           " min_free=%" PRIu32 " in_flight=%d)",
           light_name, frames, avg_show_queue_us, avg_write_us, avg_flush_us,
           avg_wait_us, this->perf_diag_max_queue_us_,
           this->perf_diag_max_write_us_, this->perf_diag_max_flush_us_,
           this->perf_diag_max_wait_us_, avg_show_req_dt_us,
           this->perf_diag_max_show_request_interval_us_,
           avg_guard_us, this->perf_diag_max_dma_guard_wait_us_,
           this->perf_diag_total_dma_guard_hits_,
           this->perf_diag_total_dma_guard_timeouts_,
           this->perf_diag_total_rmt_tx_launches_,
           this->perf_diag_total_rmt_coalesced_flushes_, this->rmt_wait_count_,
           this->rmt_wait_timeout_count_,
           this->perf_diag_total_rmt_callback_count_,
           this->perf_diag_total_rmt_starve_count_,
           this->perf_diag_max_rmt_starve_count_,
           this->perf_diag_total_rmt_reset_starve_count_,
           this->perf_diag_max_rmt_reset_starve_count_,
           this->perf_diag_min_rmt_symbols_free_ == UINT32_MAX
               ? 0
               : this->perf_diag_min_rmt_symbols_free_,
           this->rmt_tx_in_flight_);
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
      this->seg_coord_epochs_ == 0) {
    this->seg_batch_diag_last_log_ms_ = now_ms;
    return;
  }

  this->seg_partial_frame_suppressed_ = 0;
  this->seg_missed_epoch_count_ = 0;
  this->seg_clean_epoch_suppressed_ = 0;
  this->seg_coord_apply_skips_ = 0;
  this->seg_coord_write_skips_ = 0;
  this->seg_coord_epochs_ = 0;
  this->seg_coord_rendered_segments_ = 0;
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
  if (output != nullptr) {
    for (auto *seg_out : CFXVirtualSegmentLight::all_segments) {
      if (seg_out != output) {
        continue;
      }
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
  auto *cfx_stub = dynamic_cast<chimera_fx::CFXEffectStub *>(effect);
  if (cfx_stub != nullptr) {
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
    auto *effect = resolve_active_cfx_effect(this->master_light_state_);
    if (effect != nullptr) {
      effect->log_mono_idle_sleep();
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
        effect->log_mono_idle_sleep();
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

bool CFXLightOutput::has_active_parent_owned_segments_() const {
  if (this->has_outro()) {
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

void CFXLightOutput::refresh_segment_coordination_mask_() {
  uint8_t mask = 0;
  if (this->has_segments() && !this->has_outro()) {
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
    this->refresh_segment_coordination_mask_();
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

bool CFXLightOutput::service_segment_render_coordinator_() {
  if (!this->has_segments() || this->has_outro()) {
    this->apply_segment_coordination_loop_state_(0);
    this->apply_master_segment_coordination_loop_state_();
    this->apply_mono_idle_loop_state_(0);
    return false;
  }

  const uint64_t now = static_cast<uint64_t>(esphome::millis());
  if (!this->segment_coord_schedule_dirty_ && this->segment_coord_owned_mask_ != 0 &&
      this->segment_coord_next_due_ms_ != 0 && now < this->segment_coord_next_due_ms_) {
    return false;
  }
  uint8_t mask = 0;
  uint8_t count = 0;
  uint64_t next_due = 0;
  this->refresh_segment_coordination_mask_();
  const uint8_t segment_idle_mask =
      this->collect_clean_mono_idle_segment_mask_();
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
    if (!slot.effect->parent_coordinated_segment_due(now)) {
      const uint64_t slot_due = slot.due_at != 0
                                    ? slot.due_at
                                    : (slot.effect->get_update_interval() + now);
      if (next_due == 0 || slot_due < next_due) {
        next_due = slot_due;
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
    slot.due_at = now + slot.effect->get_update_interval();
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

  // P1: pass the transport constraint directly to the scheduler batch — no
  // global state mutation. SPI batches run sequentially on Core 1 (SPI driver
  // is not safe across cores); RMT batches keep the dual-core split.
  const bool complete =
      chimera_fx::CFXScheduler::get().service_runners(
          this->segment_coord_runners_, this->is_spi_transport());
  esphome::App.feed_wdt();

  if (!complete) {
    this->perf_diag_total_partial_flushes_++;
    this->seg_partial_frame_suppressed_++;
    this->seg_missed_epoch_count_ += count;
    if (count > this->perf_diag_max_partial_missing_) {
      this->perf_diag_max_partial_missing_ = count;
    }
    this->log_segment_coordinator_diag_();
    return true;
  }

  for (auto *runner : this->segment_coord_runners_) {
    if (runner != nullptr) {
      runner->diagnostics.flush_log();
    }
  }
  this->seg_coord_epochs_++;
  this->seg_coord_rendered_segments_ += count;
  this->flush_segment_coordinator_epoch_(mask, count);
  return true;
}

void CFXLightOutput::flush_segment_coordinator_epoch_(uint8_t mask,
                                                     uint8_t count) {
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
  this->flush_parent_owned_segment_epoch_direct_(mask, count);
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
    this->schedule_show();
    return;
  }

  this->last_refresh_ = now;
  this->mark_shown_();

  if (this->transport_ == TRANSPORT_SPI) {
    esphome::App.feed_wdt();
    this->flush_spi_();
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
}

// --- Core Control Loop & Initialization ---

// CFX-025: Destructor closes the visualizer UDP socket if it was opened.
// ESP32 has a small FD pool (~5 sockets under default ESP-IDF config). Without
// this, each OTA cycle or component teardown leaks one FD, eventually causing
// all subsequent socket() calls to fail. close() is available via
// lwip/sockets.h.
CFXLightOutput::~CFXLightOutput() {
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
    for (size_t i = 0; i < RMT_SYMBOLS_PER_BYTE; i++) {
      symbols[i] =
          (bytes[index] & (1 << (7 - i))) ? params->bit1 : params->bit0;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 1)
    if ((index + 1) >= size && params->reset.duration0 == 0 &&
        params->reset.duration1 == 0) {
      *done = true;
    }
#endif
    return RMT_SYMBOLS_PER_BYTE;
  }

  // Send reset pulse
  if (symbols_free < 1) {
    params->diag_reset_starve_count++;
    return 0;
  }
  symbols[0] = params->reset;
  *done = true;
  return 1;
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
  *static_cast<volatile bool *>(ctx) = false;
  if (g_rmt_dma_active_count > 0) {
    g_rmt_dma_active_count--;
  }
  return false;
}
#endif

void CFXLightOutput::setup() {
  size_t buffer_size = this->get_buffer_size_();

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
  } else {
    ESP_LOGI(TAG,
             "CFXLight ready: %u LEDs on GPIO%u (%s, rmt_symbols=%u, "
             "mem_block_symbols=%u)",
             this->num_leds_, this->pin_,
             this->rmt_dma_enabled_ ? rmt_dma_backend_label() : "non-DMA",
             this->rmt_symbols_, this->rmt_mem_block_symbols_);
  }
}

// --- RMT Transport Setup (extracted from setup()) ---

void CFXLightOutput::setup_rmt_() {
  size_t buffer_size = this->get_buffer_size_();
  this->rmt_dma_enabled_ = false;
  this->rmt_mem_block_symbols_ = 0;
  this->rmt_alloc_index_ = 0;

  // Allocate RMT transmission buffer
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  RAMAllocator<uint8_t> rmt_alloc(RAMAllocator<uint8_t>::ALLOC_INTERNAL);
  this->rmt_buf_ = rmt_alloc.allocate(buffer_size);
#else
  RAMAllocator<rmt_symbol_word_t> rmt_allocator(
      RAMAllocator<rmt_symbol_word_t>::ALLOC_INTERNAL);
  this->rmt_buf_ = rmt_allocator.allocate(buffer_size * 8 + 1);
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
  channel.intr_priority = 0;

  // DMA only supported on ESP32-S3 and ESP32-P4
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
  {
    static uint32_t s_rmt_alloc_count = 0;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    static bool s_rmt_dma_claimed = false;
    // ESP32-S3 RMT GDMA is effectively a single preferred slot in ESP-IDF's
    // allocator; probing a second DMA channel emits a driver error before
    // returning ESP_ERR_NOT_FOUND, so later strips go straight to non-DMA.
    const bool skip_dma_probe = s_rmt_dma_claimed;
#else
    const bool skip_dma_probe = false;
#endif
    this->rmt_alloc_index_ = ++s_rmt_alloc_count;

    if (skip_dma_probe) {
      channel.flags.with_dma = false;
      channel.mem_block_symbols = this->rmt_symbols_;
      ESP_LOGI(TAG,
               "RMT alloc #%" PRIu32
               ": pin=%u GDMA skipped (RMT GDMA slot already claimed) "
               "mem_block_symbols=%u rmt_symbols=%u hw_tx_slots=%d",
               this->rmt_alloc_index_, this->pin_,
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
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        s_rmt_dma_claimed = true;
#endif
      } else {
        ESP_LOGW(TAG,
                 "RMT %s unavailable for pin=%u (alloc #%" PRIu32
                 " of %d hw slots, err=%d) - falling back to non-DMA "
                 "(mem_block_symbols=%u). "
                 "Check for other RMT consumers (remote_transmitter, "
                 "neopixelbus, status_led, ir_transmitter).",
                 rmt_dma_backend_label(), this->pin_, this->rmt_alloc_index_,
                 SOC_RMT_TX_CANDIDATES_PER_GROUP, (int) err,
                 this->rmt_symbols_);
        this->channel_ = nullptr;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        s_rmt_dma_claimed = true;
#endif
        channel.flags.with_dma = false;
        channel.mem_block_symbols = this->rmt_symbols_;
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
  rmt_simple_encoder_config_t encoder;
  memset(&encoder, 0, sizeof(encoder));
  encoder.callback = encoder_callback;
  encoder.arg = &this->params_;
  encoder.min_chunk_size = RMT_SYMBOLS_PER_BYTE;
  if (rmt_new_simple_encoder(&encoder, &this->encoder_) != ESP_OK) {
    ESP_LOGE(TAG, "Simple encoder creation failed");
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
    if (rmt_tx_register_event_callbacks(this->channel_, &rmt_cbs,
                                        (void *)&this->rmt_tx_in_flight_) != ESP_OK) {
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


  ESP_LOGI(TAG,
           "SPI transport ready: frame=%u bytes, est_tx_timeout=% " PRIu32
           " ms, mode=async_queue, dual_core=enabled",
           static_cast<unsigned>(frame_size), this->get_spi_frame_timeout_ms_());
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

  if (this->is_syncing_)
    return;
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
  this->log_spi_cadence_diag_();

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
  if (!this->outro_cbs_.empty()) {
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

    // Force direct DMA flush of the frame!
    // We cannot use schedule_show() here because ESPHome's LightState loop
    // is disabled when the light is turned off, meaning it will never poll us.
    this->write_state(nullptr);

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
      this->write_state(nullptr);
      this->release_outro_callback_storage_();
    }
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
      active_count++;
      if (this->seg_request_generation_[i] != this->seg_flushed_generation_[i]) {
        ready_count++;
      }
    }
    uint32_t wait_target_ms = 2;
    // Segmented parents are visibly less tolerant of partial-frame
    // presentation, so bias them harder toward waiting for full convergence
    // without ever blocking indefinitely.
    if (segment_count > 0) {
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
        this->log_segment_coordinator_diag_();
        goto segment_flush_done;
      }
      this->write_state(nullptr);
    }
  }

segment_flush_done:
#ifdef USE_CFX_EVENTS
  chimera_fx::CFXEventManager::get().flush_pending();
#endif
}

// --- Update State (Handles Brightness & Solid Colors) ---

void CFXLightOutput::update_state(light::LightState *state) {
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

  this->all() = c;
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
  } else {
    g_last_rmt_launch_us = micros();
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
  const uint32_t write_start_us = micros();
  this->perf_diag_last_flush_valid_ = false;
  this->perf_diag_last_flush_total_us_ = 0;
  this->perf_diag_last_flush_tx_us_ = 0;

  if (!this->has_outro() &&
      ((state != nullptr && active_cfx_effect_is_clean_mono_idle(state)) ||
       (state == nullptr && this->seg_last_flush_mask_ == 0 &&
        all_active_cfx_effects_clean_mono_idle(this)))) {
    this->seg_clean_epoch_suppressed_++;
    this->log_segment_coordinator_diag_();
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

  // P3: Request coordinated DMA launch. The barrier fires commit_transmit_()
  // on all registered outputs once they have all requested in this tick window,
  // or after BARRIER_TIMEOUT_MS if some outputs are inactive. Single-output
  // setups: request_transmit() returns true immediately (no-op barrier).
  if (!CFXTransmitBarrier::get().request_transmit(this)) {
    // Deferred or already fired from inside the barrier — do not double-flush.
    mark_committed_mono_idle_outputs(this);
    this->log_segment_coordinator_diag_();
    return;
  }
  // Barrier not active (count_ < 2) — fire directly without coordination.
  this->commit_transmit_();
  mark_committed_mono_idle_outputs(this);
  this->log_segment_coordinator_diag_();

  if ((perf_diag_enabled || spi_cadence_diag_enabled) &&
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
    if (!this->is_spi_transport()) {
      this->perf_diag_flush_count_++;
    }

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
      } else if (perf_diag_enabled) {
        this->log_rmt_cadence_diag_();
      } else {
        this->reset_perf_diag_();
      }
      this->perf_diag_last_log_ms_ = now_ms;
    }
  }
}

// --- RMT Transport Flush ---

void CFXLightOutput::flush_rmt_() {
  const uint32_t flush_start_us = micros();

  if (this->rmt_tx_in_flight_) {
    this->rmt_flush_pending_ = true;
    this->perf_diag_total_rmt_coalesced_flushes_++;
    this->perf_diag_last_flush_valid_ = false;
    return;
  }

  // P2: use non-blocking flag poll (fast path: ISR already cleared the flag).
  // Dynamic timeout matches the old rmt_tx_wait_all_done() budget so the
  // fallback blocking recovery fires under identical worst-case conditions.
  uint32_t timeout_ms = (this->num_leds_ * 30u / 1000u) + 10u;
  if (timeout_ms < 15u) timeout_ms = 15u;

  if (!this->wait_for_rmt_tx_(timeout_ms, "flush")) {
    ESP_LOGE(TAG, "RMT TX timeout (Wait: %" PRIu32 "ms, LEDs: %d)",
             timeout_ms, this->num_leds_);
    this->status_set_warning();
    return;
  }
  this->harvest_rmt_encoder_diag_();
  this->reset_rmt_encoder_diag_();

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

  // Copy pixel buffer → RMT buffer and fire
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  memcpy(this->rmt_buf_, this->buf_, this->get_buffer_size_());
#else
  // Pre-5.3: encode bytes → RMT symbols manually
  size_t buffer_size = this->get_buffer_size_();
  size_t sz = 0;
  uint8_t *psrc = this->buf_;
  rmt_symbol_word_t *pdest = this->rmt_buf_;
  while (sz < buffer_size) {
    uint8_t b = *psrc;
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
  g_rmt_dma_active_count++;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  error = rmt_transmit(this->channel_, this->encoder_, this->rmt_buf_,
                       this->get_buffer_size_(), &config);
#else
  size_t len =
      this->get_buffer_size_() * 8 +
      ((this->params_.reset.duration0 > 0 || this->params_.reset.duration1 > 0)
           ? 1
           : 0);
  error = rmt_transmit(this->channel_, this->encoder_, this->rmt_buf_,
                       len * sizeof(rmt_symbol_word_t), &config);
#endif

  if (error != ESP_OK) {
    this->rmt_tx_in_flight_ = false;
    if (g_rmt_dma_active_count > 0) {
      g_rmt_dma_active_count--;
    }
    ESP_LOGE(TAG, "RMT TX error");
    this->status_set_warning();
    return;
  }
  // P2: flag was armed before transmit so a short transaction cannot complete
  // before the ISR has valid in-flight state to clear.
  this->perf_diag_total_rmt_tx_launches_++;
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

  const uint32_t dma_guard_us =
      wait_for_dma_counter_idle_(&g_rmt_dma_active_count, 8000u);
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
    *ptr++ = *src++;
    *ptr++ = *src++;
    *ptr++ = *src++;
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
  } else {
    ESP_LOGCONFIG(TAG,
                  "CFXLight (RMT):\n"
                  "  Pin: GPIO%u\n"
                  "  Chipset: %s\n"
                  "  LEDs: %u\n"
                  "  RGBW: %s\n"
                  "  RGB Order: %s\n"
                  "  RMT Symbols: %" PRIu32 "\n"
                  "  RMT TX Mode: %s\n"
                  "  RMT mem_block_symbols: %" PRIu32,
                  this->pin_, chipset_str, this->num_leds_,
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
