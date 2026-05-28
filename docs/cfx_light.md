# ChimeraFX Light Platform (`cfx_light`)

The ChimeraFX Light Platform (`cfx_light`) is a custom ESPHome light component specifically designed to be the output layer for ChimeraFX. It is an asynchronous LED driver built directly on native ESP-IDF peripherals for maximum performance.

## Why use `cfx_light`?

1. **Auto-injection (`all_effects`)**: By default, `cfx_light` automatically parses and injects all ChimeraFX effects into your device at compile time. No more `!include` macros or huge YAML effect lists.
2. **Chipset-aware intelligence**: It natively understands strip timing, byte order, and RGB/RGBW protocol widths.
3. **Optimized RMT allocation**: Automatically chooses safe memory bounds for your specific ESP32 variant.
4. **Automatic RGBW handling**: Selecting `SK6812` automatically configures the 4-byte protocol and GRBW formatting.
5. **Parallel Output (ESP32-S3)**: Dense 1-wire installations can use grouped parallel lanes to run thousands of LEDs at high framerates.

---

## Quick Start Configuration

A ChimeraFX node must use one LED transport family at a time: **RMT-only**, **SPI-only**, or **parallel-only**. Mixed entries are rejected at compile time.

### 1-Wire NRZ Strips (WS281x, SK6812, WS2811)
```yaml
light:
  - platform: cfx_light
    name: "LED Strip RMT"
    id: led_strip_rmt
    pin: GPIO16
    num_leds: 120
    chipset: WS2812X
```
*Note: If your strip uses standard 800 kbps timing but isn't explicitly supported, `WS2812X` is a reliable drop-in.*

### 2-Wire SPI Strips (APA102, SK9822)
```yaml
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

---

## Configuration Variables

### Required Parameters
* **name** (*string*): The name of the light in Home Assistant.
* **id** (*ID*): The ID of the light component.
* **chipset** (*string*): The type of LED strip you are using.
  * *1-Wire:* `WS2812X`, `SK6812` (RGBW), `WS2811`.
  * *2-Wire:* `APA102`, `SK9822`.
* **num_leds** (*int*): The number of visible, controllable LEDs. *(Note: If `sacrificial_pixel` is true, do not count it here).*
* **pin** (*Pin*): The GPIO data pin (Required for 1-wire strips).
* **data_pin** / **clock_pin** (*Pin*): The GPIO data and clock pins (Required for 2-wire SPI strips).

### Optional Parameters
* **all_effects** (*boolean*, default: `true`): Register all effects automatically. Set to `false` to manually register only selected effects.
* **rgb_order** (*string*): Override byte order (`RGB`, `RBG`, `GRB`, `GBR`, `BGR`, `BRG`). Auto-set by chipset.
* **is_rgbw** (*boolean*): Explicitly declare the strip as 4-byte RGBW. Auto-set if chipset is `SK6812`.
* **is_wrgb** (*boolean*, default: `false`): Sets the white byte position to the front of the data packet. Required for some rare SK6812 variant clones.
* **sacrificial_pixel** (*boolean*, default: `false`): RMT-only option. Transmits one extra black pixel before logical LED `0` to boost data signals on long wire runs.
* **spi_speed** (*Frequency*): SPI clock speed for 2-wire strips.
* **rmt_symbols** (*int*, default: `0`): Manual RMT symbol allocation. Leave at `0` for dynamic safe allocation.
* **default_transition_length** (*Time*, default: `0s`): Standard ESPHome transition duration for solid-color mode and eligible effects.
* **controls** (*boolean*, default: `true`): Automatically generate ChimeraFX control entities for this light.
* **ctrl_exclude** (*list[int]*): Exclude specific auto-generated control groups by ID. See [Controls](Controls.md).

#### Boot Defaults
* **set_brightness** (*percentage*): Default brightness when turned on.
* **set_color** (*list[int]*): Default color when turned on as `[r, g, b]` or `[r, g, b, w]` (0-100%).
* **set_intro** / **set_outro** (*int*): Force a global intro/outro animation for eligible effects.
* **set_inout_dur** (*Time*): Duration for intros and outros.

---

## Segments (Multi-Zone Control)

You can divide a single physical LED strip into up to **4 independent logical segments**. Each segment appears in Home Assistant as a separate light entity, allowing different effects on different parts of the same strip.

* **Master Light**: Acts as global power and brightness control. Turning off the master turns off all segments.
* **Segment Lights**: Have the full suite of effects and controls and operate independently.

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
        start: 0      # Inclusive
        stop: 40      # Exclusive
      - id: tv_top
        name: "TV Top"
        start: 40
        stop: 80
        mirror: true  # Reverses calculations for this segment
      - id: tv_right
        name: "TV Right Side"
        start: 80
        stop: 120
```

