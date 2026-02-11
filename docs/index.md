

# Welcome to ChimeraFX Docs

**ChimeraFX** is a native C++ custom component for ESPHome that brings the beauty of iconic RGB LED effects to your Home Assistant setup. By integrating these effects directly into your existing ESPHome nodes, you can run premium animations and your sensors on a single MCU, eliminating the need for a separate, dedicated lighting controller.

It allows you to run complex RGB LED effects with high performance on ESP32 devices, completely avoiding the overhead and "spaghetti code" of old YAML lambda implementations.

---

### Is this for me?
**ChimeraFX** is not intended to be a full WLED replacement. You should choose the tool that best fits your hardware and requirements:
 
*   **Install [WLED](https://kno.wled.ge/) if:** You want the complete WLED experience (Segments, E1.31/DDP support, 150+ effects) and are dedicating an MCU solely to lighting.
*   **Use `ChimeraFX` if:** You want to consolidate! If you need a single ESP32 to handle sensors, relays, or switches **and** run smooth, high-quality lighting effects simultaneously, ChimeraFX is for you.

---

## Key Features

*   **Native Performance**: Optimized for ESP-IDF and dual-core ESP32s.
*   **Zero-Lambda Config**: Uses a clean `external_components` setup.
*   **Rich Effect Library**: Ports of complex effects that were previously impossible or slow in pure YAML using `addressable_lambda`.
*   **Custom Palettes**: A curated selection of palettes to choose from.
*   **Intro Effects**: Run a special effect when the light turns on.
*   **Presets**: Create your own effect configurations.
*   **Timers**: Run an effect for a specific amount of time.
*   **Full Control**: Support for Speed, Intensity, Palettes, timers and Mirroring in real time or through presets.
*   **Debug Logger**: An easy way to enable/disable the logger at runtime level.

## Quick Links

*   **[Installation Guide](Installation.md)** - Get up and running in minutes.
*   **[Controls Guide](Controls.md)** - How to set up inputs and switches.
*   **[Effect Library](Effects-Library.md)** - Browse available effects and palettes.
*   **[Troubleshooting](Troubleshooting.md)** - Fix common issues (flickering, memory).

---

# Legal Disclaimer
ChimeraFX is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED and ESPHome projects or Nabu Casa. WLED, ESPHome and Nabu Casa are trademarks of their respective owners. This work is a derivative of WLED, licensed under EUPL-1.2.

---
*Built with ❤️ by Federico Leoni (effelle)*
