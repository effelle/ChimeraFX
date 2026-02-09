# Performance & Troubleshooting

## Common Issues

### "My strip freezes or blinks random colors"
This is almost always a power or memory/timing issue.
*   **Check Ground**: Ensure your LED strip ground is connected to the ESP32 ground.
*   **RMT Buffer**: See "RMT Symbols" below.
*   **PSRAM**: If you are using an ESP32-S3 with PSRAM, verify `use_psram: false` is set in your light config. RMT does not play well with PSRAM latency.

### "The effect is too fast"
ESPHome runs at 60FPS, while WLED targets 42FPS. I have manually tuned most effects, but if one feels "rushed," try lowering the Speed slider.

### "Colors look wrong (Red is Green)"
Check your `rgb_order` in the YAML.
*   WS2812B usually uses `GRB`.
*   WS2811 usually uses `RGB`.

---

## Performance Tuning (ESP-IDF)

### RMT Symbols

The ESP32 uses RMT (Remote Control) channels to drive LEDs. Each "symbol" represents a bit of data. Limited memory means you can't buffer infinite URLs.

| Chip | Max rmt_symbols |
|------|-----------------|
| Classic ESP32 | **512** (Total shared) |
| ESP32-S3 | **192** (Per channel) |

If you have flickering, try adjusting `rmt_symbols`.
*   **Usage**: `light: ... rmt_symbols: 512`

### WiFi Latency
This component runs on **Core 1** of the ESP32, while WiFi runs on **Core 0**.
*   **Good News**: WiFi activity should NOT cause visible stuttering in effects.
*   **Bad News**: Heavy OTA updates or web server usage might steal some bus time.

### Logging & Diagnostics

The component includes multiple diagnostic logs for troubleshooting. Enable them selectively:

```yaml
logger:
  level: DEBUG
  logs:
    wled_perf: DEBUG    # FPS, frame timing, heap usage (1/sec per strip)
    wled_timing: DEBUG  # apply() call intervals, render time, skips
    wled_effect: INFO   # Effect start/stop messages
```

**Log Output Examples:**

```
[D][wled_perf]: FPS:55 Frame_ms(min/mean/max):18/18/20 Heap:195656
[D][wled_timing]: apply() interval:16574us render:1156us total:1160us skips:30
```

| Metric | Healthy Value | Issue if... |
|--------|---------------|-------------|
| FPS | 50-60 | <40 = CPU overload |
| render | <2000us | >3000us = effect too heavy |
| skips | 30 | >100 = timing issues |
| Heap | stable | dropping = memory leak |

> [!WARNING]
> `wled_timing` is verbose (30+ lines/sec). Only enable when debugging performance.

### Additional Heap Monitoring (Optional)

For more detailed heap tracking, add this interval:

```yaml
interval:
  - interval: 2s
    then:
      - lambda: |-
          ESP_LOGI("diag", "Heap: %u/%u | MaxBlock: %u",
            esp_get_free_internal_heap_size(),
            esp_get_free_heap_size(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
```
