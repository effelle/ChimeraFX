# Performance Reference

This page documents the real-world rendering performance of ChimeraFX on ESP32 hardware, measured under controlled conditions with the Energy effect (the most compute-intensive effect in the library).

---

## Hardware Ceiling — Classic ESP32

> **Platform tested:** ESP32 (dual-core Xtensa LX6, 240 MHz), ESPHome with WiFi active.

### Single runner (1 active effect)

| Metric | Value |
|--------|-------|
| FPS | ~60 |
| Frame time | ~16 ms |
| Jitter | < 5% |

A single active effect runs at the target 60 FPS budget with no contention.

---

### Multi-runner scaling (Energy effect, 8 simultaneous strips)

The table below shows measured performance as concurrent active effects increase. All strips run the same heavy math effect (Energy) to represent the worst case.

| Active strips | FPS | Frame time | Jitter |
|--------------|-----|------------|--------|
| 1 | ~60 | ~16 ms | < 5% |
| 2 | ~58 | ~17 ms | < 5% |
| 2 (segmented, 6 segments) | ~35 | ~28 ms | ~95% |
| 8 (2 lights + 6 segments) | ~33 | ~30 ms | ~95% |

---

## Why Jitter Is High Under Load

Jitter in ChimeraFX is defined as frame-time variance normalised to the target budget:

```
Jitter % = (actual_frame_time - target_frame_time) / target_frame_time × 100
```

With 8 concurrent heavy effects, every frame exceeds the 16 ms target by ~14 ms — so jitter stabilises near 90-97% and **stays there permanently**. This is not instability — it is the system running at 100% CPU utilisation. The visual result is smooth animation at ~33 FPS, which is perfectly watchable.

High jitter at high runner counts is **expected and normal**. It only becomes a problem if it fluctuates significantly frame-to-frame.

---

## Why 33 FPS Is the Floor, Not a Bug

ESPHome processes each light component's `apply()` call sequentially in its main loop. ChimeraFX uses Core 0 to parallelise within a single `service_runners()` call (bin-packing split), but the loop itself cannot be restructured without modifying ESPHome internals.

The practical implication:

```
Total frame cost ≈ Σ (runner_service_time) × parallelism_factor
```

With the dual-core scheduler active, parallelism_factor ≈ 0.5 per light (2 segment groups run in parallel), but each light's call still blocks until both cores complete before the next light's `apply()` can start.

The **hard ceiling for a classic ESP32** with 8+ simultaneous heavy effects is approximately:

- **~30-35 FPS** sustained
- **~60 FPS** with ≤2 light effects simultaneously
- **Memory floor:** ~87 KB heap free under maximum load

---

## ESP32-S3 Outlook

The ESP32-S3 runs Xtensa LX7 cores at 240 MHz (same clock, different micro-architecture). Expected improvement: **+10-20% throughput** from improved pipeline and wider SIMD. This is a rough estimate pending direct measurement.

Key hardware differences relevant to ChimeraFX:

| Feature | ESP32 | ESP32-S3 |
|---------|-------|----------|
| Core | Xtensa LX6 | Xtensa LX7 |
| Clock | 240 MHz | 240 MHz |
| PSRAM support | Optional | Yes (Octal PSRAM) |
| Cache | 32 KB | 16/32 KB |
| Estimated FPS gain | — | ~10-20% |

If PSRAM is available on the S3, effect buffers could be offloaded there — potentially enabling 12-16 simultaneous effects without heap pressure.

---

## Practical Guidelines

| Use case | Recommendation |
|----------|----------------|
| 1-2 light effects (standard) | Any ESP32 — full 60 FPS guaranteed |
| 3-6 simultaneous effects | ESP32 — 40-55 FPS, low jitter |
| 7-8 simultaneous heavy effects | ESP32 — ~33 FPS, high jitter (normal) |
| 9-16 simultaneous effects | ESP32-S3 recommended; consider lighter effects |
| 16+ simultaneous effects | Not supported on classic ESP32 |