---

## Overriding and Customizing Effects

While `all_effects: true` loads everything, you can define your own defaults (like forcing a specific palette or speed) by manually defining an `addressable_cfx` effect.

* **To override:** Use the exact same name as the default effect.
* **To create a preset:** Use a new name.

```yaml
    effects:
      - addressable_cfx:
          name: "Aurora" # Overrides default Aurora
          effect_id: 38
          set_palette: 2
          set_speed: 50

      - addressable_cfx:
          name: "Aurora Fast" # Creates a new custom preset
          effect_id: 38
          set_palette: 2
          set_speed: 200
```
*Effect IDs can be found in the [Effects Library](Effects-Library.md).*

---

## Advanced: ESP32-S3 Parallel Driver

For high-density 1-wire layouts on the ESP32-S3, you can use the parallel driver. Multiple strips are rendered as "lanes" and transmitted simultaneously, drastically improving refresh rates.

* **Limits:** Up to 4 lanes per group, up to 4 segments per lane, and max 2 groups per ESP32-S3.
* **Chipset groups:** `SK6812` and `WS2812X` are supported, but chipsets cannot be mixed inside a single group. Use separate groups when you need both.
* **Best fit:** Large RGBW/RGB strip layouts where several lanes should refresh together.

```yaml
light:
  - platform: cfx_light
    name: "S3 Lane 1"
    id: lane_1
    pin: GPIO14
    num_leds: 600
    chipset: SK6812
    parallel_group: main   # Assigns this to the "main" parallel group

  - platform: cfx_light
    name: "S3 Lane 2"
    id: lane_2
    pin: GPIO9
    num_leds: 600
    chipset: SK6812
    parallel_group: main   # Shares transmission with Lane 1
```

**Reserved GPIOs:** The parallel driver automatically reserves two free output GPIOs for internal control (`parallel_strobe_pin` and `parallel_dc_pin`). Check your compile log to see which pins were claimed, or manually define them if you need strict pin control. Do not connect LEDs to these internal pins!

---

## Hardware Architecture & Performance Limits

`cfx_light` is highly optimized:

* **ESP-IDF Native**: Drives the RMT peripheral directly, bypassing framework overhead.

* **ESP32-S3 Preferred**: For large arrays (>800 LEDs), the ESP32-S3 using the parallel backend is strongly recommended. 40 meters of 60 LED/m strip can run at 30+ FPS.

* **ESP32 Classic Restored**: Classic ESP32 remains a strong RMT/SPI target, especially when you need several ordinary RMT outputs.

* **ESP32-C3 Timing-sensitive**: The C3 can drive useful single-output layouts, but its single core and limited RMT symbols make heavy effects more sensitive near the upper LED limits.

Rules of thumb:

* Choose **S3 parallel** for dense 1-wire installations with 3-4 lanes or many segments.
* Choose **SPI** when the strip supports it and you want the highest LED count per output.
* Choose **Classic RMT** for reliable multi-output 1-wire nodes where the LED count is moderate.
* Treat **C3 RMT** as a compact single-output option, then test your heaviest effect before pushing the LED count.

