/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * CFXLight - Async DMA LED Strip Driver for ESPHome
 *
 * Phase 1: Skeleton — pixel buffer, get_view_internal, basic setup/show.
 * Phase 2 will add async DMA fire-and-forget via rmt_transmit.
 */

#include "cfx_light.h"
#include "../cfx_effect/cfx_control.h"

#ifdef USE_WIFI
#include <lwip/inet.h>
#include <lwip/sockets.h>

#endif

#ifdef USE_ESP32

#include "esphome/components/light/light_state.h"
#include "esphome/core/log.h"
#include <cmath>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include <esp_clk_tree.h>
#endif

namespace esphome {
namespace cfx_light {

static const char *const TAG = "cfx_light";

static const size_t RMT_SYMBOLS_PER_BYTE = 8;

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

  if (index < size) {
    if (symbols_free < RMT_SYMBOLS_PER_BYTE) {
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
    return 0;
  }
  symbols[0] = params->reset;
  *done = true;
  return 1;
}
#endif

// --- Setup ---

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

  // Allocate RMT transmission buffer
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
  this->rmt_buf_ = allocator.allocate(buffer_size);
#else
  RAMAllocator<rmt_symbol_word_t> rmt_allocator(
      RAMAllocator<rmt_symbol_word_t>::ALLOC_INTERNAL);
  this->rmt_buf_ = rmt_allocator.allocate(buffer_size * 8 + 1);
#endif

  // Auto-detect RMT symbol buffer size from chip variant
  // These match ESPHome's proven defaults — larger buffers reduce
  // interrupt-refill frequency, preventing glitches on multi-strip setups
  if (this->rmt_symbols_ == 0) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    this->rmt_symbols_ = 192; // Classic: 512 total (2 strips × 192 = 384, safe)
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    this->rmt_symbols_ = 192; // S2: 256 total
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
    this->rmt_symbols_ =
        192; // S3/P4: 192 total (with DMA, effectively unlimited)
#else
    this->rmt_symbols_ = 96; // C3/C5/C6/H2: 96 total
#endif
  }

  // Configure timing from chipset
  this->configure_timing_();

  // Create RMT TX channel — DMA always enabled
  rmt_tx_channel_config_t channel;
  memset(&channel, 0, sizeof(channel));
  channel.clk_src = RMT_CLK_SRC_DEFAULT;
  channel.resolution_hz = rmt_resolution_hz();
  channel.gpio_num = gpio_num_t(this->pin_);
  channel.mem_block_symbols = this->rmt_symbols_;
  channel.trans_queue_depth = 1;
  channel.flags.invert_out = 0;
  channel.intr_priority = 0;

  // DMA only supported on ESP32-S3 and ESP32-P4
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
  channel.flags.with_dma = true;
  if (rmt_new_tx_channel(&channel, &this->channel_) != ESP_OK) {
    ESP_LOGW(TAG, "DMA channel failed, falling back to non-DMA");
    channel.flags.with_dma = false;
    if (rmt_new_tx_channel(&channel, &this->channel_) != ESP_OK) {
      ESP_LOGE(TAG, "RMT channel creation failed (pin=%u)", this->pin_);
      this->mark_failed();
      return;
    }
  }
#else
  channel.flags.with_dma = false;
  if (rmt_new_tx_channel(&channel, &this->channel_) != ESP_OK) {
    ESP_LOGE(TAG, "RMT channel creation failed (pin=%u)", this->pin_);
    this->mark_failed();
    return;
  }
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

  // --- Phase 2: Set up Event-Driven State Synchronization ---
  // Decoupled from the high-frequency DMA write loop to prevent recursion!
  if (this->master_light_state_ != nullptr &&
      !this->segment_light_states_.empty()) {

    // Wire up listeners.
    this->master_listener_ = new MasterListener(this);
    this->master_light_state_->add_remote_values_listener(
        this->master_listener_);

    for (auto *seg_state : this->segment_light_states_) {
      auto *listener = new SegmentListener(this);
      this->segment_listeners_.push_back(listener);
      seg_state->add_remote_values_listener(listener);
    }
  }

