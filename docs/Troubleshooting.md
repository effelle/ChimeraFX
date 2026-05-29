# Performance & Troubleshooting

## Debug Logger

**Warning**: Please do not enable debug logging on a production device unless necessary. Continuous debug logging increases CPU usage and can slow down your device. If you need to troubleshoot the component, you can toggle the **Debug Switch** found in the **Diagnostics** section of the device in Home Assistant. For safety, this switch automatically reverts to the Off state after a reboot, though it can also be disabled manually at any time. Every `cfx_light` strip will have its own dedicated debug switch unless explicitly excluded in the YAML configuration.

Example:
```c
[16:13:17.440][I][chimera_fx:550]:[WS_Strip] FX:Interference(180) | RenderFPS:58.6 | LedFPS:57.9 | Time: 17.1ms | Jitter: 0% | Heap: 95kB [ACTV]
```

### Understanding the Log Output
*   **[WS_Strip]**: The strip name in the YAML configuration.
*   **FX:Interference(180)**: The current effect running on the strip. (In the example, 'Interference' is the effect name and '180' is the ID of the effect.)
*   **RenderFPS**: How often ChimeraFX is able to calculate new frames for the effect. Ideally, this stays around **55-60 FPS**. If it consistently drops below 30, your ESP32 is struggling to keep up.
*   **LedFPS**: The measured strip-output cadence from successful RMT launches or SPI frame queues. This is the number to trust for visible LED refresh. On very long RMT strips, `RenderFPS` can look healthy while `LedFPS` falls below 30 because the 800 kHz wire time becomes the real limit.
*   **Time**: How long (in milliseconds) it took to render the last frame. Lower is better. For example, 17ms corresponds to ~59 FPS.
*   **Jitter**: The percentage of variation in frame timing. **0-8% is excellent**. Consistently high jitter (>50%) means other components (like Wi-Fi, heavy sensors, or multiple light effects running at the same time) are interrupting the LED driver, which can cause visible stuttering and low frame rates.
*   **Heap**: The available RAM on your chip. More is better. If you fall under the **Heap Floor**, you should rethink your configuration, especially if you are running the firmware on a Wi-Fi device. More information about the Heap Floor can be found in the [Memory Management & Stability](#memory-management-stability) section.
*   **[ACTV]** or **[IDLE]**: Indicates whether the effect is currently active or idle. Static effects, such as solid colors, will be marked as idle to save system resources.
 

## Common Issues

### "My strip freezes or blinks random colors"
This is almost always related to power, grounding, or signal timing.

*   **Common Ground**: Ensure your LED strip ground is connected to the ESP32 ground. Without a shared ground reference, the data signal becomes unstable and noisy.
*   **Use a Level Shifter**: The ESP32 outputs 3.3V, but 5V strips (like the WS2812B) expect a 5V signal. While short strips might work at 3.3V, a level shifter is highly recommended for stability on any serious build.
*   **Data Line Length**: Keep the wire between the ESP32 and the first LED as short as possible (ideally under 10cm). For longer runs, you can use a **"Sacrifice Pixel"** (a single LED placed close to the controller to boost the signal) or a pair of RS485 to TTL converters. For RMT strips, enable `sacrificial_pixel: true` in [`cfx_light`](cfx_light.md#optional-parameters) and keep `num_leds` set to the number of visible LEDs only. It is a dirt-cheap and highly effective solution.
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

## Performance Limits and Real FPS

ChimeraFX reports render performance and LED output cadence separately. This distinction matters when the strip is very long:

- **RenderFPS** is the effect engine rate. It tells you how quickly the ESP32 can calculate new frames.
- **LedFPS** is the practical LED refresh rate. It tells you how often complete frames are actually being launched toward the strip.
- **RMT / 1-wire NRZ strips** are locked to roughly 800 kHz. A long strip can make the wire time slower than the renderer, so the driver may coalesce or skip intermediate rendered frames before the next physical transmission completes.
- **SPI strips** are clocked much faster, so their bottleneck is usually CPU math, RAM, power delivery, or latch current rather than the raw wire protocol.

For tested LED counts, platform notes, and deployment limits, see [`cfx_light`](cfx_light.md#hardware-architecture--performance-limits). Keep `Troubleshooting` focused on symptoms: if `RenderFPS` is high but `LedFPS` is low, the effect engine is keeping up but the LED transport is saturated. If both numbers are low, reduce effect complexity, LED count, or other ESPHome workload.

---

## Performance Tuning for RMT Lights

ESP-IDF doesn't always play well with RGB lights. `ChimeraFX` tries to set the best values for `rmt_symbols` automatically, but if you experience issues like flickering leds or data corruption and you are sure is not a power or wiring issue, you must ensure your `rmt_symbols` are set correctly for your chip type. Each "symbol" represents a bit of data. Limited memory means you can't buffer infinite data. 

- **Classic ESP32**: 512 total symbols, 64-symbol blocks.
- **ESP32-S3**: 192 total symbols, 48-symbol blocks.
- **ESP32-C3**: 96 total symbols, 48-symbol blocks.

On ESP32 Classic, automatic allocation is intentionally conservative and caps each RMT light at `128` symbols. This avoids the higher completion latency seen with larger automatic blocks on long strips. If you have a tested layout that benefits from larger buffers, set `rmt_symbols` manually: for example `512` for one RMT light, `256` each for two RMT lights, or `128` each for four RMT lights.

For platform-specific output limits, see [`cfx_light`](cfx_light.md#hardware-architecture--performance-limits).

Example configuration:

```yaml
light:
  - platform: cfx_light
    id: led_strip                 
    rmt_symbols: 192   # Usually 320 is safe for ESP32 Classic, 192 for ESP32-S3
    # ... your light config ...
```

On an ESP32 Classic, when driving a single LED strip, you can utilize the full RMT buffer (512 symbols). With multiple strips, this total must be divided among them.

**ESP32-S3 Note:** The S3 RMT architecture has a smaller 192-symbol pool (managed in 48-symbol blocks), so normal RMT remains best for 1-2 ordinary strips. For dense 1-wire layouts, use the ESP32-S3 parallel backend: it is the preferred path for high LED counts, multiple SK6812/WS2812X lanes, and heavily segmented installs.

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

### Using Multiple LED Transports

Do **not** mix LED transport families on the same ESP32 node. Keep a ChimeraFX node **RMT-only**, **SPI-only**, or **parallel-only**. Mixed transport `cfx_light` entries are blocked at compile time; use separate ESP32 controllers if your installation needs multiple transport families.

Use one of these layouts instead:

1. **RMT-only node.** Use this for WS2812X, SK6812, WS2811, and other 1-wire NRZ strips.

2. **SPI-only node.** Use this for APA102 and SK9822 strips.

3. **Parallel-only node.** Use this on ESP32-S3 for grouped WS2812X/SK6812 lanes.

4. **Separate controllers when multiple transports are needed.** Put each transport family on its own ESP32. Keep power injection sized correctly and share ground only where required by the electrical layout.

The SPI power guidance below still applies to SPI-only nodes and to shared PSU planning across separate controllers.

1. **Budget for inrush, not steady-state.** A 60-LED SPI strip at full white draws ~3.6A steady but can spike to 9A+ at latch. Size your PSU for the peak, not the average.

2. **Separate power injection.** If possible, power SPI strips from a dedicated rail or separate PSU output, respecting a common ground. Never daisy-chain SPI strip power through NRZ strips.

3. **Add bulk decoupling.** Place a 100–470µF electrolytic capacitor at the SPI strip's power input, close to the first LED. This buffers the inrush spike.

4. **Add local decoupling near the ESP32.** A 100nF ceramic capacitor on the ESP32's 3.3V rail prevents brief voltage dips from propagating to the microcontroller.

5. **Test with all strips at max brightness.** The worst case is all strips set to full white simultaneously. If your setup survives that, it will handle any effect sequence.

6. **Consider the PSU's current rating.** A 10A / 5V PSU (50W) can be insufficient for a setup with several large strips. A 20A / 5V PSU (100W) provides the necessary headroom.

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
