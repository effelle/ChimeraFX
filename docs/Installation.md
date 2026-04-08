# Installation & Setup

## Prerequisites

*   **ESPHome Version**: The minimal version to run ChimeraFX for ESPHome is **2026.3.0**
*   **Supported Hardware**:
      *   **ESP32 (Classic)**: Fully supported and can control up to 4 strips.
      *   **ESP32-S3**: Fully supported (and recommended for new builds). Up to 4 strips.
      *   **ESP32-C3**: **NOT RECOMMENDED**. The C3 is single-core, and since the effects are computationally intensive, running them alongside WiFi on a single core can cause stuttering and stability issues. Up to 2 strips.
      *   **Other ESP32 variants** (S2, P4, C6, H2, etc.): Untested. Dual-core variants are expected to work; single-core variants are not recommended for the same reasons as the C3. Community reports welcome.
      *   **ESP8266 (and variants)**: **NOT SUPPORTED**. Although ESPHome can target the ESP8266, it lacks the FPU and RAM required by the ChimeraFX rendering engine — it will not compile. Please upgrade to an ESP32. Seriously.
*   **Framework**: Both **ESP-IDF** and **Arduino** are fully supported!

---

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

---

## Light Configuration

ChimeraFX introduces its own high-performance, asynchronous DMA LED driver called `cfx_light`. This component automatically detects your ESP32 model and allocates the optimal memory blocks for flawless, jitter-free animation.

```yaml
esp32:
  board: your_board_here
  framework:
    type: esp-idf
```

Example config for 1-wire NRZ strips (WS2812X, SK6812, WS2811):
```yaml
light:
  - platform: cfx_light
    name: "LED Strip"
    id: led_strip
    pin: GPIO16             # Remember to select the correct pin for your board
    num_leds: 60            # Number of LEDs in your strip
    chipset: WS2812X        # WS2812X, SK6812, WS2811, etc.
    all_effects: true       # Magic! Automatically registers all ChimeraFX effects.
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
    all_effects: true      # Magic! Automatically registers all ChimeraFX effects.
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

---

#### Regardless of the installation method you chose, you are now ready to [configure your controls](Controls.md).

---

## Dependencies

The component handles its own dependencies automatically. You don't need to install anything else manually.

---