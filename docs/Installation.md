# Installation & Setup

## Prerequisites

*   **Supported Hardware**:
    *   **ESP32 (Classic)**: Fully supported.
    *   **ESP32-S3**: Fully supported (and recommended for new builds).
    *   **ESP32-C3**: **NOT RECOMMENDED**. The C3 is single-core, and since the effects are computationally intensive, running them alongside WiFi on a single core can cause stuttering and stability issues.
    *   **ESP8266**: **NOT RECOMMENDED**. The old ESP8266 is stuck on the Arduino framework and simply lacks the resources to run ESPHome and ChimeraFX simultaneously. Save yourself the headache and upgrade to an ESP32. Seriously.
*   **Frameworks** (both supported):
    *   **ESP-IDF** (with esp32_rmt_led_strip) — slightly better performance
    *   **Arduino** (with NeoPixelBus) — simpler setup

You can install the component in two ways:

## 1. Declaring the External Component

ESPHome will download the component directly from GitHub at compile-time.

Add this to your `esphome.yaml`:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    components: [cfx_effect]
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

## Framework-Specific Light Configuration

### Option A: ESP-IDF + RMT Strip (Better Performance for longer strips)

```yaml
esp32:
  board: your_board_here
  framework:
    type: esp-idf

light:
  - platform: esp32_rmt_led_strip
    rgb_order: GRB
    pin: GPIO16             # Remember to select the correct pin for your board
    num_leds: 60            # Number of LEDs in your strip
    chipset: ws2812         # Set your correct chipset 
    max_refresh_rate: 24ms  # Recommended for clean timing
    name: "LED Strip"       # Name of your light
    id: led_strip           # ID of your light
```

### Option B: Arduino + NeoPixelBus (Simpler)

```yaml
esp32:
  board: your_board_here
  framework:
    type: arduino

light:
  - platform: neopixelbus
    type: GRB
    variant: WS2812X      # Set your correct chipset 
    pin: GPIO16           # Remember to select the correct pin for your board
    num_leds: 60          # Number of LEDs in your strip
    name: "LED Strip"     # Name of your light
    id: led_strip         # ID of your light
```
### Adding the effects

You can now add the effects you like to your light component. The `effect_id` is the ID of the effect you want to use. You can find the list of effects in the [Effects](Effects-Library.md) section. 

```yaml
    effects:
      - addressable_cfx:
          name: "Ocean"   # Name of the effect. Customizable.
          effect_id: 101  # ID of the effect. Required.
      - addressable_cfx:
          # Add more effects here
```
### Mass Inclusion

Adding every single effect to your device configuration can be a bit of a pain, and you will likely end up with a very long file. To make it easier, you can load all 20+ effects at once using the provided `chimera_fx_effects.yaml` file.

1.  **Download** `chimera_fx_effects.yaml` from the repository root.
2.  **Save** it to your ESPHome configuration folder (e.g. `/config/`).
3.  **Include** it in your light configuration:

```yaml
light:
  - platform: esp32_rmt_led_strip # Or Neopixelbus for Arduino framework
    # ... your light config ...
    effects: !include chimera_fx_effects.yaml
```

**Note:** Every time a new effect is added, you will need to download the updated `chimera_fx_effects.yaml` file and replace the old one. A small price to pay for convenience.

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

The component handles its own dependencies automatically:

- **Arduino**: Uses NeoPixelBus (auto-included)
- **ESP-IDF**: Uses native RMT driver (built-in)

You don't need to install anything else manually.

---