/*
 * ChimeraFX — CFXScheduler implementation
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Licensed under the EUPL-1.2
 */

#include "cfx_scheduler.h"
#include "esphome/core/log.h"
#include <algorithm>  // std::sort

static const char *const TAG = "CFXScheduler";

namespace esphome {
namespace chimera_fx {

// ── Singleton ────────────────────────────────────────────────────────────────

CFXScheduler &CFXScheduler::get() {
  static CFXScheduler inst;
  return inst;
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void CFXScheduler::setup() {
  if (setup_done_) return;
  setup_done_ = true;

#if CFX_DUAL_CORE
  // Binary semaphore: Core 1 takes it after notifying Core 0, Core 0 gives it
  // when its slice is done. Acts as the inter-core frame synchronisation barrier.
  core0_done_ = xSemaphoreCreateBinary();
  if (core0_done_ == nullptr) {
    ESP_LOGE(TAG, "Semaphore alloc failed — falling back to sequential dispatch");
    return;
  }

  // Priority 1: below ESPHome main loop (typically 5), above idle (0).
  // Keeps Core 0's WiFi/BT/LwIP stack from being starved during heavy LED frames.
  BaseType_t ret = xTaskCreatePinnedToCore(
      core0_task_fn,   // task function
      "cfx_core0",     // task name (visible in FreeRTOS task list)
      4096,            // stack: 4 KB — RAM-AUDIT: reduced from 8192 after HWM measurement.
                       // Task only runs: notify-wait → runner loop (InstanceGuard + service())
                       // → semaphore-give. Effects allocate on heap, not stack.
                       // HWM readings across Fire/Energy/Collider confirmed negligible depth.
      this,            // parameter passed to task function
      1,               // priority
      &core0_task_,    // handle out
      0                // pin to Core 0
  );

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Task create failed (err=%d) — falling back to sequential dispatch", (int)ret);
    core0_task_ = nullptr;
  } else {
    ESP_LOGD(TAG, "Core 0 task created — dual-core LED dispatch enabled");
  }

#else
  ESP_LOGD(TAG, "Single-core ESP32 — sequential dispatch active");
#endif
}

// ── Core 0 task ───────────────────────────────────────────────────────────────

#if CFX_DUAL_CORE
void CFXScheduler::core0_task_fn(void *arg) {
  CFXScheduler *self = static_cast<CFXScheduler *>(arg);

  for (;;) {
    // Block until Core 1 signals the start of a new frame slice.
    // portMAX_DELAY: task sleeps with zero CPU cost between frames.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Service Core 0's runner slice.
    // InstanceGuard sets instance_per_core[0] for each runner so all
    // `instance->` calls inside effect functions resolve to the correct runner
    // without any changes to the effect function bodies.
    for (auto *r : self->core0_slice_) {
      if (r != nullptr) {
        InstanceGuard guard(r);
        r->service();
      }
    }

    // RAM-AUDIT: Stack high-water mark instrumentation.
    // Reports every ~200 frames (~3s @ 66 FPS) to measure actual stack usage.
    // Safe to remove once stack size is finalized.
    static uint32_t hwm_report_counter = 0;
    if (++hwm_report_counter >= 200) {
      hwm_report_counter = 0;
      const UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(nullptr);
      ESP_LOGV(TAG, "Core 0 stack HWM: %u words (%u bytes free of 4096)",
               (unsigned)hwm_words, (unsigned)(hwm_words * 4));
    }

    // Signal Core 1 that this frame's slice is complete.
    xSemaphoreGive(self->core0_done_);
  }

  // Never reached — task loops until the device reboots.
  vTaskDelete(nullptr);
}
#endif

// ── Dispatch ─────────────────────────────────────────────────────────────────

void CFXScheduler::service_runners(std::vector<CFXRunner *> &runners) {
  const size_t total = runners.size();
  if (total == 0) return;

#if CFX_DUAL_CORE
  // Deferred batch path: register all runners into the shared pending list.
  // They will be flushed in parallel at the next tick boundary, together with
  // runners from other lights that also call service_runner/service_runners.
  // This enables cross-light parallelism (all N runners split N/2 per core)
  // instead of per-light parallelism (3+3 sequential between lights).
  if (!force_sequential_ && core0_task_ != nullptr && core0_done_ != nullptr) {
    for (auto *r : runners) {
      if (r != nullptr) {
        service_runner(r);
      }
    }
    return;
  }
#endif

  // Sequential fallback: force_sequential, single-core, or task not live.
  if (force_sequential_ && !sequential_diag_logged_) {
    ESP_LOGW(TAG,
             "Sequential diagnostic mode enabled — dual-core dispatch bypassed");
    sequential_diag_logged_ = true;
  }

  for (auto *r : runners) {
    if (r != nullptr) {
      InstanceGuard guard(r);
      r->service();
    }
  }
}

// ── flush_pending ─────────────────────────────────────────────────────────────

#if CFX_DUAL_CORE
void CFXScheduler::flush_pending() {
  const size_t total = pending_runners_.size();
  if (total == 0) return;

  // Always reset core0_slice_ so the caller's log is never stale.
  core0_slice_.clear();

  if (force_sequential_ || total < 2 || core0_task_ == nullptr || core0_done_ == nullptr) {
    // Sequential fallback: single runner, no Core 0, or diag mode.
    for (auto *r : pending_runners_) {
      if (r != nullptr) {
        InstanceGuard guard(r);
        r->service();
      }
    }
    return;
  }

  // ── Cost-weighted bin-packing split (Longest Processing Time heuristic) ──
  // Sort runners heaviest-first so largest jobs are placed first,
  // minimising load imbalance between cores.
  std::vector<CFXRunner *> sorted(pending_runners_.begin(), pending_runners_.end());
  std::sort(sorted.begin(), sorted.end(), [](CFXRunner *a, CFXRunner *b) {
    return a->frame_time > b->frame_time;
  });

  uint32_t cost0 = 0, cost1 = 0;
  core0_slice_.clear();
  std::vector<CFXRunner *> core1_slice;

  for (auto *r : sorted) {
    if (cost1 <= cost0) {
      core1_slice.push_back(r);
      cost1 += r->frame_time;
    } else {
      core0_slice_.push_back(r);
      cost0 += r->frame_time;
    }
  }

  constexpr uint32_t TIMEOUT_MS = (FRAMETIME + 12 > 25) ? (FRAMETIME + 12) : 25;

  // Wake Core 0 — it will start servicing core0_slice_ immediately.
  xTaskNotifyGive(core0_task_);

  // Core 1 services its slice inline in parallel with Core 0.
  for (auto *r : core1_slice) {
    if (r != nullptr) {
      InstanceGuard guard(r);
      r->service();
    }
  }

  // Wait for Core 0 to finish before returning.
  // On timeout the strip retains its last valid pixel values — graceful degradation.
  if (xSemaphoreTake(core0_done_, pdMS_TO_TICKS(TIMEOUT_MS)) != pdTRUE) {
    ESP_LOGW(TAG, "Core 0 batch flush timeout (%ums) — %u runners may have stale pixels",
             TIMEOUT_MS, (unsigned)core0_slice_.size());
  }

}
#endif

// ── service_runner ────────────────────────────────────────────────────────────

void CFXScheduler::service_runner(CFXRunner *r) {
  if (r == nullptr) return;

#if CFX_DUAL_CORE
  if (!force_sequential_ && core0_task_ != nullptr && core0_done_ != nullptr) {

    // ── Tick boundary detection ─────────────────────────────────────────────
    // pending_runners_ accumulates one entry per runner per tick.
    // When a runner already in pending_runners_ is presented again, the
    // ESPHome apply() loop has wrapped around — flush the batch in parallel
    // and start collecting fresh for the next tick.
    bool is_new_tick = false;
    for (auto *p : pending_runners_) {
      if (p == r) { is_new_tick = true; break; }
    }

    if (is_new_tick) {
      flush_pending();
      pending_runners_.clear();
    }

    // Register this runner for the upcoming flush — do NOT call service() now.
    // service() will be called by flush_pending() at the next tick boundary.
    // The pixel buffer retains last tick's values until the flush runs,
    // introducing ≤1 frame of visual latency (~28ms at 35 FPS — imperceptible).
    pending_runners_.push_back(r);
    return;
  }
#endif

  // Sequential path: single-core, force_sequential, or Core 0 not live.
  InstanceGuard guard(r);
  r->service();
}

void CFXScheduler::drain_core0() {
#if CFX_DUAL_CORE
  if (core0_task_ != nullptr && core0_done_ != nullptr) {
    if (xSemaphoreTake(core0_done_, pdMS_TO_TICKS(FRAMETIME + 12)) == pdTRUE) {
      xSemaphoreGive(core0_done_);
    }
  }
#endif
}

} // namespace chimera_fx
} // namespace esphome
