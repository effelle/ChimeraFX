# Performance & Troubleshooting

## DebugLogger
I cannot stress this enough: **DO NOT** enable the logger on a production device if you don't need it. It will slow down your device, but if, for some reason, you need to debug the component, you can enable it just flipping a switch. You can find it on **Diagnostics** tab in Home Assistant.

```bash
[15:46:46.362][I][cfx_diag:227]: [CFX] FPS:55.6 | Time: 18.0ms | Jitter: 0% | Heap: 172kB Free (108kB Max)
```

### Understanding the Logs

*   **FPS (Frames Per Second)**: Ideally stays around **55-60 FPS**. If this consistently drops below 30, your ESP32 is struggling to keep up.
*   **Time**: How long (in milliseconds) it took to render the last frame. Lower is better. ~18ms corresponds to ~55 FPS.
*   **Jitter**: The percentage of variation in frame timing. **0-5% is excellent**. Consistently high jitter (>20%) means other components (like WiFi or heavy sensors) are interrupting the LED driver, which causes visible stuttering.
*   **Heap**: The available RAM on your chip. `172kB Free` is the total free memory, while `108kB Max` is the largest contiguous block. If "Max" is significantly lower than "Free" (< 20kB), memory is fragmented and allocation failures may occur.

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


