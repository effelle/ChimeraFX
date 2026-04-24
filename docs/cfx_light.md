# ChimeraFX Light Platform

The ChimeraFX Light Platform (`cfx_light`) is a custom, high-performance ESPHome light component specifically designed to be the ultimate companion for ChimeraFX. 
`cfx_light` wraps native ESP-IDF drivers with opinionated optimizations tailored for heavy lifting and seamless effect integration.

> **Limits:** ChimeraFX currently supports up to **4** `cfx_light` instances per node, in any mix of RMT and SPI lights. Each `cfx_light` can define up to **3** segments.

## Why use `cfx_light`?

1. **Auto-injection (`all_effects`)**: The biggest feature! By default, `cfx_light` will automatically parse and inject all ChimeraFX effects into your device at compile time. No more `!include` macros, and no more bloated YAML files with hundreds of lines of effect blocks. You can disable this feature by setting `all_effects: false`.
2. **Chipset-Aware Intelligence**: Native understanding of the strip's timing requirements.
3. **Optimized RMT Symbols Allocation**: ESPHome's standard RMT driver occasionally struggles with symbol buffering on different chips (like S3 vs Classic). `cfx_light` automatically sets the optimal RMT memory boundaries for your exact silicon, eliminating the "flickering" or "data corruption" issues associated with large LED strips. Can be manually overridden with `rmt_symbols`.
4. **Automatic RGBW Handling**: If you select `SK6812`, it automatically configures the 4-byte protocol and `GRBW` formatting without requiring manual overrides (although you can still override them if you have a weird LED strip variant).

---

## CFX Light Configuration

```yaml
# Example config for 1-wire NRZ strips (WS2812X, SK6812, WS2811):
light:
  - platform: cfx_light
    name: "LED Strip RMT"
    id: led_strip_rmt
    pin: GPIO16
    num_leds: 120
    chipset: WS2812X

# Example config for 2-wire SPI strips (APA102, SK9822):
light:
  - platform: cfx_light
    name: "LED Strip SPI"
    id: led_strip_spi
    data_pin: GPIO23       # Data pin required for SPI strips
    clock_pin: GPIO18      # Clock pin required for SPI strips
    spi_speed: 10MHz       # SPI speed for SPI strips
    num_leds: 120          
    chipset: SK9822
```

### Required Parameters
#### For 1-wire NRZ chipsets:
* **name** (*string*): The name of the light in Home Assistant.
* **id** (*ID*): The ID of the light component.
* **pin** (*Pin*): The GPIO pin the data line of your LED strip is connected to.
* **chipset** (*string*): The type of LED strip you are using.
* **num_leds** (*int*): The total number of LEDs in your strip.
#### For 2-wire SPI chipsets:
* **name** (*string*): The name of the light in Home Assistant.
* **id** (*ID*): The ID of the light component.
* **data_pin** (*Pin*): The GPIO pin the data line of your LED strip is connected to.
* **clock_pin** (*Pin*): The GPIO pin the clock line of your LED strip is connected to.
* **chipset** (*string*): The type of LED strip you are using.
* **num_leds** (*int*): The total number of LEDs in your strip.
### Supported Chipsets
`cfx_light` supports both **1-wire (NRZ)** and **2-wire (SPI)** chipsets. It utilizes native hardware peripherals (RMT and SPI Master) to generate precise timings:

* **1-wire NRZ**: WS2812, WS2812B, WS2813, WS2815, SK6812 (RGBW), WS2811.
* **2-wire SPI**: APA102, SK9822 (beta driver).

### Chipset Identification and Configuration
To use a specific chipset, use the `chipset` variable in your YAML:

| Parameter | Chipset Option | Description |
|:---|:---|:---|
| **chipset** | `WS2812X` | Standard 3-pin RGB timing (Default) |
| | `SK6812` | 4-byte RGBW timing (GRBW order) |
| | `WS2811` | Optimized timing for WS2811 data rates |
| | `APA102` | 2-wire SPI timing (beta driver) |
| | `SK9822` | 2-wire SPI timing (beta driver) |

