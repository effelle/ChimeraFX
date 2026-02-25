# ChimeraFX Light Platform

The `cfx_light` platform is a custom, high-performance ESPHome light component specifically designed to be the ultimate companion for ChimeraFX. 

While you can run ChimeraFX effects on top of standard `esp32_rmt_led_strip` or `neopixelbus` platforms, the `cfx_light` platform offers significant advantages. It wraps the native ESP-IDF RMT driver with opinionated optimizations tailored for heavy lifting and seamless effect integration.

## Why use `cfx_light`?

1. **Auto-injection (`all_effects`)**: The biggest feature! By simply setting `all_effects: true`, `cfx_light` will automatically parse and inject all 50+ ChimeraFX effects into your device at compile time. No more `!include` macros, and no more bloated YAML files with hundreds of lines of effect blocks.
2. **Chipset-Aware Intelligence**: Native understanding of WS2812, WS2811, and SK6812 timing requirements.
3. **Optimized Memory Allocation**: ESPHome's standard RMT driver occasionally struggles with symbol buffering on different chips (like S3 vs Classic). `cfx_light` automatically sets the optimal RMT memory boundaries for your exact silicon, eliminating the "flickering" or "data corruption" issues associated with large LED strips.
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
```

### Required Parameters
* **name** (*string*): The name of the light in Home Assistant.
* **id** (*ID*): The ID of the light component.
* **pin** (*Pin*): The GPIO pin the data line of your LED strip is connected to.
* **num_leds** (*int*): The total number of LEDs in your strip.
### Supported Chipsets
`cfx_light` is a high-speed **1-wire (NRZ)** driver. It utilizes the ESP32 RMT peripheral to generate industrial-grade timings for the following chipsets:

* **WS2812 / WS2812B / WS2813 / WS2815** (General 3-pin RGB strips)
* **SK6812** (RGBW strips with a dedicated white channel)
* **WS2811** (Standard 12V 3-LED segment strips)

> [!WARNING]
> **SPI strips are NOT supported.**  
> 2-wire chipsets that require a separate Clock signal (such as **APA102, SK9822, WS2801, LPD8806**) are not compatible with this driver.

### Configuration Variables
To use a specific chipset, use the `chipset` variable in your YAML:

| Parameter | Chipset Option | Description |
|:---|:---|:---|
| **chipset** | `WS2812X` | Standard 3-pin RGB timing (Default) |
| | `SK6812` | 4-byte RGBW timing (GRBW order) |
| | `WS2811` | Optimized timing for WS2811 data rates |

### Optional Parameters
* **all_effects** (*boolean*, default: `false`): When set to `true`, the component will look at the `chimera_fx_effects.yaml` file in your project root and instantly register all effects.
* **rgb_order** (*string*): The byte order of the colors. If omitted, `cfx_light` sets the standard default based on your `chipset` (e.g., `WS2812X` defaults to `GRB`). Options: `RGB`, `RBG`, `GRB`, `GBR`, `BGR`, `BRG`.
* **is_rgbw** (*boolean*): Explicitly declare the strip as 4-byte RGBW. If your chipset is `SK6812`, this is automatically `true`.
* **is_wrgb** (*boolean*, default: `false`): Sets the white byte position to the *front* of the data packet rather than the end. Required for some rare SK6812 variant clones.
* **rmt_symbols** (*int*, default: `0`): The number of RMT symbols to allocate. If left at `0`, `cfx_light` will dynamically allocate the maximum safe bounds based on your specific ESP32 processor variant.
* **max_refresh_rate** (*Time*, default: `16ms`): Controls the ESPHome frame limit. `16ms` achieves ~62 FPS.
* **default_transition_length** (*Time*, default: `0s`): The standard ESPHome transition duration.

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
    all_effects: true

    # We want ALL effects, but we want to customize 'Aurora'
    effects:
      - addressable_cfx:
          name: "Aurora"
          effect_id: 38
          set_palette: 2   # Force 'Forest' 
          set_speed: 100   # Make it much faster
```

With the above configuration, your ESP32 will compile with all 50+ effects, but `Aurora` will use your tailored speed and palette settings out of the box. More on effect presets in the [Effect Presets](Effect-Presets.md) page.

## Hardware & Driver Architecture

`cfx_light` is a high-performance, asynchronous DMA driver. Unlike standard platforms that provide a broad but generic compatibility layer, `cfx_light` is specifically optimized for visual excellence and stability on the ESP32:

- **Universal RMT Backend:** Whether you use the **ESP-IDF** or **Arduino** framework in ESPHome, this component leverages the native ESP32 RMT (Remote Control) peripheral directly via the IDF drivers. This ensures fire-and-forget DMA transmissions regardless of your framework choice.
- **Curated Timing Engine:** Because we use a custom timing generator to ensure 1:1 color accuracy and zero-flicker performance, we support a curated list of the most popular 1-wire NRZ chipsets. We do not support the broad and often unstable list of older or 2-wire chipsets found in generic platforms.

If your chipset is not on the supported list but uses standard 800Kbps timings, `WS2812X` is often a compatible drop-in choice.
