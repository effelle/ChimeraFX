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