  ESP_LOGI(TAG, "CFXLight ready: %u LEDs on GPIO%u (DMA, %u symbols)",
           this->num_leds_, this->pin_, this->rmt_symbols_);
}

// --- Dynamic State Synchronization ---

void CFXLightOutput::on_master_update() {
  if (this->master_light_state_ == nullptr ||
      this->segment_light_states_.empty()) {
    return;
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
      auto call = seg_state->make_call();
      if (state_changed)
        call.set_state(master_on);
      if (bright_changed)
        call.set_brightness(master_brightness);

      ESP_LOGD("chimera_fx", "Sync TOP-DOWN: Master -> %s (ON: %d)",
               seg_state->get_name().c_str(), master_on);
      call.perform();
    }
  }

  this->is_syncing_ = false; // Unlock
}

void CFXLightOutput::on_segment_update() {
  if (this->master_light_state_ == nullptr ||
      this->segment_light_states_.empty()) {
    return;
  }

  if (this->is_syncing_)
    return;
  this->is_syncing_ = true; // Lock recursion

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
    // We are commanding the master to change state.
    // Update prev_master_state_ so that unexpected/deferred incoming Master
    // listener callbacks don't overreact and force all segments ON!
    this->prev_master_state_ = is_any_segment_on;

    auto call = this->master_light_state_->make_call();
    call.set_state(is_any_segment_on);
    ESP_LOGD("chimera_fx", "Sync BOTTOM-UP: Segments -> Master (ON: %d)",
             is_any_segment_on);
    call.perform();
  }

  this->is_syncing_ = false; // Unlock
}

// --- Component Loop (Intercepts Outro Playback) ---

void CFXLightOutput::loop() {
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
      // Outro finished. Reset local brightness and physically black out strip.
      for (int i = 0; i < this->size(); i++) {
        (*this)[i] = Color::BLACK;
      }
      this->write_state(nullptr);
    }
  }

  // Segment-driven DMA flush: segments write pixels to the parent buffer and
  // call request_segment_flush() instead of schedule_show(). We flush here
  // with write_state(nullptr) which bypasses the Master mute guard below,
  // ensuring segment pixels reach the hardware without the Master paint.
  if (this->segment_needs_flush_) {
    this->segment_needs_flush_ = false;
    this->write_state(nullptr);
  }
}

// --- Update State (Handles Brightness & Solid Colors) ---

void CFXLightOutput::update_state(light::LightState *state) {
  auto val = state->current_values;
  auto max_brightness =
      light::to_uint8_scale(val.get_brightness() * val.get_state());
  this->correction_.set_local_brightness(max_brightness);

  if (this->has_segments()) {
    // Segmented mode: Master LightState is muted.
    // Individual segments handle their own update_state().
    return;
  }

  if (this->is_effect_active()) {
    // Effect handles its own pixel math in apply().
    return;
  }

  // Solid color logic for non-segmented lights
  Color c = light::color_from_light_color_values(val);

  // BUG 13 FIX: Apply force_white to solid colors BEFORE they hit the buffer
  if (this->force_white_sw_ != nullptr && this->force_white_sw_->state &&
      this->has_white_channel()) {
    cfx::apply_force_white(c.r, c.g, c.b, c.w);
  }

  this->all() = c;
  this->schedule_show();
}

// --- Write State (Fire-and-Forget DMA) ---

