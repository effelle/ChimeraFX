# Installation & Setup

## Prerequisites

*   **ESPHome Version**: The minimum version to run ChimeraFX for ESPHome is **2026.3.0**
*   **Supported Hardware**:
      *   **ESP32 (Classic)**: Fully supported. V1.41 supports up to 4 RMT strips or 2 SPI strips per node.
      *   **ESP32-S3**: Fully supported and recommended for new builds. V1.41 supports up to 2 RMT strips or 2 SPI strips per node.
      *   **ESP32-C3**: **NOT RECOMMENDED**. The C3 is single-core, and since the effects are computationally intensive, running them alongside Wi-Fi on a single core can cause stuttering and stability issues. V1.41 treats C3 RMT as experimental: one tuned RMT output reached ~57-59 `LedFPS` at 360 LEDs and ~30-33 `LedFPS` at 600 LEDs with `Energy`. C3 supports either 1 experimental RMT strip or 1 experimental SPI strip; 2000 virtual SPI LEDs held ~58-60 `LedFPS`, but long physical SPI strips are still pending validation.
      *   **Other ESP32 variants** (S2, P4, C6, H2, etc.): Untested. Dual-core variants are expected to work; single-core variants are not recommended for the same reasons as the C3. Community reports welcome.
      *   **ESP8266 (and variants)**: **NOT SUPPORTED**. Although ESPHome can target the ESP8266, it lacks the FPU and RAM required by the ChimeraFX rendering engine — it will not compile. Please upgrade to an ESP32. Seriously.
*   **Framework**: Both **ESP-IDF** and **Arduino** are fully supported.

---

## Before You Flash

ChimeraFX does not need special ESPHome knowledge, but addressable LEDs are picky about wiring and power. Before your first compile and flash:

*   Connect the LED strip ground and ESP32 ground together.
*   Use a power supply sized for your LED count and expected brightness.
*   Use a level shifter for serious 5V strip builds; ESP32 data pins are 3.3V.
*   Keep the data wire from the ESP32 to the first LED short.
*   Plan power injection for longer strips instead of feeding everything from one end.

For flicker, random colors, resets, SPI inrush, and memory pressure, see [Performance & Troubleshooting](Troubleshooting.md).

For ChimeraFX V1.41, keep each node **RMT-only** or **SPI-only**. Mixed RMT + SPI `cfx_light` entries are blocked at compile time; use separate ESP32 controllers if your installation needs both transports.

---

You can install the component in two ways:

## 1. Declaring the External Component

ESPHome will download the component directly from GitHub at compile-time.

Add this to your `esphome.yaml`:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    refresh: always
```

⚠️ **About `refresh: always`**

Setting to `always` forces ESPHome to download the absolute latest version of the code every time you compile. 

*   **Pros:** You always get the newest features and bug fixes immediately.
*   **Cons:** If something breaks in `main` branch, your build might break too! 

**For Production Stability:**
Once you have a working setup, it is safer to remove `refresh: always` or pin to a specific commit hash. This ensures your lights keep working even if the repository changes.

## 2. Advanced Manual Installation

If you are developing, need to modify the code locally, or prefer not to rely on the GitHub repository, you can manually copy the component to your ESPHome config directory:

1.  Download the `components/` folder from the repository.
2.  Place it in your ESPHome config directory (e.g., `config/components/cfx_effect`).
3.  Point your configuration to the local folder:

```yaml
external_components:
  - source: components
    components: [cfx_effect]
```

---
## Complete Minimal Example

If you already have a working ESPHome device YAML, you only need the `external_components` block and the `light` block shown below. This complete example is here as a known-good starting point for a new ESP32 node:

```yaml
esphome:
  name: chimera_led_demo
  friendly_name: Chimera LED Demo

esp32:
  board: esp32dev
  framework:
    type: esp-idf

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

logger:

api:

ota:
  - platform: esphome

external_components:
  - source: github://effelle/ChimeraFX@main
    refresh: always

light:
  - platform: cfx_light
    name: "LED Strip"
    id: led_strip
    pin: GPIO16
    num_leds: 60
    chipset: WS2812X
```

After the first successful compile, consider removing `refresh: always` or pinning to a known working commit for a more stable production device.

---

## Quick Light Configuration

ChimeraFX introduces its own high-performance, asynchronous DMA LED driver called `cfx_light`. This component automatically detects your ESP32 model and allocates the optimal memory blocks for flawless, jitter-free animation.

Example config for 1-wire NRZ strips (WS2812X, SK6812, WS2811):
```yaml
light:
  - platform: cfx_light
    name: "LED Strip"
    id: led_strip
    pin: GPIO16             # Remember to select the correct pin for your board
    num_leds: 60            # Number of LEDs in your strip
    chipset: WS2812X        # WS2812X, SK6812, WS2811, etc.
```

Example config for 2-wire SPI strips (APA102, SK9822):
```yaml
light:
  - platform: cfx_light
    name: "LED Strip"
    id: led_strip
    data_pin: GPIO23       # Data pin required for SPI strips
    clock_pin: GPIO18      # Clock pin required for SPI strips
    spi_speed: 15MHz       # SPI speed for SPI strips
    num_leds: 60           # Number of LEDs in your strip
    chipset: SK9822        # APA102, SK9822
```

For a full overview of the `cfx_light` platform, please refer to the [cfx_light documentation](cfx_light.md).

### Adding Effects Manually (Without `all_effects`)

If you prefer not to use `all_effects: true`, or want to create custom [presets](Effect-Presets.md) you can manually include specific effects:

```yaml
    effects:
      - addressable_cfx:
          name: "Kaleidos" # The display name of the effect (Required, customizable)
          effect_id: 155   # The ChimeraFX effect ID (Required)

      - addressable_cfx:
          # Add more effects here
```
The necessary YAML to declare each single effect is available on the [Effects-Library.md](Effects-Library.md) page.
---

## Dependencies

The component handles its own dependencies automatically. You don't need to install anything else manually.

---
