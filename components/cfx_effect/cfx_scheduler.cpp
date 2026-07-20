/*
 * ChimeraFX — CFXScheduler implementation
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Licensed under the EUPL-1.2
 */

#include "cfx_scheduler.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>  // std::sort
#include <cinttypes>

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

    // Signal Core 1 that this frame's slice is complete.
    xSemaphoreGive(self->core0_done_);
  }

  // Never reached — task loops until the device reboots.
  vTaskDelete(nullptr);
}
#endif

// ── Dispatch ─────────────────────────────────────────────────────────────────

bool CFXScheduler::service_runners(std::vector<CFXRunner *> &runners,
                                    bool force_sequential) {
  const size_t total = runners.size();
  if (total == 0) return true;
  const uint32_t dispatch_start_us = total >= 4 ? micros() : 0;

  // Global override (diagnostic) takes precedence. Per-call flag (e.g. SPI
  // coordinator batch) runs the same sequential path without the warning —
  // it is expected, deliberate behavior for that transport.
  const bool run_sequential = force_sequential_ || force_sequential;

  if (run_sequential) {
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

    if (total >= 4) {
      const uint32_t now_ms = millis();
      if (this->last_batch_diag_ms_ == 0 ||
          (now_ms - this->last_batch_diag_ms_) >= 2000) {
        this->last_batch_diag_ms_ = now_ms;
        ESP_LOGV(TAG,
                 "CFX sched_batch total=%u mode=sequential force=%u global=%u "
                 "core1=%u core0=0 cost1=0 cost0=0 dispatch_us=%" PRIu32
                 " ok=1",
                 static_cast<unsigned>(total),
                 static_cast<unsigned>(force_sequential),
                 static_cast<unsigned>(force_sequential_),
                 static_cast<unsigned>(total), micros() - dispatch_start_us);
      }
    }

    return true;
  }

#if CFX_DUAL_CORE
  // Parallel path: requires ≥2 runners and a live Core 0 task.
  if (total >= 2 && core0_task_ != nullptr && core0_done_ != nullptr) {

    // ── Cost-weighted greedy bin-packing split ────────────────────────────
    // Instead of a naive midpoint split, distribute runners by measured frame
    // cost (frame_time, in ms, updated every frame by CFXRunner::service()).
    // Runners are sorted heaviest-first so the largest jobs are placed first,
    // minimising imbalance. Each runner goes to whichever core has less
    // accumulated cost so far — classic "Longest Processing Time" heuristic.
    //
    // On the very first frame frame_time is 0 for all runners, so the sort
    // is a no-op and runners alternate cores in list order — equivalent to
    // the old midpoint split, with no warm-up penalty.
    //
    // Core 1 is the calling core (ESPHome main loop). It receives the extra
    // runner when counts are odd because there is no scheduling latency for
    // inline work.

    // Reuse scheduler-owned scratch vectors to avoid heap churn in the
    // per-frame hot path. Pointer copies only; runner ownership stays external.
    if (sorted_slice_.capacity() < total)
      sorted_slice_.reserve(total);
    if (core1_slice_.capacity() < total)
      core1_slice_.reserve(total);
    if (core0_slice_.capacity() < total)
      core0_slice_.reserve(total);

    sorted_slice_.clear();
    sorted_slice_.insert(sorted_slice_.end(), runners.begin(), runners.end());
    std::sort(sorted_slice_.begin(), sorted_slice_.end(), [](CFXRunner *a, CFXRunner *b) {
      return a->frame_time > b->frame_time;
    });

    uint32_t cost_core0 = 0, cost_core1 = 0;
    core0_slice_.clear();
    core1_slice_.clear();

    for (auto *r : sorted_slice_) {
      if (cost_core1 <= cost_core0) {
        core1_slice_.push_back(r);
        cost_core1 += r->frame_time;
      } else {
        core0_slice_.push_back(r);
        cost_core0 += r->frame_time;
      }
    }

    // ── Dynamic semaphore timeout ─────────────────────────────────────────
    // The old hardcoded 20 ms timed out whenever Core 0's runners needed more
    // than one frame budget (e.g. heavy effects at high strip counts).
    // Use FRAMETIME + a 12 ms safety margin instead, which covers the worst
    // observed render times in testing (~28 ms) with headroom to spare.
    // Minimum of 25 ms so we never regress below the old budget on fast paths.
    constexpr uint32_t CFX_CORE0_TIMEOUT_MS = (FRAMETIME + 12 > 25) ? (FRAMETIME + 12) : 25;

    // Populate Core 0's slice BEFORE the notification so the task sees
    // a consistent view the moment it wakes up.
    // (core0_slice_ is already set above.)

    // Wake Core 0 — it will start processing core0_slice_ immediately.
    xTaskNotifyGive(core0_task_);

    // Core 1 services its slice inline while Core 0 runs in parallel.
    // InstanceGuard sets instance_per_core[1] for each runner.
    for (auto *r : core1_slice_) {
      if (r != nullptr) {
        InstanceGuard guard(r);
        r->service();
      }
    }

    // Wait for Core 0 to finish before returning to apply().
    // On timeout the strip retains its last valid pixel values —
    // graceful degradation, no crash.
    const bool core0_ok =
        xSemaphoreTake(core0_done_, pdMS_TO_TICKS(CFX_CORE0_TIMEOUT_MS)) == pdTRUE;
    if (!core0_ok) {
      ESP_LOGW(TAG,
               "Core 0 runner timeout (%" PRIu32
               " ms) — frame skipped for %u runners",
               CFX_CORE0_TIMEOUT_MS, (unsigned)core0_slice_.size());
    }

    if (total >= 4) {
      const uint32_t now_ms = millis();
      if (this->last_batch_diag_ms_ == 0 ||
          (now_ms - this->last_batch_diag_ms_) >= 2000) {
        this->last_batch_diag_ms_ = now_ms;
        ESP_LOGV(TAG,
                 "CFX sched_batch total=%u mode=dual force=0 global=%u "
                 "core1=%u core0=%u cost1=%u cost0=%u dispatch_us=%" PRIu32
                 " ok=%u timeout_ms=%" PRIu32,
                 static_cast<unsigned>(total),
                 static_cast<unsigned>(force_sequential_),
                 static_cast<unsigned>(core1_slice_.size()),
                 static_cast<unsigned>(core0_slice_.size()),
                 static_cast<unsigned>(cost_core1),
                 static_cast<unsigned>(cost_core0),
                 micros() - dispatch_start_us, static_cast<unsigned>(core0_ok),
                 CFX_CORE0_TIMEOUT_MS);
      }
    }

    return core0_ok;
  }
  // Fall through: task not ready yet, or only 1 runner — sequential is fine.
#endif

  // Sequential fallthrough: single-core, only 1 runner, or dual-core task not live.
  for (auto *r : runners) {
    if (r != nullptr) {
      InstanceGuard guard(r);
      r->service();
    }
  }

  if (total >= 4) {
    const uint32_t now_ms = millis();
    if (this->last_batch_diag_ms_ == 0 ||
        (now_ms - this->last_batch_diag_ms_) >= 2000) {
      this->last_batch_diag_ms_ = now_ms;
      ESP_LOGV(TAG,
               "CFX sched_batch total=%u mode=fallthrough force=%u global=%u "
               "core1=%u core0=0 cost1=0 cost0=0 dispatch_us=%" PRIu32
               " ok=1",
               static_cast<unsigned>(total),
               static_cast<unsigned>(force_sequential),
               static_cast<unsigned>(force_sequential_),
               static_cast<unsigned>(total), micros() - dispatch_start_us);
    }
  }

  return true;
}

void CFXScheduler::service_runner(CFXRunner *r) {
  if (r == nullptr) return;
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
