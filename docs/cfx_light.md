# ChimeraFX Light Platform

The ChimeraFX Light Platform (`cfx_light`) is a custom ESPHome light component specifically designed to be the output layer for ChimeraFX.

## Hardware & Driver Architecture

`cfx_light` is an asynchronous LED driver built directly on native ESP-IDF peripherals. In practice:

- **Framework-agnostic RMT backend.** Whether you compile with ESP-IDF or Arduino, `cfx_light` drives the ESP32 RMT peripheral directly via IDF drivers.
- **1-wire NRZ strips** (`WS2812X`, `SK6812`, `WS2811`) are driven at hardware-timed 800 kHz.
- **ESP32-S3 parallel backend.** For high-density 1-wire installs, `parallel_group` uses the S3 LCD/I80 peripheral as an 8-bit waveform bus. Multiple strips are rendered as lanes and transmitted at the same time, so refresh is bound mostly by the longest active lane instead of the sum of every strip.
- **2-wire SPI strips** (`APA102`, `SK9822`) are driven via the SPI Master peripheral at configurable speeds, typically 10-20 MHz or higher.
- **Chipset-aware defaults.** `cfx_light` selects byte order, RGBW protocol width, and RMT symbol allocation automatically based on your declared `chipset`.

If your strip uses standard 800 kbps NRZ timing but is not on the supported list, `WS2812X` is a reliable drop-in. For unlisted SPI strips, try `APA102`.

> **Node limits for V1.51:** A ChimeraFX node must use one LED transport family at a time: **RMT-only**, **SPI-only**, or **parallel-only**. Mixed RMT + SPI + parallel `cfx_light` entries are rejected at compile time. ESP32-S3 parallel output supports up to **4 lanes per group** and up to **2 parallel groups**. ESP32 Classic parallel output is available for SK6812 RGBW lanes only in this release. Each `cfx_light` can define up to **4** segments.

### Tested LED Limits

The limits below are based on physical testing for ChimeraFX V1.51 using the `Energy` effect at default values, chosen because it is one of the heavier calculation loads in the library. Treat the recommended range as the target for smooth real-world installations, the tested range as validated on the current rig, and the stress range as useful engineering data rather than a deployment target. When validating your own rig, use `LedFPS` as the visible strip-refresh metric; `RenderFPS` measures effect calculation speed.

| Platform | Transport | Recommended smooth limit | Tested stable limit | Stress result |
|:---|:---|:---|:---|:---|
| ESP32 Classic | RMT / 1-wire NRZ | 360 LEDs per GPIO for ~60 `LedFPS` | 600 LEDs per GPIO for 30+ `LedFPS` | 1100 LEDs per GPIO on 4 outputs runs, but visible refresh drops to roughly 16-18 `LedFPS` |
| ESP32 Classic | SPI / APA102-SK9822 | Up to 2000 LEDs per SPI output | 2x2000 LEDs, smooth on the test rig | Pending larger stress test |
| ESP32 Classic | Parallel / SK6812 RGBW | Engineering option for SK6812-only parallel groups | Conservative testing recommended | S3 is preferred for new parallel installs |
| ESP32-C3 | RMT / 1-wire NRZ | Experimental: 360 LEDs on 1 output at ~60 `LedFPS` | 600 LEDs on 1 output for 30+ `LedFPS` | Pending larger stress test |
| ESP32-C3 | SPI / APA102-SK9822 | Up to 2000 LEDs on 1 SPI output | 2000 virtual LEDs at ~60 `LedFPS` | Pending larger stress test |
| ESP32-S3 | RMT / 1-wire NRZ | Best for 1-2 ordinary strips | 2x600 SK6812/RGBW at roughly ~40 `LedFPS` | Use parallel for dense multi-strip installs |
| ESP32-S3 | Parallel / SK6812 RGBW | 4x600 LEDs with 4 segments per lane at 30+ `LedFPS` | 4x650 segmented SK6812, or mixed 2x600 SK6812 + 2x600 WS2812X segmented, at 30+ `LedFPS` | 4x650 whole SK6812 runs around the high-30s `LedFPS` |
| ESP32-S3 | Parallel / WS2812X RGB | Use a separate group from SK6812 | 4x650 whole WS2812X runs above the SK6812 benchmark | Larger RGB limits are possible but SK6812 remains the release benchmark |
| ESP32-S3 | SPI / APA102-SK9822 | Up to 2000 LEDs per SPI output | Pending long-strip physical retest | Pending larger stress test |

