# Installation & Setup

Retrieving the code and getting it running on your ESP32.

## Prerequisites

*   **Supported Hardware**:
    *   **ESP32 (Classic)**: Fully supported.
    *   **ESP32-S3**: Fully supported (and recommended for new builds).
    *   **ESP32-C3**: **NOT RECOMMENDED**. The C3 is single-core, and since the effects are computationally intensive, running them alongside WiFi on a single core can cause stuttering and stability issues.
    *   **ESP8266**: **NOT RECOMMENDED**. The old ESP8266 is stuck on the Arduino framework and simply lacks the resources to run ESPHome and ChimeraFX simultaneously. Save yourself the headache and upgrade to an ESP32. Seriously.
*   **Frameworks** (both supported):
    *   **ESP-IDF** (with esp32_rmt_led_strip) — slightly better performance
    *   **Arduino** (with NeoPixelBus) — simpler setup

## Method 1: External Component (Recommended)

This is the way! And also the easiest way. ESPHome will download the component directly from GitHub at compile-time.

Add this to your `esphome.yaml`:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    components: [cfx_effect]
    refresh: always # ⚠️ See "Versioning & Stability" below

cfx_effect: # Mandatory! Loads the component
```

**Note:** `ChimeraFX` controls depend on the `number`, `select`, and `switch` components. If your config doesn't use them, you must add the empty headers `number:`, `select:`, and `switch:` to your YAML to prevent compilation errors:

```yaml
number:
select: 
switch: 
```

---

## Framework-Specific Light Configuration

### Option A: ESP-IDF + RMT Strip (Better Performance)

```yaml
esp32:
  board: your_board_here
  framework:
    type: esp-idf

light:
  - platform: esp32_rmt_led_strip
    rgb_order: GRB
    pin: GPIO16 # Remeber to select the correct pin for your board
    num_leds: 60
    chipset: ws2812
    max_refresh_rate: 24ms  # Recommended for clean timing
    name: "LED Strip"
    id: led_strip
    effects:
      - addressable_cfx:
          name: "Ocean"
          effect_id: 101
      - addressable_cfx:
          # Add more effects here
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
    variant: WS2812X
    pin: GPIO16 # Remeber to select the correct pin for your board
    num_leds: 60
    name: "LED Strip"
    effects:
      - addressable_cfx:
          name: "Ocean"
          effect_id: 101
      - addressable_cfx:
          # Add more effects here

```

> TIP: ESP-IDF typically provides 5-10% better FPS on longer strips (200+ LEDs).

## ⚠️ Versioning & Stability

**About `refresh: always`**

Setting to `always` forces ESPHome to download the absolute latest version of the code every time you compile. 

*   **Pros:** You always get the newest features and bug fixes immediately.
*   **Cons:** If I break something in the `main` branch, your build might break too!

**For Production Stability:**
Once you have a working setup, it is safer to remove `refresh: always` or pin to a specific commit hash. This ensures your lights keep working even if the repository changes.

## Method 2: Manual Copy (Advanced)

If you are developing or need to modify the code locally:

1.  Download the `components/` folder from the repository.
2.  Place it in your ESPHome config directory (e.g., `config/components/cfx_effect`).
3.  Point your configuration to the local folder:

```yaml
external_components:
  - source: components
    components: [cfx_effect]
```

**Note:** This method requires you to manually update files when I release improvements or new effects.

## Dependencies

The component handles its own dependencies automatically:
- **Arduino**: Uses NeoPixelBus (auto-included)
- **ESP-IDF**: Uses native RMT driver (built-in)

You don't need to install anything else manually.
