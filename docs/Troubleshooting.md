# Performance & Troubleshooting

## Debug Logger

**Warning**: Please do not enable debug logging on a production device unless necessary. High-level logging increases CPU usage and can slow down your device. If you need to troubleshoot the component, you can toggle the **Debug Switch** found in the **Diagnostics** section of the device in Home Assistant. For safety, this switch automatically reverts to the Off state after a reboot, though it can also be disabled manually at any time. Every light registered through `cfx_control` will have its own dedicated debug switch.

Example:
```bash
[16:13:17.440][I][chimera_fx:550]: [WS_Strip] FX:Interference(180) | FPS:58.6 | Time: 17.1ms | Jitter: 0% | Heap: 95kB [ACTV]
```

### Understanding the Log Output
*   **[WS_Strip]**: The strip name in YAML configuration.
*   **FX:Interference(180)**: The current effect running on the strip. (In the example, 'Interference' is the effect name and '180' is the ID of the effect)
*   **FPS (Frames Per Second)**: Ideally, this stays around **55-60 FPS**. If it consistently drops below 30, your ESP32 is struggling to keep up.
*   **Time**: How long (in milliseconds) it took to render the last frame. Lower is better. For example, 17ms corresponds to ~59 FPS.
*   **Jitter**: The percentage of variation in frame timing. **0-8% is excellent**. Consistently high jitter (>30%) means other components (like WiFi, heavy sensors or multiple light effects running at the same time) are interrupting the LED driver, which can cause visible stuttering and low frame rate.
*   **Heap**: The available RAM on your chip. More is better. If you fall under the **Heap Floor**, you should rethink your configuration, especially if you are running the firmware on a WiFi device. More information about Heap Floor can be found on [Memory Management & Stability](#memory-management-stability) chapter.

---


## Debug Logger

**Warning**: Please do not enable debug logging on a production device unless necessary. Continuous debug logging increases CPU usage and can slow down your device. If you need to troubleshoot the component, you can toggle the **Debug Switch** found in the **Diagnostics** section of the device in Home Assistant. For safety, this switch automatically reverts to the Off state after a reboot, though it can also be disabled manually at any time. Every `cfx_light` strip will have its own dedicated debug switch unless explicitly excluded in the YAML configuration.

Example:
```c
[16:13:17.440][I][chimera_fx:550]:[WS_Strip] FX:Interference(180) | FPS:58.6 | Time: 17.1ms | Jitter: 0% | Heap: 95kB [ACTV]
```

### Understanding the Log Output
*   **[WS_Strip]**: The strip name in the YAML configuration.
*   **FX:Interference(180)**: The current effect running on the strip. (In the example, 'Interference' is the effect name and '180' is the ID of the effect.)
*   **FPS (Frames Per Second)**: Ideally, this stays around **55-60 FPS**. If it consistently drops below 30, your ESP32 is struggling to keep up.
*   **Time**: How long (in milliseconds) it took to render the last frame. Lower is better. For example, 17ms corresponds to ~59 FPS.
*   **Jitter**: The percentage of variation in frame timing. **0-8% is excellent**. Consistently high jitter (>50%) means other components (like Wi-Fi, heavy sensors, or multiple light effects running at the same time) are interrupting the LED driver, which can cause visible stuttering and low frame rates.
*   **Heap**: The available RAM on your chip. More is better. If you fall under the **Heap Floor**, you should rethink your configuration, especially if you are running the firmware on a Wi-Fi device. More information about the Heap Floor can be found in the [Memory Management & Stability](#memory-management-stability) section.


## Common Issues

### "My strip freezes or blinks random colors"
This is almost always related to power, grounding, or signal timing.

*   **Common Ground**: Ensure your LED strip ground is connected to the ESP32 ground. Without a shared ground reference, the data signal becomes unstable and noisy.
*   **Use a Level Shifter**: The ESP32 outputs 3.3V, but 5V strips (like the WS2812B) expect a 5V signal. While short strips might work at 3.3V, a level shifter is highly recommended for stability on any serious build.
*   **Data Line Length**: Keep the wire between the ESP32 and the first LED as short as possible (ideally under 10cm). For longer runs, you can use a **"Sacrifice Pixel"** (a single LED placed close to the controller to boost the signal) or a pair of RS485 to TTL converters. It is a dirt-cheap and highly effective solution.
*   **Power Injection**: Long strips suffer from voltage drop, which causes flickering or "browning out" (colors turning orange/dim). Inject power at the beginning, the end, and every few meters for consistent performance.
*   **RMT Light Buffer**: You can try to set or increase the RMT buffer size to avoid the flicker issue. It can be set under `cfx_light` component, using `rmt_symbols` option.

### "The effect is too fast"
ChimeraFX try to run all effects at 60FPS, and I have manually tuned most effects, but if one feels "rushed," try lowering the Speed slider.

### "Colors look wrong (Red is Green)"
Check your `rgb_order` in the YAML.

*   WS2812B usually uses `GRB`.
*   WS2811 usually uses `RGB`.
*   SK9822 usually uses `BGR`.

---

## Performance Tuning for RMT Lights

ESP-IDF doesn't always play well with RGB lights. `ChimeraFX` tries to set the best values for `rmt_symbols` automatically, but if you experience issues like flickering leds or data corruption and you are sure is not a power or wiring issue, you must ensure your `rmt_symbols` are set correctly for your chip type. Each "symbol" represents a bit of data. Limited memory means you can't buffer infinite data. 

- **Classic ESP32**: 512 Total Symbols - Block size 64 symbols 
- **ESP32-S3**: 192 Total Symbols - Block size 48 symbols 
- **ESP32-C3**: 96 Total Symbols - Block size 48 symbols 

**Note**: with only 96 RMT symbols (48-symbol blocks), the ESP32-C3 maxes out at 2 TX channels. Declaring 3 or more `cfx_light` strips on a C3 is a **compile-time error**.

Example configuration:

```yaml
light:
  - platform: cfx_light
    id: led_strip                 
    rmt_symbols: 192   # Usually 320 is safe for ESP32 Classic, 192 for ESP32-S3
    # ... your light config ...
```

On an ESP32 Classic, when driving a single LED strip, you can utilize the full RMT buffer (512 symbols). However, with multiple strips, this total must be divided among them. On an ESP32-S3, the RMT architecture is different: it has dedicated memory for transmission, but the total buffer is smaller. The S3 provides 192 symbols total for all transmit channels. This means if you use 4 strips, you are limited to 48 symbols per strip.

---

## SPI Power Planning

> **⚠️ Important:** APA102 and SK9822 strips have fundamentally different power characteristics than 1-wire NRZ strips (WS2812B, SK6812). Failure to account for this can cause system instability, especially in multi-strip setups.

### The Inrush Current Problem

When an SPI strip latches new pixel data, **all LEDs update simultaneously** (triggered by the end-of-frame clock sequence). This creates a transient current spike that can be **2–3× higher** than the strip's steady-state draw. In contrast, NRZ strips update LEDs sequentially as data propagates down the chain, spreading the current draw over time.

| Characteristic | 1-wire NRZ (WS2812B) | 2-wire SPI (APA102/SK9822) |
|:---|:---|:---|
| **Update behavior** | Sequential (LED by LED) | Simultaneous (all LEDs at once) |
| **Steady-state per LED** | ~50–60 mA (full white) | ~50–60 mA (full white) |
| **Inrush per LED** | ~60 mA (same as steady) | Up to **120–150 mA** for 0.2–0.4s |
| **60-LED strip peak** | ~3.6 A | Up to **9 A** (brief) |

> **Note:** Inrush values vary significantly across SK9822 clone batches. Genuine APA102 strips tend to have lower inrush, but cheap SK9822 clones can exhibit the worst-case values above.

### Symptoms of Insufficient Power Budget

When the SPI inrush current exceeds your power supply's headroom:

- **ESP32 hard reset** (appears as `ESP_RST_POWERON` — the voltage drops below the ESP32's ~2.0V power-on-reset threshold)
- **API disconnects** followed by immediate reconnection
- **Intermittent crashes** that only occur when multiple strips run effects simultaneously
- System works fine with the SPI strip alone, but crashes when combined with two or more NRZ strips

These symptoms are **not software bugs** — they are hardware power-rail collapses.

### Best Practices for Mixed SPI + NRZ Installations

1. **Budget for inrush, not steady-state.** A 60-LED SPI strip at full white draws ~3.6A steady but can spike to 9A+ at latch. Size your PSU for the peak, not the average.

2. **Separate power injection.** If possible, power SPI strips from a dedicated rail or separate PSU output, respecting a common ground. Never daisy-chain SPI strip power through NRZ strips.

3. **Add bulk decoupling.** Place a 100–470µF electrolytic capacitor at the SPI strip's power input, close to the first LED. This buffers the inrush spike.

4. **Add local decoupling near the ESP32.** A 100nF ceramic capacitor on the ESP32's 3.3V rail prevents brief voltage dips from propagating to the microcontroller.

5. **Test with all strips at max brightness.** The worst case is all strips set to full white simultaneously. If your setup survives that, it will handle any effect sequence.

6. **Consider the PSU's current rating.** A 10A / 5V PSU (50W) can be insufficient for a setup with multiple NRZ strips + one SPI strip. A 20A / 5V PSU (100W) provides the necessary headroom.

## Memory Management & Stability

Visual effects are computationally expensive, and microcontroller hardware resources are finite, particularly RAM. Because ChimeraFX is an ecosystem component that shares resources with other ESPHome components, it employs a dynamic **Heap Floor Guard** to prevent memory starvation and system instability (such as spontaneous resets or API disconnects). 

Whenever a `cfx_light` is turned on to run an effect, **ChimeraFX** checks the available RAM against this dynamic floor. If the available RAM is too low, it logs a warning, forces the impacted light to solid red for 5 seconds, and then turns that same light OFF.

**How the Limit is Calculated:**
To maximize available RAM without penalizing lightweight setups, the limit is calculated automatically during compilation based on the components you include:
- Base Margin: `15kB`
- Wi-Fi Stack: `+30kB`
- ESPHome API: `+10kB`
- Bluetooth Stack: `+20kB`

For example, a standard Wi-Fi node (Base + Wi-Fi + API) will enforce a floor of `55kB`, whereas a "heavy" node that also includes Bluetooth will enforce a `75kB` floor to safely protect the radio stacks and UI buffers.

The amount of RAM reserved by the **Heap Floor Guard** is displayed in the logs at boot time:

```yaml
# Example log of an ESP32 with just Wi-Fi and API enabled
[15:53:48.823][C][cfx_light:1896]: System CFX Heap Floor dynamically set to: 55000 B
```
Fortunately, you can free up RAM with a few minor adjustments. Because every setup is different, please consider the following suggestions as a starting point:

**Webserver**: Unless you are running ESPHome standalone without Home Assistant, the webserver is completely redundant. It is not optimized for modern web standards, and disabling it won't affect your Home Assistant integration at all. Removing it will free ~8 kB.

**mDNS**: The ESPHome API relies on mDNS for device discovery, especially during the first registration with Home Assistant. However, if you utilize static IP addresses and manually add your devices to Home Assistant, it is generally safe to disable mDNS:
```yaml
mdns: 
  disabled: true
```
Disabling this feature will free up ~7 kB of RAM.

Combined, these two simple changes save enough RAM to easily support a couple of mid-sized lights running complex effects.
