# Installation & Setup

## Prerequisites

*   **Supported Hardware**:
    *   **ESP32 (Classic)**: Fully supported.
    *   **ESP32-S3**: Fully supported (and recommended for new builds).
    *   **ESP32-C3**: **NOT RECOMMENDED**. The C3 is single-core, and since the effects are computationally intensive, running them alongside WiFi on a single core can cause stuttering and stability issues.
    *   **ESP8266**: **NOT RECOMMENDED**. The old ESP8266 is stuck on the Arduino framework and simply lacks the resources to run ESPHome and ChimeraFX simultaneously. Save yourself the headache and upgrade to an ESP32. Seriously.
*   **Framework**: Both **ESP-IDF** and **Arduino** are fully supported!
    *   **ESP-IDF**: Uses the native asynchronous RMT DMA drivers.
    *   **Arduino**: Automatically relies on NeoPixelBus under the hood.

You can install the component in two ways:

## 1. Declaring the External Component

ESPHome will download the component directly from GitHub at compile-time.

Add this to your `esphome.yaml`:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    refresh: always # ⚠️ See "Versioning & Stability" below

cfx_effect: # Mandatory! Loads the component
```

## ⚠️ Versioning & Stability

**About `refresh: always`**

Setting to `always` forces ESPHome to download the absolute latest version of the code every time you compile. 

*   **Pros:** You always get the newest features and bug fixes immediately.
*   **Cons:** If I break something in the `main` branch, your build might break too!

**For Production Stability:**
Once you have a working setup, it is safer to remove `refresh: always` or pin to a specific commit hash. This ensures your lights keep working even if the repository changes.

**Note:** `ChimeraFX` controls depend on the `number`, `select`, and `switch` components. If your config doesn't use them, you must add the empty headers `number:`, `select:`, and `switch:` to your YAML to prevent compilation errors:

```yaml
number:
select: 
switch:
```
---

## Light Configuration

ChimeraFX introduces its own high-performance, asynchronous DMA LED driver called `cfx_light`. This component automatically detects your ESP32 model and allocates the optimal memory blocks for flawless, jitter-free animation.

```yaml
esp32:
  board: your_board_here
  framework:
    type: esp-idf

light:
  - platform: cfx_light
    name: "LED Strip"
    id: led_strip
    pin: GPIO16             # Remember to select the correct pin for your board
    num_leds: 60            # Number of LEDs in your strip
    chipset: WS2812X        # WS2812X, SK6812, WS2811
    all_effects: true       # Magic! Automatically registers all ChimeraFX effects.
```

For a full overview of the `cfx_light` platform, including how to set default animation parameters on first boot, please refer to the [cfx_light documentation](cfx_light.md).

### Adding Effects Manually (Without `all_effects`)

If you prefer not to use `all_effects: true`, you can manually include only the specific effects you want:

```yaml
    effects:
      - addressable_cfx:
          name: "Ocean"   # Name of the effect. Customizable.
          effect_id: 101  # ID of the effect. Required.
      - addressable_cfx:
          # Add more effects here
```

Alternatively, you can manually use the mass inclusion YAML file:

1.  **Download** `chimera_fx_effects.yaml` from the repository root.
2.  **Save** it to your ESPHome configuration folder (e.g. `/config/`).
3.  **Include** it in your light configuration:

```yaml
light:
  - platform: cfx_light
    # ... your light config ...
    effects: !include chimera_fx_effects.yaml
```

## 2. Advanced Manual Installation

If you are developing or need to modify the code locally, or simply you don't like to rely on the GitHub repository, you can manually copy the component to your ESPHome config directory:

1.  Download the `components/` folder from the repository.
2.  Place it in your ESPHome config directory (e.g., `config/components/cfx_effect`).
3.  Point your configuration to the local folder:

```yaml
external_components:
  - source: components
    components: [cfx_effect]

cfx_effect: # Mandatory! Loads the component
```

**Note:** This method requires you to manually update files when I release improvements or new effects.

## Dependencies

The component handles its own dependencies automatically. The native `cfx_light` driver utilizes the built-in ESP-IDF RMT APIs for asynchronous hardware rendering and Neopixelbus as fallback for Arduino Framework.

You don't need to install anything else manually.

---