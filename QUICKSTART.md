# WLED Effects for ESPHome - Quick Start

## What You Get
Port WLED's beautiful effects to ESPHome with full speed and palette control via Home Assistant.

---

## Installation (2 Steps)

### Step 1: Add Component
Add the repository to your `external_components` in your ESPHome config:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    components: [cfx_effect]
    refresh: always

cfx_effect:
```

### Step 2: Configure Your Light
Add the effect to your addressable light configuration. Both Arduino and ESP-IDF frameworks are supported.

#### Option A: Arduino + NeoPixelBus (Simpler)
```yaml
esp32:
  board: your_board_here
  framework:
    type: arduino

light:
  - platform: neopixelbus
    type: GRB
    variant: WS2812X
    pin: GPIO16
    num_leds: 60
    name: "LED Strip"
    effects:
      - addressable_cfx:
          name: "Aurora"
          effect_id: 38
```

#### Option B: ESP-IDF + RMT Strip (Better Performance)
```yaml
esp32:
  board: your_board_here
  framework:
    type: esp-idf

light:
  - platform: esp32_rmt_led_strip
    rgb_order: GRB
    pin: GPIO16
    num_leds: 60
    chipset: ws2812
    max_refresh_rate: 24ms
    name: "LED Strip"
    effects:
      - addressable_cfx:
          name: "Aurora"
          effect_id: 38
```

#### Standard Setup (Recommended)
Add the `cfx_control` component to automatically generate all controls (Speed, Intensity, Palette, Mirror, Intro, Timer) and link them to your light.

```yaml
wled_effect:
  cfx_control:
    - id: my_cfx_controller
      name: "Living Room WLED"
      light_id: led_strip
      # Optional: Exclude controls you don't need
      # exclude: [timer, intro]  # or IDs [6, 5]
```

> [!IMPORTANT]
> **Component Dependencies:** The `cfx_control` component automatically generates entities using ESPHome's standard `number`, `select`, and `switch` components. You do **not** need to manually define them.

### Generated Controls
The component creates the following entities based on your controller ID (e.g., `my_cfx_controller`):
- `my_cfx_controller_speed` (Number)
- `my_cfx_controller_intensity` (Number)
- `my_cfx_controller_palette` (Select)
- `my_cfx_controller_mirror` (Switch)
- `my_cfx_controller_intro` (Select)
- `my_cfx_controller_intro_dur` (Number)
- `my_cfx_controller_timer` (Number)

### Exclusion Options
You can exclude specific controls by adding their ID to the `exclude` list (e.g., `exclude: [5, 6]`).

| ID | Control |
|:---|:---|
| `1` | Speed |
| `2` | Intensity |
| `3` | Palette |
| `4` | Mirror |
| `5` | Intro Effects (Intro & Duration) |
| `6` | Timer |

That's it! The effects will automatically find this controller.

#### Linked Light Config
Ensure your light has the matching ID (`led_strip`):

```yaml
light:
  - platform: neopixelbus  # or esp32_rmt_led_strip
    id: led_strip
    name: "LED Strip"
    # ... pin settings ...
    effects:
      - addressable_cfx:
          name: "Aurora"
          effect_id: 38
      - addressable_cfx:
          name: "Pacifica"
          effect_id: 101
```

> [!NOTE]
> **Unique Names Required:** ESPHome requires every effect to have a unique `name`. If you define multiple WLED effects, you **must** provide a `name` for each one (e.g., "Aurora", "Pacifica") to avoid build errors.

---

## Per-Effect Presets

You can optionally define preset values (`set_speed`, `set_intensity`, `set_palette`, `set_mirror`) that automatically push to the linked controls when an effect is activated:

```yaml
effects:
  # Pacifica: slow, high intensity, uses Pacifica palette (index 10)
  - addressable_cfx:
      name: "Pacifica"
      effect_id: 101
      speed: wled_speed
      intensity: wled_intensity
      palette: wled_palette
      set_speed: 50
      set_intensity: 200
      set_palette: 10
  
  # Aurora: medium speed, no preset for intensity (keeps current value)
  - addressable_cfx:
      name: "Aurora"
      effect_id: 38
      speed: wled_speed
      intensity: wled_intensity
      reverse: wled_reverse
      set_speed: 128
      set_mirror: true
  
  # Bouncing Balls: high speed (gravity), low intensity (fewer balls), Default Palette (Solid)
  - addressable_cfx:
      name: "Bouncing Balls"
      effect_id: 91
      speed: wled_speed
      intensity: wled_intensity
      palette: wled_palette
      set_speed: 128
      set_intensity: 140
      set_palette: 0
```

**Behavior:**
- If `set_speed: 200` is specified → the speed slider jumps to 200 when the effect starts
- If not specified → the slider stays at its current value (unchanged)
- Palette uses numeric index (0-10 based on your options list)

---

## Troubleshooting

**"addressable_cfx not found"**  
Make sure you have added the `external_components` block pointing to `github://effelle/ChimeraFX@main`.

**"Flickering LEDs"**  
If using ESP-IDF, you must ensure your `rmt_symbols` are set correctly. User testing confirms that **320 symbols** is often the minimum required for stability with complex effects, regardless of strip length.

- **Classic ESP32**: Try **320** symbols (Max is 512 per channel, but standard config is often lower)
- **ESP32-S3**: Try **192** (Hardware limit is tighter)

**Note:** If you run multiple strips, you may hit the total RMT memory limit (512 total on Classic). If you experience flickering and can't increase symbols further, consider switching to `neopixelbus` (Arduino framework) or splitting strips across multiple ESPs.

Also, set `use_psram: false` in your light config. PSRAM is significantly slower than internal SRAM. The RMT peripheral requires high-speed data access; using PSRAM can cause timing jitter, resulting in flickering or data corruption.

---

## Debugging & Diagnostics

The component includes built-in diagnostics for troubleshooting performance issues. These are disabled by default to keep logs clean.

### Enabling FPS & Heap Monitoring

Add to your YAML to show FPS, heap usage, and max allocatable block:

```yaml
logger:
  level: DEBUG
  logs:
    wled_diag: DEBUG
```

This will log once per second per strip:
```
[D][wled_diag]: FPS:56 Mode:38 Heap:220336/220336 MaxBlk:110592
```

### Additional Heap Monitoring (Optional)

For more detailed heap tracking, add this interval:

```yaml
interval:
  - interval: 2s
    then:
      - lambda: |-
          ESP_LOGI("diag", "Heap: %u/%u | MaxBlock: %u",
            esp_get_free_internal_heap_size(),
            esp_get_free_heap_size(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
```

### Multi-Strip Support

This component fully supports **multiple LED strips** on the same ESP32. Each strip has its own independent effect runner - you can run Aurora on one strip and Rainbow on another simultaneously.

## Visual Comparison

![Testing Setup](/C:/Users/effel/.gemini/antigravity/brain/cd5b621c-3b7b-4fef-8846-f9c04b04dc3e/uploaded_image_1767052210112.jpg)

The porting process wasn't a 1:1 conversion due to fundamental architectural differences between WLED and ESPHome. Instead, we used a trial-and-error approach, iteratively tuning the effects side-by-side until we achieved a (very) similar visual result to WLED.

**Enjoy your lights! ✨**
