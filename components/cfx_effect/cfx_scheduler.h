/*
 * ChimeraFX — CFXScheduler
 * Copyright (c) 2026 Federico Leoni (effelle)
 * Licensed under the EUPL-1.2
 *
 * Dispatches CFXRunner::service() calls across FreeRTOS cores.
 *
 * Dual-core ESP32 / ESP32-S3 — Deferred Batch Dispatch:
 *   service_runner() registers runners into a pending list each tick instead
 *   of executing immediately. At the detected tick boundary (when a runner
 *   seen in the previous tick reappears), flush_pending() dispatches the full
 *   list in parallel: Core 0 handles one cost-weighted half, Core 1 the other.
 *   A binary semaphore synchronises completion before DMA write.
 *   Result: all N runners complete in max(Core0_cost, Core1_cost) ≈ N/2 × T
 *   instead of N × T. At 8 runners this is a theoretical ~2× FPS improvement.
 *
 * Single-core ESP32 variants (C3, S2, H2):
 *   CONFIG_FREERTOS_UNICORE is defined by sdkconfig.
 *   CFX_DUAL_CORE resolves to 0 and the scheduler falls through to a plain
 *   sequential loop — zero overhead, identical behaviour to pre-scheduler code.
 */

#pragma once

#include "CFXRunner.h"
#include <vector>

// ── Dual-core detection ───────────────────────────────────────────────────────
// CONFIG_FREERTOS_UNICORE is set by sdkconfig on single-core ESP32 variants
// (C3, S2, H2) and any ESP32 built with a single-core configuration.
#if !defined(CONFIG_FREERTOS_UNICORE)
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #define CFX_DUAL_CORE 1
#else
  #define CFX_DUAL_CORE 0
#endif

namespace esphome {
namespace chimera_fx {

class CFXScheduler {
public:
  // Singleton accessor — safe to call from any context.
  static CFXScheduler &get();

  // Idempotent setup: creates the Core 0 FreeRTOS task on first call.
  // Called from CFXAddressableLightEffect::start() so the task is only
  // created when at least one CFX effect is actually running.
  void setup();

  bool is_setup() const { return setup_done_; }

  // ── Primary dispatch entry points ────────────────────────────────────────

  // Service a vector of runners.
  // Dual-core: splits list, Core 0 handles second half in parallel.
  // Single-core / 1 runner: sequential loop, no FreeRTOS overhead.
  void service_runners(std::vector<CFXRunner *> &runners);

  // Convenience wrapper for the single-runner (no-segment) code path.
  void service_runner(CFXRunner *r);

  // Diagnostic override used when mixed-transport runs need the safest
  // possible scheduler behavior.
  void set_force_sequential(bool enabled) {
    if (force_sequential_ == enabled)
      return;
    force_sequential_ = enabled;
    if (enabled)
      sequential_diag_logged_ = false;
  }
  bool is_force_sequential() const { return force_sequential_; }

  // Wait for Core 0 to finish its current frame slice before returning.
  // Must be called from Core 1 before mutating segment_runners or deleting
  // any CFXRunner that may still be referenced in core0_slice_.
  // No-op on single-core builds or when Core 0 task is not live.
  void drain_core0();

private:
  CFXScheduler() = default;

  bool setup_done_{false};
  bool force_sequential_{false};
  bool sequential_diag_logged_{false};

#if CFX_DUAL_CORE
  static void core0_task_fn(void *arg);

  // Flush the accumulated pending_runners_ list in parallel across both cores.
  // Core 0 receives a cost-weighted half via core0_slice_; Core 1 runs the
  // remainder inline. Blocks until Core 0 signals done (binary semaphore).
  void flush_pending();

  TaskHandle_t      core0_task_{nullptr};
  SemaphoreHandle_t core0_done_{nullptr};

  // Slice of runners dispatched to Core 0 each frame.
  // Written by Core 1 (before xTaskNotifyGive), read by Core 0 task.
  // Protected by the notification/semaphore handshake — no mutex needed.
  std::vector<CFXRunner *> core0_slice_;

  // Deferred batch: runners registered each tick, flushed at the next tick
  // boundary (detected when a runner reappears in pending_runners_).
  std::vector<CFXRunner *> pending_runners_;
#endif
};

} // namespace chimera_fx
} // namespace esphome
