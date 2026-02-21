# Performance & Troubleshooting

## Debug Logger

**Warning**: Please do not enable debug logging on a production device unless necessary. High-level logging increases CPU usage and can slow down your device. If you need to troubleshoot the component, you can toggle the **Debug Switch** found in the **Diagnostics** section of the device in Home Assistant. For safety, this switch automatically reverts to the Off state after a reboot, though it can also be disabled manually at any time. Every light registered trough `cfx_control` will have its own debug switch.

Example:
```bash
[15:46:46.362][I][cfx_diag:227]: [CFX] FPS:55.6 | Time: 18.0ms | Jitter: 0% | Heap: 172kB Free (108kB Max)
```

### Understanding the Logs from the example above

*   **FPS (Frames Per Second)**: Ideally stays around **55-60 FPS**. If this consistently drops below 30, your ESP32 is struggling to keep up.
*   **Time**: How long (in milliseconds) it took to render the last frame. Lower is better. ~18ms corresponds to ~55 FPS.
*   **Jitter**: The percentage of variation in frame timing. **0-5% is excellent**. Consistently high jitter (>20%) means other components (like WiFi or heavy sensors) are interrupting the LED driver, which causes visible stuttering.
*   **Heap**: The available RAM on your chip. `172kB Free` is the total free memory, while `108kB Max` is the largest contiguous block. If "Max" is significantly lower than "Free" (< 20kB), memory is fragmented and allocation failures may occur.

---
## Common Issues

### "My strip freezes or blinks random colors"
This is almost always related to power, grounding, or signal timing.

*   **Common Ground**: Ensure your LED strip ground is connected to the ESP32 ground. Without a shared ground reference, the data signal becomes unstable and noisy.
*   **Use a Level Shifter**: The ESP32 outputs 3.3V, but 5V strips (like the WS2812B) expect a 5V signal. While short strips might work at 3.3V, a level shifter is highly recommended for stability on any serious build.
*   **Data Line Length**: Keep the wire between the ESP32 and the first LED as short as possible (ideally under 10cm). For longer runs, you can use a **"Sacrifice Pixel"** (a single LED placed close to the controller to boost the signal) or a pair of RS485 to TTL converters. It is a dirt-cheap and highly effective solution.
*   **Power Injection**: Long strips suffer from voltage drop, which causes flickering or "browning out" (colors turning orange/dim). Inject power at the beginning, the end, and every few meters for consistent performance.
*   **RMT Buffer (ESP-IDF)**: If you are using the **ESP-IDF framework**, you can try to set or increase the RMT buffer size to avoid the flicker issue. Refer to the [Performance Tuning](#performance-tuning-esp-idf-and-esp32_rmt_led_strip-platform) section for more information.
*   **PSRAM (mainly ESP32-S3)**: If you are using an ESP32 with PSRAM enabled, verify that `use_psram: false` is set in your light config. RMT timing is extremely sensitive, and PSRAM latency will cause the strip to glitch.

### "The effect is too fast"
ESPHome runs at 60FPS, while WLED targets 42FPS. I have manually tuned most effects, but if one feels "rushed," try lowering the Speed slider.

### "Colors look wrong (Red is Green)"
Check your `rgb_order` in the YAML.

*   WS2812B usually uses `GRB`.
*   WS2811 usually uses `RGB`.

---

## Performance Tuning (ESP-IDF and esp32_rmt_led_strip Platform)

ESP-IDF doesn't always play well with RGB lights. If you experience issues like flickering leds or data corruption and you are sure is not a power or wiring issue, you must ensure your `rmt_symbols` are set correctly for your chip type. Each "symbol" represents a bit of data. Limited memory means you can't buffer infinite data. 

- **Classic ESP32**: 512 Total Symbols - Block size 64 symbols 
- **ESP32-S3**: 192 Total Symbols - Block size 48 symbols 

Also, set `use_psram: false` in your light config, especially if you are using an ESP32-S3. PSRAM represents external RAM which is significantly slower than the ESP32's internal SRAM. The RMT (Remote Control) peripheral requires high-speed data access to generate accurate timing for addressable LEDs. Using PSRAM can cause timing jitter, resulting in flickering or data corruption.

Example configuration:

```yaml
light:
  - platform: esp32_rmt_led_strip
    id: led_strip                 
    rmt_symbols: 192   # Usually 320 is a safe number for ESP32 Classic, 192 for ESP32-S3
    use_psram: false
    # ... your light config ...
```

On an ESP32 Classic, when driving a single LED strip, you can utilize the full RMT buffer (512 symbols). However, with multiple strips, this total must be divided among them. On an ESP32-S3, the RMT architecture is different: it has dedicated memory for transmission, but the total buffer is smaller. The S3 provides 192 symbols total for all transmit channels. This means if you use 4 strips, you are limited to 48 symbols per strip unless you use DMA.