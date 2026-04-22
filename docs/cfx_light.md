# ChimeraFX Light Platform

The ChimeraFX Light Platform (`cfx_light`) is a custom, high-performance ESPHome light component specifically designed to be the ultimate companion for ChimeraFX. 
`cfx_light` wraps native ESP-IDF drivers with opinionated optimizations tailored for heavy lifting and seamless effect integration.

## Why use `cfx_light`?

1. **Auto-injection (`all_effects`)**: The biggest feature! By default, `cfx_light` will automatically parse and inject all ChimeraFX effects into your device at compile time. No more `!include` macros, and no more bloated YAML files with hundreds of lines of effect blocks. You can disable this feature by setting `all_effects: false`.
2. **Chipset-Aware Intelligence**: Native understanding of the strip's timing requirements.
3. **Optimized RMT Symbols Allocation**: ESPHome's standard RMT driver occasionally struggles with symbol buffering on different chips (like S3 vs Classic). `cfx_light` automatically sets the optimal RMT memory boundaries for your exact silicon, eliminating the "flickering" or "data corruption" issues associated with large LED strips. Can be manually overridden with `rmt_symbols`.
4. **Automatic RGBW Handling**: If you select `SK6812`, it automatically configures the 4-byte protocol and `GRBW` formatting without requiring manual overrides (although you can still override them if you have a weird LED strip variant).

---

## Configuration Variables

```yaml
light:
  - platform: cfx_light
    name: "Living Room Light"
    id: led_strip
    pin: GPIO16
    num_leds: 300
    chipset: WS2812X
    all_effects: true
    use_intro: 1         # Global Wipe intro
    use_outro: 2         # Global Fade outro
    intro_dur: 1000ms    # 1s duration
```

### Required Parameters
* **name** (*string*): The name of the light in Home Assistant.
* **id** (*ID*): The ID of the light component.
* **pin** (*Pin*): The GPIO pin the data line of your LED strip is connected to.
* **num_leds** (*int*): The total number of LEDs in your strip.
### Supported Chipsets
`cfx_light` supports both **1-wire (NRZ)** and **2-wire (SPI)** chipsets. It utilizes native hardware peripherals (RMT and SPI Master) to generate precise timings:

* **1-wire NRZ**: WS2812, WS2812B, WS2813, WS2815, SK6812 (RGBW), WS2811.
* **2-wire SPI**: APA102, SK9822.

### Configuration Variables
To use a specific chipset, use the `chipset` variable in your YAML:

| Parameter | Chipset Option | Description |
|:---|:---|:---|
| **chipset** | `WS2812X` | Standard 3-pin RGB timing (Default) |
| | `SK6812` | 4-byte RGBW timing (GRBW order) |
| | `WS2811` | Optimized timing for WS2811 data rates |
| | `APA102` | 2-wire SPI timing |
| | `SK9822` | 2-wire SPI timing |

### Optional Parameters
* **all_effects** (*boolean*, default: `true`): When set to `true`, the component will instantly register all effects. Set to `false` to manually register effects or custom presets.
* **rgb_order** (*string*): The byte order of the colors. If omitted, `cfx_light` sets the standard default based on your `chipset` (e.g., `WS2812X` defaults to `GRB`). Options: `RGB`, `RBG`, `GRB`, `GBR`, `BGR`, `BRG`.
* **is_rgbw** (*boolean*): Explicitly declare the strip as 4-byte RGBW. If your chipset is `SK6812`, this is automatically `true`.
* **is_wrgb** (*boolean*, default: `false`): Sets the white byte position to the *front* of the data packet rather than the end. Required for some rare SK6812 variant clones.
* **rmt_symbols** (*int*, default: `0`): The number of RMT symbols to allocate. If left at `0`, `cfx_light` will dynamically allocate the maximum safe bounds based on your specific ESP32 processor variant.
* **max_refresh_rate** (*Time*, default: `16ms`): Controls the ESPHome frame limit. ChimeraFX will automatically adjust the frame rate to match the slowest segment.
* **default_transition_length** (*Time*, default: `0s`): The standard ESPHome transition duration.
* **use_intro** (*int*): Force a specific global Intro Animation for all effects.
* **use_outro** (*int*): Force a specific global Outro Animation for all effects.
* **intro_dur** (*Time*): Sets the duration for both global intros and outros.

---

## Overriding Specific Effects

The `all_effects: true` command is incredibly powerful, but what if you want to set specific hardcoded default presets for a single effect? Say, you want the `Aurora` effect to automatically use the `Forest` palette on boot.

`cfx_light` is smart enough to handle overrides out of the box! If you declare `all_effects: true` but also manually define an `addressable_cfx` effect beneath it with the **exact same name**, your manual override will completely supersede the auto-injected version.

```yaml
light:
  - platform: cfx_light
    name: "Living Room Light"
    id: led_strip
    pin: GPIO16
    num_leds: 300
    chipset: WS2812X

    # We want ALL effects, but we want to customize 'Aurora'
    effects:
      - addressable_cfx:
          name: "Aurora"
          effect_id: 38
          set_palette: 2   # Force 'Forest' 
          set_speed: 100   # Make it much faster
```

With the above configuration, your ESP32 will compile with all 50+ effects, but `Aurora` will use your tailored speed and palette settings out of the box. More on effect presets in the [Effect Presets](Effect-Presets.md) page.

---

## Segments (Multi-Zone Control)

ChimeraFX supports dividing a single physical LED strip into up to **4 independent logical segments**. Each segment is exposed to Home Assistant as a separate light entity, allowing you to run different effects on different parts of the same strip simultaneously.

### Segment Configuration