### Effect cost ranking (light to heavy)

| Cost | Effects |
|------|---------|
| 🟢 Low | Monochromatic, Blink, Strobe |
| 🟡 Medium | Meteor, Color Wipe, Scan |
| 🔴 High | Fire, Interference, Collider |
| 🔴 Heavy | Energy |

Mixing 🟢 effects with 🔴 effects at 8 strips will perform significantly better than 8× 🔴🔴.

---

## Dual-Core Scheduler

ChimeraFX automatically detects dual-core ESP32 variants and enables the parallel scheduler:

- **Single-core variants** (ESP32-C3, S2, H2): Sequential dispatch, no FreeRTOS overhead.
- **Dual-core variants** (ESP32, ESP32-S3): Core 0 handles one half of each light's segment runners in parallel with Core 1, using a Longest-Processing-Time bin-packing heuristic to minimise imbalance.

The scheduler is transparent — no YAML configuration needed.


!!! note "Core 0 task memory"
    The dual-core scheduler creates a dedicated FreeRTOS task on Core 0 with a **4 KB stack**. This is allocated once at first effect start and never freed. Stack size was validated via high-water mark measurement across all heavy effects.

---

## Memory Math: The Price per LED

Memory (Heap) is the most precious resource in a ChimeraFX setup. Understanding the "byte price" of your configuration helps avoid system brownouts and reboots.

### 1. The Components of Heap Consumption

Your total memory cost is the sum of three distinct layers:

1.  **System Floor (~55 KB)**: Mandatory overhead for ESPHome's Wi-Fi and API stacks.
2.  **Segment Overhead (~3.6 KB/segment)**: The "Stub + Singleton" architecture cost for each virtual segment entity.
3.  **Pixel Buffer Cost**: The memory consumed by the actual LEDs, which depends heavily on your **Transport** choice.

### 2. The "Price per LED" Table

| Transport / Hardware | Byte Price per LED | 1000 LED Impact |
| :--- | :--- | :--- |
| **SPI** (APA102, SK9822, WS2801) | **~9 Bytes** | ~9 KB |
| **RMT** (WS2812X, All ESP32 variants) | **~9 Bytes** | ~9 KB |

!!! tip "ChimeraFX Hybrid DMA"
    Unlike standard implementations that can consume massive amounts of RAM for RMT symbol expansion, ChimeraFX uses a custom Hybrid DMA to ESP-IDF RMT system. This architecture works efficiently on both ESP-IDF and Arduino platforms, keeping RAM usage minimal. A single classic ESP32 can safely drive up to 2400 LEDs (e.g., 4 strips of 600 LEDs) well within hardware limits.

### 3. How to calculate your Free Heap

You can estimate your available RAM after ChimeraFX starts using this formula:

**Free Heap ≈ 160KB – [ (N_Segs × 3.6KB) + (N_LEDs × BytePrice) + 55KB ]**

#### Example Scenarios:

*   **Small setup (Classic ESP32)**: 60 LEDs (RMT), 1 Light.
    *   Cost: 55KB (System) + (60 × 9B) ≈ 56 KB.
    *   Result: **~104 KB Free Heap** (Ultra Stable).
*   **Medium setup (Classic ESP32)**: 300 LEDs (RMT), 4 Segments.
    *   Cost: 55KB + (4 × 3.6KB) + (300 × 9B) ≈ 72 KB.
    *   Result: **~88 KB Free Heap** (Very Stable).
*   **Massive setup (Classic ESP32)**: 2400 LEDs (RMT), 4 Lights.
    *   Cost: 55KB + (2400 × 9B) ≈ 77 KB.
    *   Result: **~83 KB Free Heap** (Stable, despite 2400 LEDs).

### 4. RGB vs RGBW
Adding a white channel (RGBW/WRGB) adds exactly **1 additional byte per LED** to the pixel buffer cost. It does not affect the RMT/SPI transport cost significantly.
