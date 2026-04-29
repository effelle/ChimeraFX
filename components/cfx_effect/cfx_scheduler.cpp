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
      8192,            // stack: 8 KB — effects allocate via heap, not stack
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
      ESP_LOGD(TAG, "Core 0 stack HWM: %u words (%u bytes free of 8192)",
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

  if (force_sequential_) {
    if (!sequential_diag_logged_) {
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

    // RAM-AUDIT: Sequential path (Main Loop) stack instrumentation.
    static uint32_t seq_hwm_counter = 0;
    if (++seq_hwm_counter >= 200) {
      seq_hwm_counter = 0;
      const UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(nullptr);
      ESP_LOGD(TAG, "Sequential Loop stack HWM: %u words (%u bytes free)",
               (unsigned)hwm_words, (unsigned)(hwm_words * 4));
    }
    return;
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

    // Build a local sorted copy of runner pointers (pointer copy only, cheap).
    std::vector<CFXRunner *> sorted(runners.begin(), runners.end());
    std::sort(sorted.begin(), sorted.end(), [](CFXRunner *a, CFXRunner *b) {
      return a->frame_time > b->frame_time;
    });

    uint32_t cost_core0 = 0, cost_core1 = 0;
    core0_slice_.clear();
    std::vector<CFXRunner *> core1_slice;

    for (auto *r : sorted) {
      if (cost_core1 <= cost_core0) {
        core1_slice.push_back(r);
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
    for (auto *r : core1_slice) {
      if (r != nullptr) {
        InstanceGuard guard(r);
        r->service();
      }
    }

    // Wait for Core 0 to finish before returning to apply().
    // On timeout the strip retains its last valid pixel values —
    // graceful degradation, no crash.
    if (xSemaphoreTake(core0_done_, pdMS_TO_TICKS(CFX_CORE0_TIMEOUT_MS)) != pdTRUE) {
      ESP_LOGW(TAG, "Core 0 runner timeout (%u ms) — frame skipped for %u runners",
               CFX_CORE0_TIMEOUT_MS, (unsigned)core0_slice_.size());
    }

    // RAM-AUDIT: Parallel path (Main Loop / Core 1) stack instrumentation.
    static uint32_t main_hwm_counter = 0;
    if (++main_hwm_counter >= 200) {
      main_hwm_counter = 0;
      const UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(nullptr);
      ESP_LOGD(TAG, "Main Loop stack HWM: %u words (%u bytes free)",
               (unsigned)hwm_words, (unsigned)(hwm_words * 4));
    }
    return;
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

  // RAM-AUDIT: Shared HWM counter — hits whichever path is actually hot.
  static uint32_t fallthrough_hwm_counter = 0;
  if (++fallthrough_hwm_counter >= 200) {
    fallthrough_hwm_counter = 0;
    const UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGD(TAG, "(fallthrough) Main Loop stack HWM: %u words (%u bytes free)",
             (unsigned)hwm_words, (unsigned)(hwm_words * 4));
  }
}

void CFXScheduler::service_runner(CFXRunner *r) {
  if (r == nullptr) return;
  InstanceGuard guard(r);
  r->service();

  // RAM-AUDIT: HWM instrumentation — this is the hot path for non-segmented lights.
  // 8 effects × ~34 FPS = ~272 calls/sec → counter trips every ~0.7s.
  static uint32_t single_hwm_counter = 0;
  if (++single_hwm_counter >= 200) {
    single_hwm_counter = 0;
    const UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGD(TAG, "Main Loop stack HWM: %u words (%u bytes free)",
             (unsigned)hwm_words, (unsigned)(hwm_words * 4));
  }
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
