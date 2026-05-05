#include "cfx_transmit_barrier.h"
#include "cfx_light.h"

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace cfx_light {

static const char *const TAG_BARRIER = "cfx_barrier";

// ── Singleton ────────────────────────────────────────────────────────────────

CFXTransmitBarrier &CFXTransmitBarrier::get() {
  static CFXTransmitBarrier instance;
  return instance;
}

// ── Registration ─────────────────────────────────────────────────────────────

void CFXTransmitBarrier::register_output(CFXLightOutput *output) {
  if (count_ >= MAX_OUTPUTS) {
    ESP_LOGW(TAG_BARRIER,
             "MAX_OUTPUTS (%zu) reached — output not registered, "
             "it will flush independently",
             MAX_OUTPUTS);
    return;
  }
  outputs_[count_++] = output;
  ESP_LOGI(TAG_BARRIER, "Output registered (%zu total)", count_);
}

// ── request_transmit ─────────────────────────────────────────────────────────

bool CFXTransmitBarrier::request_transmit(CFXLightOutput *output) {
  // Barrier is transparent for single-output setups.
  if (count_ < 2)
    return true;

  // Find the slot for this output.
  size_t slot = MAX_OUTPUTS;
  for (size_t i = 0; i < count_; i++) {
    if (outputs_[i] == output) {
      slot = i;
      break;
    }
  }
  // Unregistered output (e.g. registered after MAX_OUTPUTS): fire immediately.
  if (slot == MAX_OUTPUTS)
    return true;

  // Mark this output as pending for the current tick.
  if (!pending_[slot]) {
    pending_[slot] = true;
    pending_count_++;
    if (pending_count_ == 1) {
      // First arrival — arm the timeout window.
      first_req_ms_ = esphome::millis();
    }
  }

  // All registered outputs are ready — fire all now.
  if (pending_count_ >= count_) {
    fire_all_pending_();
    // Return false: the flush for this output already happened inside
    // fire_all_pending_(). Caller must NOT flush again.
    return false;
  }

  // Still waiting for other outputs — defer.
  return false;
}

// ── service ──────────────────────────────────────────────────────────────────

void CFXTransmitBarrier::service(CFXLightOutput * /* caller */) {
  if (pending_count_ == 0 || count_ < 2)
    return;

  // Timeout: fire whoever arrived — don't starve active outputs waiting for
  // an inactive one that suppressed write_state() this tick.
  if ((esphome::millis() - first_req_ms_) >= BARRIER_TIMEOUT_MS) {
    ESP_LOGV(TAG_BARRIER,
             "Barrier timeout — firing %zu/%zu pending output(s)",
             pending_count_, count_);
    fire_all_pending_();
  }
}

// ── fire_all_pending_ ────────────────────────────────────────────────────────

void CFXTransmitBarrier::fire_all_pending_() {
  // Fire RMT outputs first — they are more timing-sensitive (WS281x protocol
  // has strict inter-frame silence requirements). SPI outputs follow immediately
  // after; both DMA engines then run in parallel.
  for (size_t pass = 0; pass < 2; pass++) {
    for (size_t i = 0; i < count_; i++) {
      if (!pending_[i])
        continue;
      // Pass 0 = RMT, pass 1 = SPI (or any non-RMT transport).
      const bool is_rmt = outputs_[i]->is_rmt_transport();
      if ((pass == 0) != is_rmt)
        continue;
      outputs_[i]->commit_transmit_();
      pending_[i] = false;
    }
  }
  pending_count_ = 0;
  first_req_ms_ = 0;
}

}  // namespace cfx_light
}  // namespace esphome
