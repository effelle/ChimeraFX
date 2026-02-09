# ChimeraFX for ESPHome - Quick Start

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

#### Option A: ESP-IDF + RMT Strip (Better Performance)
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

#### Option B: Arduino + NeoPixelBus (Simpler)
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

### Step 3: Add Controls (Recommended)
Add the `cfx_control` component to automatically generate all controls (Speed, Intensity, Palette, Mirror, Intro, Timer) and link them to your light.

```yaml
cfx_effect:
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
  - platform: esp32_rmt_led_strip  # or neopixelbus
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

You can optionally define preset values (`set_speed`, `set_intensity`, `set_palette`, `set_mirror`) that automatically push to the controls when an effect is activated:

```yaml
effects:
  # Pacifica: slow, high intensity, uses Pacifica palette (index 11)
  - addressable_cfx:
      name: "Pacifica"
      effect_id: 101
      set_speed: 50
      set_intensity: 200
      set_palette: 11
  
  # Aurora: medium speed, mirrors direction (left/right)
  - addressable_cfx:
      name: "Aurora"
      effect_id: 38
      set_speed: 128
      set_mirror: true
  
  # Bouncing Balls: high speed (gravity), low intensity (fewer balls), Default Palette (Solid)
  - addressable_cfx:
      name: "Bouncing Balls"
      effect_id: 91
      set_speed: 128
      set_intensity: 140
      set_palette: 0
```

**Behavior:**
- If `set_speed: 200` is specified → the speed slider jumps to 200 when the effect starts
- If not specified → the slider stays at its current value (unchanged)
- Palette uses numeric index (see `docs/Configuration.md` for list)

---

## Troubleshooting

**"addressable_cfx not found"**  
Make sure you have added the `external_components` block pointing to `github://effelle/ChimeraFX@main`.

**"Flickering LEDs"**  
If using ESP-IDF, you must ensure your `rmt_symbols` are set correctly for your chip type:
- **Classic ESP32**: 512
- **ESP32-S3**: 192

Also, set `use_psram: false` in your light config. PSRAM represents external RAM which is significantly slower than the ESP32's internal SRAM. The RMT (Remote Control) peripheral requires high-speed data access to generate accurate timing for addressable LEDs. Using PSRAM can cause timing jitter, resulting in flickering or data corruption.

---

## Debugging & Diagnostics

Must redo

### Multi-Strip Support

This component fully supports **multiple LED strips** on the same ESP32. Each strip has its own independent effect engine - you can run Aurora on one strip and Rainbow on another simultaneously. The limit is the amount of resources available on your ESP32.

## Visual Comparison

![Testing Setup](/C:/Users/effel/.gemini/antigravity/brain/cd5b621c-3b7b-4fef-8846-f9c04b04dc3e/uploaded_image_1767052210112.jpg)

The porting process wasn't a 1:1 conversion due to fundamental architectural differences between WLED and ESPHome. Instead, I used a trial-and-error approach, iteratively tuning the effects side-by-side until we achieved a (very) similar visual result to WLED.

**Enjoy your lights!**