void CFXLightOutput::write_state(light::LightState *state) {
  // 1. Master Mute: When segments are active, the Master LightState's
  // rendering pipeline is suppressed. The Master paints the entire strip with
  // its ON-state color (white) on every frame — this overwrites segment pixels
  // and creates a white flash. Muting it here forces all pixel rendering to
  // go through the segment-driven flush path (write_state(nullptr)) instead.
  // Non-segmented lights and outro DMA (nullptr calls) pass through unchanged.
  if (state != nullptr && this->has_segments()) {
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
    for (size_t i = 0; i < this->segment_light_states_.size(); i++) {
      auto *seg_state = this->segment_light_states_[i];
      if (!seg_state->remote_values.is_on() &&
          !seg_state->current_values.is_on()) {
        const auto &def = this->segment_defs_[i];
        for (int p = def.start; p < def.stop; p++) {
          if (p < this->size()) {
            (*this)[p] = esphome::Color::BLACK;
          }
        }
      }
    }
  }

  // 1.5. Visualizer UDP Broadcast
#ifdef USE_WIFI
  if (this->visualizer_enabled_ && !this->visualizer_ip_.empty()) {
    if (this->socket_fd_ < 0) {
      this->socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    }
    if (this->socket_fd_ >= 0) {
      struct sockaddr_in dest_addr;
      dest_addr.sin_addr.s_addr = inet_addr(this->visualizer_ip_.c_str());
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = htons(this->visualizer_port_);

      // Prepend Type Header (0x00 = Pixels)
      // To avoid sendmsg issues, we'll use a local buffer for small strips
      // or a vector for larger ones.
      size_t buf_len = this->get_buffer_size_();
      std::vector<uint8_t> pkt;
      pkt.reserve(buf_len + 1);
      pkt.push_back(VISUALIZER_TYPE_PIXELS);
      pkt.insert(pkt.end(), this->buf_, this->buf_ + buf_len);

      sendto(this->socket_fd_, pkt.data(), pkt.size(), 0,
             (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
  }
#endif
  this->status_clear_warning();

  // Protect from refreshing too often
  uint32_t now = micros();
  if (*this->max_refresh_rate_ != 0 &&
      (now - this->last_refresh_) < *this->max_refresh_rate_) {
    this->schedule_show();
    return;
  }
  this->last_refresh_ = now;
  this->mark_shown_();

  // Wait for previous DMA transmission to complete (safety valve)
  // Dynamic timeout: ~30us per LED (WS2812B) + 10ms padding for RTOS overhead
  int timeout_ms = (this->num_leds_ * 30 / 1000) + 10;
  if (timeout_ms < 15)
    timeout_ms = 15; // Minimum 15ms

  esp_err_t error = rmt_tx_wait_all_done(this->channel_, timeout_ms);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "RMT TX timeout (Wait: %dms, LEDs: %d)", timeout_ms,
             this->num_leds_);
    this->status_set_warning();
    return;
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

  // Fire-and-forget: rmt_transmit returns immediately, DMA handles the rest
  rmt_transmit_config_t config;
  memset(&config, 0, sizeof(config));

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
    ESP_LOGE(TAG, "RMT TX error");
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();
}

void CFXLightOutput::send_visualizer_metadata(const std::string &name,
                                              const std::string &palette) {
#ifdef USE_WIFI
  if (this->visualizer_enabled_ && !this->visualizer_ip_.empty()) {
    if (this->socket_fd_ < 0) {
      this->socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    }
    if (this->socket_fd_ >= 0) {
      struct sockaddr_in dest_addr;
      dest_addr.sin_addr.s_addr = inet_addr(this->visualizer_ip_.c_str());
      dest_addr.sin_family = AF_INET;
      dest_addr.sin_port = htons(this->visualizer_port_);

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

      sendto(this->socket_fd_, pkt.data(), pkt.size(), 0,
             (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
  }
#endif
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
  ESP_LOGCONFIG(TAG,
                "CFXLight:\n"
                "  Pin: %u\n"
                "  Chipset: %s\n"
                "  LEDs: %u\n"
                "  RGBW: %s\n"
                "  RGB Order: %s\n"
                "  RMT Symbols: %" PRIu32,
                this->pin_, chipset_str, this->num_leds_,
                this->is_rgbw_ ? "yes" : "no", order_str, this->rmt_symbols_);

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
}

float CFXLightOutput::get_setup_priority() const {
  return setup_priority::HARDWARE;
}

} // namespace cfx_light
} // namespace esphome

#endif // USE_ESP32
