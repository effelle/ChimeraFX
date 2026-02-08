![ChimeraFX](media/Chimera_github_wide_banner.png)

# ChimeraFX: Made for ESPHome


> **High-performance WLED effects running natively within ESPHome.**

---

## The Lore of the Chimera

A **Chimera** is a legendary creature composed of three animals. ChimeraFX merges three worlds:

- **The Lion (Power):** Raw WLED logic and effects — the proven algorithms that make lights come alive.
- **The Goat (Structure):** The reliable ESPHome framework — robust, maintainable, and Home Assistant native.
- **The Serpent (Connection):** Our custom abstraction layer — the binding force that seamlessly connects them.

---

### Is this for me?

This project is **not** a full WLED replacement and it will never be. Choose the right tool for your hardware:

*   **Install [WLED](https://kno.wled.ge/) if:** You want the full experience (Segments, E1.31, 150+ effects) or are dedicating an MCU solely to lighting.
*   **Use `wled-runner` if:** You want to consolidate! You need a single ESP32 to handle sensors, relays, or switches **AND** run smooth, high-quality lighting effects simultaneously.

### Reality Check

**Your mileage may vary.**
Visual effects are computationally expensive.
*   **Hardware:** An ESP32 is highly recommended.
*   **Resources:** Trying to run complex effects alongside heavy components (like *Bluetooth Proxy* or *Cameras*) will likely cause instability.
*   **Optimization:** This library is optimized for ESP-IDF, but hardware resources are finite. Manage your load accordingly.

This native C++ component brings advanced lighting effects to ESPHome. Unlike the old `addressable_lambda` method, this implementation runs as a proper component optimized for the **ESP-IDF** framework.

## Platform Compatibility

- **Framework:** **ESP-IDF** (Recommended) and **Arduino**
- **Chips:** **ESP32 Classic** and **ESP32-S3**
  - *ESP32-C3/S2/C6 are not supported due to single-core limitations.*

## Features

- **Native C++ Performance** — Optimized for multi-core ESP32s
- **Clean YAML Syntax** — Simple `addressable_cfx` configuration
- **Many Built-in Palettes** — Easily customizable
- **Smooth Transitions** — Professional-grade animations
- **Dynamic Controls** — Speed, intensity, palette, and mirror direction
- **Intro Animations** — Wipe, Fade, Center, and Glitter effects on turn-on

---

## Quick Start

See [QUICKSTART.md](QUICKSTART.md) for installation and configuration.

Add to your ESPHome YAML:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    components: [cfx_effect]

cfx_effect:
```

---

## Available Effects

| ID | Name | Description |
|----|------|-------------|
| `0` | Static | Solid color with palette support |
| `2` | Breathe | Apple-style standby breathing LED |
| `3` | Wipe | Primary/Secondary color wipe |
| `6` | Sweep | Ping-pong wipe animation |
| `8` | Colorloop | Solid color cycling through palette |
| `9` | Rainbow | Per-pixel rainbow with density control |
| `18` | Dissolve | Random pixel color transitions |
| `38` | Aurora | Northern lights animation |
| `53` | Fire Dual | Two flames meeting in the center |
| `63` | Colorwaves | Rainbow flag with breathing motion |
| `64` | Juggle | Eight bouncing dots with trails |
| `66` | Fire | Realistic fire simulation |
| `74` | Colortwinkles | Magical fairy-dust twinkles |
| `76` | Meteor | Meteor with random decay trail |
| `91` | Bouncing Balls | Real gravity physics |
| `97` | Plasma | Smooth plasma animation |
| `101` | Pacifica | Gentle ocean waves |
| `104` | Sunrise | Gradual sunrise/sunset simulation |
| `105` | Phased | Sine wave interference pattern |
| `110` | Flow | Smooth color zones animation |

> [!TIP]
> **Why aren't all WLED effects here yet?**  
> Bringing WLED effects to `ChimeraFX` takes time. Each effect is manually rewritten trying to "squeeze" every bit of performance out of the hardware with minimal resources cost. My goal is to maintain the original look while ensuring it runs perfectly on ESPHome. Thank you for your patience as I port them incrementally! (No ETA).
---

## Available Palettes

Aurora, Forest, Halloween, Rainbow, Fire, Sunset, Ice, Party, Lava, Pastel, Ocean, HeatColors, Sakura, Rivendell, Cyberpunk, OrangeTeal, Christmas, RedBlue, Matrix, SunnyGold, Fairy, Twilight, Solid

---

## Credits

- **[WLED](https://github.com/wled/WLED)** by Aircoookie — Original effect algorithms
- **[ESPHome](https://github.com/esphome/esphome)** by ESPHome — Framework integration
- **[FastLED](https://github.com/FastLED/FastLED)** by FastLED — Color handling and math utilities
- **[NeoPixelBus](https://github.com/Makuna/NeoPixelBus)** by Makuna — NeoPixel driver for ESP32
- **[ESP-IDF](https://github.com/espressif/esp-idf)** by Espressif — ESP32 framework
- **[Arduino](https://github.com/arduino/Arduino)** by Arduino — ESP32 framework

---

## License

**EUPL-1.2** (European Union Public Licence) — See [LICENSE](LICENSE) for details.

---

## Legal Disclaimer

**ChimeraFX** is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED and ESPHome projects or Nabu Casa. WLED, ESPHome and Nabu Casa are trademarks of their respective owners. 
This work is a derivative of WLED, licensed under EUPL-1.2.