### Optional Parameters
* **all_effects** (*boolean*, default: `true`): When set to `true`, the component will instantly register all effects. Set to `false` to manually register effects or custom presets.
* **rgb_order** (*string*): The byte order of the colors. If omitted, `cfx_light` sets the standard default based on your `chipset` (e.g., `WS2812X` defaults to `GRB`). Options: `RGB`, `RBG`, `GRB`, `GBR`, `BGR`, `BRG`.
* **is_rgbw** (*boolean*): Explicitly declare the strip as 4-byte RGBW. If your chipset is `SK6812`, this is automatically `true`.
* **is_wrgb** (*boolean*, default: `false`): Sets the white byte position to the *front* of the data packet rather than the end. Required for some rare SK6812 variant clones.
* **rmt_symbols** (*int*, default: `0`): The number of RMT symbols to allocate. If left at `0`, `cfx_light` will dynamically allocate the maximum safe bounds based on your specific ESP32 processor variant.
* **spi_speed** (*Frequency*, Optional): The SPI clock speed for `APA102` and `SK9822` strips. If omitted, `cfx_light` uses a sensible default.
* **spi_host** (*string*, Optional): Selects the ESP-IDF SPI host to use for SPI strips. Options: `SPI2_HOST`, `SPI3_HOST`.
* **default_transition_length** (*Time*, default: `0s`): The standard ESPHome transition duration for **solid color** light **when no effect is selected**.
* **set_intro** (*int*, Optional): Force a specific global Intro Animation for all effects.
* **set_outro** (*int*, Optional): Force a specific global Outro Animation for all effects.
* **set_inout_dur** (*Time*, Optional): Sets the duration for both global intros and outros.
* **set_brightness** (*percentage*, Optional): Sets the default brightness for the light, using the same `0-100%` style as ESPHome light brightness values.
* **set_color** (*list[int]*, Optional): Sets the default base color for the light as `[r, g, b]` or `[r, g, b, w]` using `0-100` channel percentages (`w` requires a white-channel strip). This seeds solid-color mode and also affects single-tone effects that derive their color from the current light state. It does not force palette-driven multicolor effects to a single color.
* **controls** (*boolean*, default: `true`): Automatically generate the ChimeraFX control entities for this light.
* **ctrl_exclude** (*list[int]*, Optional): Exclude specific auto-generated control groups by ID. See [Controls](Controls.md) for the control ID list.
* **segments** (*list*, Optional): Define logical sub-zones of the strip as independent light entities, up to **3** per `cfx_light`.

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

With the above configuration, your ESP32 will compile with all the effects, but `Aurora` will use your tailored speed and palette settings out of the box. More on effect presets in the [Effect Presets](Effect-Presets.md) page.

---

## Segments (Multi-Zone Control)

ChimeraFX supports dividing a single physical LED strip into up to **3 independent logical segments**. Each segment is exposed to Home Assistant as a separate light entity, allowing you to run different effects on different parts of the same strip simultaneously.

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

* **id** (*ID*, Required): A unique ESPHome `id` for the segment light entity.
* **name** (*string*, Optional): The name of the light entity in Home Assistant. If omitted, the `id` is used.
* **start** (*int*, Required): The starting pixel index (inclusive).
* **stop** (*int*, Required): The stopping pixel index (exclusive).
* **mirror** (*boolean*, default: `false`): If true, calculations are reversed for this segment (useful for symmetrical setups).
* **set_intro** (*int*, Optional): Override the global intro mode for this specific segment.
* **set_outro** (*int*, Optional): Override the global outro mode for this specific segment.
* **set_inout_dur** (*Time*, Optional): Override the global intro/outro duration for this specific segment.
* **set_brightness** (*percentage*, Optional): Override the segment's default brightness, using the same `0-100%` style as ESPHome light brightness values.
* **set_color** (*list[int]*, Optional): Override the segment's default base color as `[r, g, b]` or `[r, g, b, w]` using `0-100` channel percentages (`w` requires a white-channel strip). Like the parent light, this affects solid-color mode and single-tone effects that use the light state's color.

### Master vs. Segment Behavior

When segments are defined:

1.  The "Master" light entity (e.g., "Main TV Strip") acts as a global power and brightness control. It does **not** have its own effects. Turning off the Master turns off all segments.

2.  Each segment light entity (e.g., "TV Left Side") has the **full suite of ChimeraFX effects** injected into it (unless `all_effects: false` is set on the parent).

3.  Segments can run different effects, speeds, and palettes independently.

---

## Hardware & Driver Architecture

`cfx_light` is a high-performance, asynchronous DMA driver. Unlike standard platforms that provide a broad but generic compatibility layer, `cfx_light` is specifically optimized for visual excellence and stability on the ESP32:

- **Universal RMT Backend:** Whether you use the **ESP-IDF** or **Arduino** framework in ESPHome, this component leverages the native ESP32 RMT (Remote Control) peripheral directly via the IDF drivers. This ensures fire-and-forget DMA transmissions regardless of your framework choice.
- **Protocol Support:** **1-wire NRZ** (WS2812X, SK6812, WS2811) and **2-wire SPI** (APA102, SK9822)
- **Curated Timing Engine:** Because ChimeraFX uses a custom timing generator to ensure 1:1 color accuracy and zero-flicker performance, it supports a curated list of the most popular 1-wire NRZ and 2-wire SPI chipsets.

If your 1-wire NRZ chipset is not on the supported list but uses standard 800Kbps timings, `WS2812X` is often a compatible drop-in choice. If your 2-wire SPI chipset is not on the supported list, `APA102` is often a compatible drop-in choice.
