/*
 * ChimeraFX — CFXScheduler implementation
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Licensed under the EUPL-1.2
 */

#include "cfx_scheduler.h"
#include "esphome/core/log.h"

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
    ESP_LOGI(TAG, "Core 0 task created — dual-core LED dispatch enabled");
  }

#else
  ESP_LOGI(TAG, "Single-core ESP32 — sequential dispatch active");
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

void CFXScheduler::service_runners(std::vector<CFXRunner *> &runners) {
  const size_t total = runners.size();
  if (total == 0) return;

#if CFX_DUAL_CORE
  // Parallel path: requires ≥2 runners and a live Core 0 task.
  if (total >= 2 && core0_task_ != nullptr && core0_done_ != nullptr) {

    // Split at midpoint. Odd runner counts give Core 1 the extra runner —
    // it is the calling core, so there is no scheduling latency for the extra work.
    const size_t split = total / 2;

    // Populate Core 0's slice BEFORE the notification so the task sees
    // a consistent view the moment it wakes up.
    core0_slice_.assign(runners.begin() + split, runners.end());

    // Wake Core 0 — it will start processing core0_slice_ immediately.
    xTaskNotifyGive(core0_task_);

    // Core 1 services its half inline while Core 0 runs in parallel.
    // InstanceGuard sets instance_per_core[1] for each runner.
    for (size_t i = 0; i < split; i++) {
      if (runners[i] != nullptr) {
        InstanceGuard guard(runners[i]);
        runners[i]->service();
      }
    }

    // Wait for Core 0 to finish before returning to apply().
    // 20 ms = one frame budget at 50 fps. On timeout the strip retains
    // its last valid pixel values — graceful degradation, no crash.
    if (xSemaphoreTake(core0_done_, pdMS_TO_TICKS(20)) != pdTRUE) {
      ESP_LOGW(TAG, "Core 0 runner timeout — frame skipped for %u runners",
               (unsigned)(total - split));
    }
    return;
  }
  // Fall through: task not ready yet, or only 1 runner — sequential is fine.
#endif

  // Sequential path (single-core chip, 1 runner, or dual-core task not live).
  for (auto *r : runners) {
    if (r != nullptr) {
      InstanceGuard guard(r);
      r->service();
    }
  }
}

void CFXScheduler::service_runner(CFXRunner *r) {
  if (r == nullptr) return;
  InstanceGuard guard(r);
  r->service();
}

} // namespace chimera_fx
} // namespace esphome