??? abstract "Click here to view the detailed Performance Test Matrices"


    ### How to Read These Results
    * **Stress Tests, Not Averages:** The numbers below represent the suggested maximum limits of the hardware. A device is intentionally pushed to find the maximum number of LEDs you can run while maintaining a *minimum* of ~30 FPS and keeping the device stable.
    * **Higher FPS:** These are not the best performances you can get! By simply reducing the number of LEDs to normal room-scale amounts, performance will scale up smoothly to **~60 FPS**.
    * **Benchmark Details:** Every test ran for at least 20 minutes using the `Energy` effect, chosen because it represents one of the heaviest mathematical loads in the library. This guarantees real-world stability for even the most demanding setups.
    * **Segment Sizing:** In the tables below, segmented lights were tested using equal-sized segments (e.g., 4x200 or 8x175). Please note that **this is not a limitation**. You can customize your segments to any size; equal sizes were used purely to establish a consistent testing baseline.
    !!! note "Result labels"
        `PASS` means Heap WiFi >= 75kB.

        `PASS / BT WARN` means 55kB <= Heap WiFi < 75kB. Wi-Fi is still in the safe zone, but Bluetooth may not have enough free heap for reliable operation.

    * **FPS:** shown as `Render / Led`.


    ### ESP32-S3 Test Matrix

    | Setup | LEDs | Heap | FPS | Result |
    | ----- | ---- | ---- | --- | ------ |
    | Parallel, 1 lane, no segments | 820/lane, 820 total | 76kB | 30.6 / 30.6 | PASS |
    | Parallel, 1 lane, 4x200 | 800/lane, 800 total | 68kB | 31.4 / 31.4 | PASS / BT WARN |
    | RMT-GDMA, 1 lane, no segments | 620/lane, 620 total | 230kB | 59.0 / 30.9 | PASS |
    | RMT-GDMA, 1 lane, 4x135 | 540/lane, 540 total | 213kB | 53.0 / 31.4 | PASS |
    | Parallel, 2 lanes, no segments | 750/lane, 1500 total | 76kB | 33.4 / 33.5 | PASS |
    | Parallel, 2 lanes, 8x175 | 700/lane, 1400 total | 63kB | 35.8 / 35.8 | PASS / BT WARN |
    | RMT, 2 lanes, no segments | 680/lane, 1360 total | 213kB | 58.6 / 31.6 | PASS |
    | RMT, 2 lanes, 4x154 | 616/lane, 1232 total | 182kB | 43.9 / 31.7 | PASS |
    | Parallel, 3 lanes, no segments | 720/lane, 2880 total | 72kB | 34.8 / 34.8 | PASS / BT WARN |
    | Parallel, 3 lanes, 12x170 | 680/lane, 2040 total | 100kB | 31.5 / 31.4 | PASS |
    | Parallel, 4 lanes, no segments | 650/lane, 2600 total | 71kB | 38.5 / 38.6 | PASS / BT WARN |
    | Parallel, 4 lanes, 16x160 | 640/lane, 2560 total | 82kB | 33.1 / 33.1 | PASS |
    | SPI, 1 lane, no segments | 2000/lane, 2000 total | 166kB | 51.3 / 51.2 | PASS |
    | SPI, 1 lane, 4x500 | 2000/lane, 2000 total | 134kB | 45.7 / 45.7 | PASS |
    | SPI, 2 lanes, no segments | 2000/lane, 4000 total | 225kB | 58.3 / 58.8 | PASS |
    | SPI, 2 lanes, 4x500 | 2000/lane, 4000 total | 152kB | 45.2 / 45.4 | PASS |

    ### ESP32 Classic Test Matrix

    | Setup | LEDs | Heap | FPS | Result |
    | ----- | ---- | ---- | --- | ------ |
    | RMT, 1 lane, no segments | 740/lane, 740 total | 157kB | 32.3 / 32.2 | PASS |
    | RMT, 1 lane, 4x175 | 700/lane, 700 total | 145kB | 34.6 / 36.6 | PASS |
    | RMT, 2 lanes, no segments | 700/lane, 1400 total | 142kB | 34.4 / 33.8 | PASS |
    | RMT, 2 lanes, 8x165 | 660/lane, 1320 total | 117kB | 34.6 / 38.7 | PASS |
    | RMT, 3 lanes, no segments | 680/lane, 2040 total | 126kB | 35.7 / 32.5 | PASS |
    | RMT, 3 lanes, 12x160 | 640/lane, 1920 total | 89kB | 35.7 / 35.6 | PASS |
    | RMT, 4 lanes, no segments | 670/lane, 3080 total | 109kB | 36.0 / 36.6 | PASS |
    | RMT, 4 lanes, 16x135 | 540/lane, 2160 total | 64kB | 41.2 / 43.2 | PASS / BT WARN |
    | SPI, 1 lane, no segments | 2000/lane, 2000 total | 220kB | 58.4 / 58.0 | PASS |
    | SPI, 1 lane, 4x500 | 2000/lane, 2000 total | 207kB | 58.8 / 58.9 | PASS |
    | SPI, 2 lanes, no segments | 2000/lane, 4000 total | 193kB | 58.8 / 58.9 | PASS |
    | SPI, 2 lanes, 8x500 | 2000/lane, 4000 total | 168kB | 59.3 / 60.0 | PASS |

    ### ESP32-C3 Test Matrix

    | Setup | LEDs | Heap | FPS | Result |
    | ----- | ---- | ---- | --- | ------ |
    | RMT, 1 lane, no segments | 720/lane, 720 total | 177kB | 32.5 / 32.1 | PASS |
    | RMT, 1 lane, 4x165 | 660/lane, 660 total | 163kB | 33.1 / 38.1 | PASS |
    | SPI, 1 lane, no segments | 2000/lane, 2000 total | 166kB | 51.3 / 51.2 | PASS |
    | SPI, 1 lane, 4x500 | 2000/lane, 2000 total | 152kB | 45.2 / 45.4 | PASS |

    *Notes:*

    * *SPI driver is currently in BETA.*
    * *GDMA means General Direct Memory Access, preventing main CPU bog down.*
