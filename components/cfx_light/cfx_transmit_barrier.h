#pragma once
// CFXTransmitBarrier — P3: Coordinated DMA launch across all CFXLightOutput instances.
//
// Problem: ESPHome calls write_state() on each output independently. Without
// coordination, SPI and RMT DMA bursts start in the order write_state() fires —
// separated by the CPU time of each flush call. On ESP32-S3 this gap is ~5µs
// (acceptable), but with many outputs or on Classic ESP32 with the 300µs RMT
// stagger gate, accumulated skew reduces the parallel overlap window.
//
// Solution: each output calls request_transmit() instead of flushing directly.
// The barrier fires commit_transmit_() on all outputs simultaneously once all
// registered outputs have requested in the same tick window. A safety timeout
// (BARRIER_TIMEOUT_MS) fires whoever is pending if a late output misses the
// window (e.g. it was suppressed as clean-idle this tick).
//
// Single-output setups: request_transmit() returns true immediately — zero
// overhead, barrier is fully transparent.
//
// Thread safety: all paths run on the ESPHome main-loop task.

#include <stddef.h>
#include <stdint.h>

namespace esphome {
namespace cfx_light {

class CFXLightOutput;  // forward declaration

class CFXTransmitBarrier {
 public:
  static CFXTransmitBarrier &get();

  // Called once per output at the end of setup(). Must be called for every
  // output that will participate in coordinated launches.
  void register_output(CFXLightOutput *output);

  // Called from write_state() in place of a direct flush.
  //
  // Returns true  → barrier not active (< 2 outputs); caller fires directly.
  // Returns false → output deferred. The barrier has either already fired all
  //                 pending outputs (when this was the last one to arrive), or
  //                 it is still collecting and will fire on timeout via service().
  //                 In both cases the caller must NOT flush.
  bool request_transmit(CFXLightOutput *output);

  // Called from every registered output's loop() each tick. Fires any outputs
  // that are pending and whose barrier window has expired.
  void service(CFXLightOutput *caller);

 private:
  CFXTransmitBarrier() = default;
  void fire_all_pending_();

  // Maximum number of simultaneously registered outputs. 8 covers all
  // practical ESP32 configurations (limited by RMT channels + SPI buses).
  static constexpr size_t MAX_OUTPUTS = 8;

  // Maximum time to wait for all outputs to arrive before firing whoever is
  // pending. 2 ms sits well inside one 20 ms frame budget at 50 FPS and is
  // longer than the Classic ESP32 300 µs RMT stagger gap.
  static constexpr uint32_t BARRIER_TIMEOUT_MS = 2;

  CFXLightOutput *outputs_[MAX_OUTPUTS]{};
  bool pending_[MAX_OUTPUTS]{};
  size_t count_{0};          // total registered outputs
  size_t pending_count_{0};  // outputs that have requested this tick
  uint32_t first_req_ms_{0}; // timestamp of the first request this tick
};

}  // namespace cfx_light
}  // namespace esphome