Segments are defined under the `segments` key in your `cfx_light` configuration.

```yaml
light:
  - platform: cfx_light
    name: "Main TV Strip"
    id: tv_strip
    pin: GPIO16
    num_leds: 120
    chipset: WS2812X
    
    segments:
      - id: "tv_left"
        name: "TV Left Side"
        start: 0
        stop: 40
      - id: "tv_top"
        name: "TV Top"
        start: 40
        stop: 80
        mirror: true
      - id: "tv_right"
        name: "TV Right Side"
        start: 80
        stop: 120
```

### Segment Parameters

* **id** (*string*, Required): A unique ID for the segment.
* **name** (*string*, Optional): The name of the light entity in Home Assistant. If omitted, the `id` is used.
* **start** (*int*, Required): The starting pixel index (inclusive).
* **stop** (*int*, Required): The stopping pixel index (exclusive).
* **mirror** (*boolean*, default: `false`): If true, calculations are reversed for this segment (useful for symmetrical setups).
* **use_intro** / **use_outro** (*int*, Optional): Override the global intro/outro modes for this specific segment.
* **intro_dur** (*Time*, Optional): Override the global intro/outro duration for this specific segment.

### Master vs. Segment Behavior

When segments are defined:

1.  The "Master" light entity (e.g., "Main TV Strip") acts as a global power and brightness control. It does **not** have its own effects. Turning off the Master turns off all segments.

2.  Each segment light entity (e.g., "TV Left Side") has the **full suite of ChimeraFX effects** injected into it (if `all_effects: true` is set on the parent).

3.  Segments can run different effects, speeds, and palettes independently.

---

## Hardware & Driver Architecture

`cfx_light` is a high-performance, asynchronous DMA driver. Unlike standard platforms that provide a broad but generic compatibility layer, `cfx_light` is specifically optimized for visual excellence and stability on the ESP32:

- **Universal RMT Backend:** Whether you use the **ESP-IDF** or **Arduino** framework in ESPHome, this component leverages the native ESP32 RMT (Remote Control) peripheral directly via the IDF drivers. This ensures fire-and-forget DMA transmissions regardless of your framework choice.
- **Protocol Support:** **1-wire NRZ** (WS2812X, SK6812, WS2811) and **2-wire SPI** (APA102, SK9822)
- **Curated Timing Engine:** Because ChimeraFX uses a custom timing generator to ensure 1:1 color accuracy and zero-flicker performance, it supports a curated list of the most popular 1-wire NRZ and 2-wire SPI chipsets.

If your 1-wire NRZ chipset is not on the supported list but uses standard 800Kbps timings, `WS2812X` is often a compatible drop-in choice. If your 2-wire SPI chipset is not on the supported list, `APA102` is often a compatible drop-in choice.

---

## SPI Power Planning

> **⚠️ Important:** APA102 and SK9822 strips have fundamentally different power characteristics than 1-wire NRZ strips (WS2812B, SK6812). Failure to account for this can cause system instability, especially in multi-strip setups.

### The Inrush Current Problem

When an SPI strip latches new pixel data, **all LEDs update simultaneously** (triggered by the end-of-frame clock sequence). This creates a transient current spike that can be **2–3× higher** than the strip's steady-state draw. In contrast, NRZ strips update LEDs sequentially as data propagates down the chain, spreading the current draw over time.

| Characteristic | 1-wire NRZ (WS2812B) | 2-wire SPI (APA102/SK9822) |
|:---|:---|:---|
| **Update behavior** | Sequential (LED by LED) | Simultaneous (all LEDs at once) |
| **Steady-state per LED** | ~50–60 mA (full white) | ~50–60 mA (full white) |
| **Inrush per LED** | ~60 mA (same as steady) | Up to **150–180 mA** for 0.3–0.5s |
| **64-LED strip peak** | ~3.8 A | Up to **11.5 A** (brief) |

> **Note:** Inrush values vary significantly across SK9822 clone batches. Genuine APA102 strips tend to have lower inrush, but cheap SK9822 clones can exhibit the worst-case values above.

### Symptoms of Insufficient Power Budget

When the SPI inrush current exceeds your power supply's headroom:

- **ESP32 hard reset** (appears as `ESP_RST_POWERON` — the voltage drops below the ESP32's ~2.0V power-on-reset threshold)
- **API disconnects** followed by immediate reconnection
- **Intermittent crashes** that only occur when multiple strips run effects simultaneously
- System works fine with the SPI strip alone, but crashes when combined with 5+ NRZ strips

These symptoms are **not software bugs** — they are hardware power-rail collapses.

### Best Practices for Mixed SPI + NRZ Installations

1. **Budget for inrush, not steady-state.** A 64-LED SPI strip at full white draws ~3.8A steady but can spike to 11A+ at latch. Size your PSU for the peak, not the average.

2. **Separate power injection.** If possible, power SPI strips from a dedicated rail or separate PSU output. Never daisy-chain SPI strip power through NRZ strips.

3. **Add bulk decoupling.** Place a 100–470µF electrolytic capacitor at the SPI strip's power input, close to the first LED. This buffers the inrush spike.

4. **Add local decoupling near the ESP32.** A 100nF ceramic capacitor on the ESP32's 3.3V rail prevents brief voltage dips from propagating to the microcontroller.

5. **Test with all strips at max brightness.** The worst case is all strips set to full white simultaneously. If your setup survives that, it will handle any effect sequence.

6. **Consider the PSU's current rating.** A 10A / 5V PSU (50W) can be insufficient for a setup with multiple NRZ strips + one SPI strip. A 20A / 5V PSU (100W) provides the necessary headroom.