For 60 LED/m strips, the tested ESP32-S3 parallel guidance translates to roughly **40 meters total** at 30+ `LedFPS` with 4x600 LEDs, including segmented lanes.

ESP32-C3 RMT remains experimental. After C3-specific RMT tuning plus Energy optimization, one RMT output can run the reference `Energy` effect cleanly at the limits above. Two 600-LED RMT outputs still overload the C3 RMT path and are not part of the recommended configuration.

ESP32-S3 RMT still has a smaller symbol pool than ESP32 Classic, but V1.51 no longer treats that as a deployment blocker. For dense S3 layouts, use the parallel backend instead of adding more RMT outputs. The S3 is now the preferred target for large 1-wire ChimeraFX nodes.

For the timing details behind these numbers, see [Performance & Troubleshooting](Troubleshooting.md#performance-limits-and-real-fps).

## Why use `cfx_light`?

1. **Auto-injection (`all_effects`)**: By default, `cfx_light` parses and injects all ChimeraFX effects into your device at compile time. No more `!include` macros and no huge YAML effect lists. Disable this with `all_effects: false`.
2. **Chipset-aware intelligence**: Native understanding of strip timing, byte order, and RGB/RGBW protocol width.
3. **Optimized RMT symbol allocation**: For classic RMT output, `cfx_light` automatically chooses safe RMT memory bounds for the current ESP32 variant. You can manually override this with `rmt_symbols`.
4. **Automatic RGBW handling**: Selecting `SK6812` configures the 4-byte protocol and GRBW formatting automatically.
5. **Parallel S3 output**: Dense 1-wire installations can use grouped parallel lanes while keeping Home Assistant light entities and ChimeraFX segments.

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

# RMT strip with one extra physical sacrificial pixel near the controller:
light:
  - platform: cfx_light
    name: "LED Strip With Sacrificial Pixel"
    id: led_strip_signal
    pin: GPIO16
    num_leds: 60              # Visible LEDs only; physical strip has 61 LEDs
    chipset: WS2812X
    sacrificial_pixel: true   # First physical LED stays off and boosts signal

# Example config for 2-wire SPI strips (APA102, SK9822):
light:
  - platform: cfx_light
    name: "LED Strip SPI"
    id: led_strip_spi
    data_pin: GPIO23
    clock_pin: GPIO18
    spi_speed: 10MHz
    num_leds: 120
    chipset: SK9822
```

### ESP32-S3 Parallel Driver

The parallel driver is intended for high-density 1-wire layouts on ESP32-S3. Every `cfx_light` with the same `parallel_group` becomes one lane of the same parallel output. ChimeraFX renders the lanes independently, then packs them into one LCD/I80 waveform transfer.

```yaml
light:
  - platform: cfx_light
    name: "S3 Lane 1"
    id: lane_1
    pin: GPIO14
    num_leds: 600
    chipset: SK6812
    parallel_group: main
    segments:
      - id: lane_1_a
        name: "Lane 1 A"
        start: 0
        stop: 150
      - id: lane_1_b
        name: "Lane 1 B"
        start: 150
        stop: 300
      - id: lane_1_c
        name: "Lane 1 C"
        start: 300
        stop: 450
      - id: lane_1_d
        name: "Lane 1 D"
        start: 450
        stop: 600

  - platform: cfx_light
    name: "S3 Lane 2"
    id: lane_2
    pin: GPIO9
    num_leds: 600
    chipset: SK6812
    parallel_group: main
```

Parallel driver rules for V1.51:

- Use `parallel_group` on each lane that should share a parallel transmission.
- A group supports up to **4 lanes**.
- ESP32-S3 supports up to **2 parallel groups**. Use separate groups when you need both `SK6812` and `WS2812X`; chipsets cannot be mixed inside one group.
- Parallel groups cannot be mixed with normal RMT or SPI `cfx_light` entries on the same node in this release.
- Segment entities still work normally, up to **4 segments per lane**.
- `RenderFPS` remains the effect/render cadence. `LedFPS` remains the physical LED output cadence.

### Reserved GPIOs for Parallel Output

The ESP32-S3 LCD/I80 peripheral needs the LED lane pins plus two internal control GPIOs:

- `parallel_strobe_pin`: internal WR/strobe line.
- `parallel_dc_pin`: internal D/C line.

If you do not configure these pins, `cfx_light` automatically reserves two free output-capable GPIOs that are not already declared elsewhere in your YAML. The chosen pins are printed in the configuration log, for example:

```text
[C][cfx_light]:   Internal WR: GPIO38 (auto)
[C][cfx_light]:   Internal D/C: GPIO39 (auto)
```

You can override them when needed:

```yaml
light:
  - platform: cfx_light
    name: "S3 Lane 1"
    id: lane_1
    pin: GPIO14
    num_leds: 600
    chipset: SK6812
    parallel_group: main
    parallel_strobe_pin: GPIO40
    parallel_dc_pin: GPIO41
```

The same internal WR/strobe and D/C pins are shared by every lane in a parallel group. On ESP32-S3 with two parallel groups, the groups share the same internal LCD/I80 bus control pins. Do not connect LEDs to these internal control pins.

### Required Parameters

#### For 1-wire NRZ chipsets

* **name** (*string*): The name of the light in Home Assistant.
* **id** (*ID*): The ID of the light component.
* **pin** (*Pin*): The GPIO pin the data line of your LED strip is connected to.
* **chipset** (*string*): The type of LED strip you are using.
* **num_leds** (*int*): The number of visible, controllable LEDs in your strip. If `sacrificial_pixel` is enabled, do not include the sacrificial LED in this count.

#### For 2-wire SPI chipsets

* **name** (*string*): The name of the light in Home Assistant.
* **id** (*ID*): The ID of the light component.
* **data_pin** (*Pin*): The GPIO pin the data line of your LED strip is connected to.
* **clock_pin** (*Pin*): The GPIO pin the clock line of your LED strip is connected to.
* **chipset** (*string*): The type of LED strip you are using.
* **num_leds** (*int*): The total number of LEDs in your strip.

### Supported Chipsets

`cfx_light` supports both **1-wire (NRZ)** and **2-wire (SPI)** chipsets:

* **1-wire NRZ**: WS2812, WS2812B, WS2813, WS2815, SK6812 (RGBW), WS2811.
* **2-wire SPI**: APA102, SK9822.

Parallel support in V1.51:

* **ESP32-S3**: `SK6812` and `WS2812X`.
* **ESP32 Classic**: `SK6812` only.
* **WS2811**: Supported by normal RMT, not by the V1.51 parallel backend.

### Chipset Identification and Configuration

| Parameter | Chipset Option | Description |
|:---|:---|:---|
| **chipset** | `WS2812X` | Standard 3-pin RGB timing |
| | `SK6812` | 4-byte RGBW timing (GRBW order) |
| | `WS2811` | Optimized timing for WS2811 data rates |
| | `APA102` | 2-wire SPI timing |
| | `SK9822` | 2-wire SPI timing |

### Optional Parameters

* **all_effects** (*boolean*, default: `true`): Register all effects automatically. Set to `false` to manually register only selected effects and custom presets.
* **rgb_order** (*string*): The byte order of the colors. If omitted, `cfx_light` sets the standard default based on `chipset`. Options: `RGB`, `RBG`, `GRB`, `GBR`, `BGR`, `BRG`.
* **is_rgbw** (*boolean*): Explicitly declare the strip as 4-byte RGBW. If your chipset is `SK6812`, this is automatically `true`.
* **is_wrgb** (*boolean*, default: `false`): Sets the white byte position to the front of the data packet rather than the end. Required for some rare SK6812 variant clones.
* **rmt_symbols** (*int*, default: `0`): Number of RMT symbols to allocate for normal RMT output. If left at `0`, `cfx_light` dynamically allocates safe bounds for the ESP32 variant.
* **sacrificial_pixel** (*boolean*, default: `false`): RMT-only option for long data-line runs. When enabled, `cfx_light` transmits one extra black pixel before logical LED `0`.
* **parallel_group** (*string*, Optional): Enables parallel transport and assigns this light as one lane of the named group.
* **parallel_strobe_pin** (*Pin*, Optional): Internal parallel WR/strobe GPIO. Auto-selected if omitted.
* **parallel_dc_pin** (*Pin*, Optional): Internal parallel D/C GPIO. Auto-selected if omitted.
* **spi_speed** (*Frequency*, Optional): SPI clock speed for `APA102` and `SK9822` strips.
* **default_transition_length** (*Time*, default: `0s`): Standard ESPHome transition duration for solid-color mode and eligible ChimeraFX effect power transitions.
* **set_intro** (*int*, Optional): Force a global intro animation for eligible effects. Architectural effects, `Energy`, and `Chaos Theory` keep their authored intros.
* **set_outro** (*int*, Optional): Force a global outro animation for eligible effects. Architectural effects, `Energy`, and `Chaos Theory` keep their authored outros.
* **set_inout_dur** (*Time*, Optional): Sets the duration for both global intros and outros.
* **set_brightness** (*percentage*, Optional): Applies a brightness default every time the light turns on.
* **set_color** (*list[int]*, Optional): Applies a color default every time the light turns on as `[r, g, b]` or `[r, g, b, w]` using `0-100` channel percentages.
* **controls** (*boolean*, default: `true`): Automatically generate ChimeraFX control entities for this light.
* **ctrl_exclude** (*list[int]*, Optional): Exclude specific auto-generated control groups by ID. See [Controls](Controls.md).
* **segments** (*list*, Optional): Define logical sub-zones of the strip as independent light entities, up to **4** per `cfx_light`.

---

## Segments (Multi-Zone Control)

ChimeraFX supports dividing a single physical LED strip into up to **4 independent logical segments**. Each segment is exposed to Home Assistant as a separate light entity, allowing you to run different effects on different parts of the same strip simultaneously.

### Segment Configuration

```yaml
light:
  - platform: cfx_light
    name: "Main TV Strip"
    id: tv_strip
    pin: GPIO16
    num_leds: 120
    chipset: WS2812X

    segments:
      - id: tv_left
        name: "TV Left Side"
        start: 0
        stop: 40
      - id: tv_top
        name: "TV Top"
        start: 40
        stop: 80
        mirror: true
      - id: tv_right
        name: "TV Right Side"
        start: 80
        stop: 120
```

### Segment Parameters

* **id** (*ID*, Required): A unique ESPHome `id` for the segment light entity.
* **name** (*string*, Optional): The name of the light entity in Home Assistant. If omitted, the `id` is used.
* **start** (*int*, Required): The starting pixel index (inclusive).
* **stop** (*int*, Required): The stopping pixel index (exclusive).
* **mirror** (*boolean*, default: `false`): If true, calculations are reversed for this segment.
* **set_intro** (*int*, Optional): Override the global intro mode for this segment on eligible effects.
* **set_outro** (*int*, Optional): Override the global outro mode for this segment on eligible effects.
* **set_inout_dur** (*Time*, Optional): Override intro/outro duration for this segment.
* **set_brightness** (*percentage*, Optional): Applies a brightness default every time the segment turns on.
* **set_color** (*list[int]*, Optional): Defines the default color applied when the segment turns on.

### Master vs. Segment Behavior

When segments are defined:

1. The master light entity acts as global power and brightness control. It does not have its own effects. Turning off the master turns off all segments.
2. Each segment light entity has the full suite of ChimeraFX effects injected into it unless `all_effects: false` is set on the main light.
3. Each segment operates on its own runner instance, so parameters are independent.

---

## Overriding and Customizing Effects

While `all_effects: true` loads all available effects automatically, you may still want to set hardcoded defaults such as making `Aurora` default to the `Forest` palette on boot.

* **To override an effect:** Manually define an `addressable_cfx` effect with the exact same name. Your manual override replaces the auto-injected version.
* **To create multiple versions:** Keep the default effect and add a customized preset with a different name.

```yaml
light:
  - platform: cfx_light
    name: "Living Room Light"
    id: led_strip
    pin: GPIO16
    num_leds: 300
    chipset: WS2812X

    effects:
      - addressable_cfx:
          name: "Aurora"
          effect_id: 38
          set_palette: 2
          set_speed: 50

      - addressable_cfx:
          name: "Aurora Fast"
          effect_id: 38
          set_palette: 2
          set_speed: 200
```

More on effect presets in the [Effect Presets](Effect-Presets.md) page. Effect IDs can be found in the [Effects Library](Effects-Library.md) page.
