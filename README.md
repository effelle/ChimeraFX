![ChimeraFX](media/ChimeraFX_new_banner_github.png)

# ChimeraFX: Advanced LED Effects Engine for ESPHome

*A high-performance C++ rendering engine bringing premium fluid simulations, architectural transitions, and optimized WLED classics natively into your ESPHome and Home Assistant ecosystem.*

### Key Features
* **Zero YAML Overhead:** Pure C++ implementation for maximum ESP32 frame rates.
* **ChimeraFX Originals:** Exclusive physics-based fluid and noise algorithms.
* **Architectural Transitions:** Premium intro/outro sweeps for monochromatic setups.
* **Smart Random:** Procedural color theory engine for aesthetic, non-repeating palettes.
* **100% Home Assistant Native:** Instantly exposes speed, intensity, and palette controls.

### Getting Started

**[Read the Official Documentation](https://effelle.github.io/ChimeraFX/)**

The documentation includes everything you need:
- [Installation & Quick Start](https://effelle.github.io/ChimeraFX/Installation/)
- [Visual Effects Gallery](https://effelle.github.io/ChimeraFX/Effects-Library/)
- [YAML Configuration & Presets](https://effelle.github.io/ChimeraFX/Effect-Presets/)

---

### Beyond WLED (Our Philosophy)

ChimeraFX stands as a distinct, high-performance lighting engine. It bridges the gap between hand-tuned open-source favorites and **exclusive original algorithms** designed specifically for architectural integration and fluid motion.

Every effect in this library is selected for visual fidelity and rewritten for maximum efficiency within the ESPHome environment. To ensure the device remains fast, responsive, and stable, unoptimized 2D matrix effects or animations requiring multi-color segment layering are intentionally excluded in favor of streamlined, high-speed rendering.

## Features

- **Native C++ Performance** — Optimized for multi-core ESP32s
- **Clean YAML Syntax** — Automatic loading of all effects or simple `addressable_cfx` configuration 
- **Many Built-in Palettes** — Easily customizable
- **Smooth Transitions** — Professional-grade animations
- **Dynamic Controls** — Speed, intensity, palette, and mirror direction for most of the effects
- **Intelligent Autotune** — Automatically snaps to optimal parameter defaults, but instantly yields manual control when you touch a slider.
- **Intro and Outro Animations** — Wipe, Fade, Center, Glitter and more effects on turn-on and turn-off
- **Timer** — Turn off after a specified amount of time
- **Presets** — Save and restore effect configurations
- **Debug Logger** — Turn on/off a logger at runtime level for the component

### Is this for me?

This project is **not** a full WLED replacement and it will never be. Choose the right tool for your hardware:

*   **Install [WLED](https://kno.wled.ge/) if:** You want the full experience (Support for SPI light strips, E1.31, 150+ effects, sound reactivity, etc.) or are dedicating an MCU solely to lighting.
*   **Use `ChimeraFX` if:** You want to consolidate! You need a single ESP32 to handle sensors, relays, or switches **AND** run smooth, high-quality lighting effects simultaneously.

## Platform Compatibility

- **Framework:** **ESP-IDF**  and **Arduino** (via RMT DMA)
- **Chips:** **ESP32 Classic** and **ESP32-S3**
  - *ESP32-C3/S2/C6 and ESP8266 are not officially supported due to single-core limitations.*
- **Protocol Support:** **1-wire NRZ** only (WS2812X, SK6812, WS2811)
  - *2-wire SPI strips (APA102, WS2801, SK9822, etc.) are **not supported**.*

### Reality Check

**Your mileage may vary.**
Visual effects are computationally expensive.
*   **Hardware:** An ESP32 is highly recommended due to its dual-core architecture.
*   **Resources:** Trying to run complex effects alongside heavy components (like *Bluetooth Proxy* or *Cameras*) will likely cause instability.
*   **Optimization:** This library is optimized for ESP-IDF, but hardware resources are finite. Manage your load accordingly.

This native C++ component brings advanced lighting effects to ESPHome. Unlike the old `addressable_lambda` method, this implementation runs as a proper component optimized for the **ESP-IDF** framework.

## Credits

- **[WLED](https://github.com/wled/WLED)** by Aircoookie — Original effect algorithms
- **[ESPHome](https://github.com/esphome/esphome)** by ESPHome — Framework integration
- **[FastLED](https://github.com/FastLED/FastLED)** by FastLED — Color handling and math utilities

---

## License

**EUPL-1.2** (European Union Public Licence) — See [LICENSE](LICENSE) for details.

---

## Support the Project

If you find **ChimeraFX** useful and would like to support the time and effort put into porting these effects, donations are never expected but always greatly appreciated!

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-ffdd00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black)](https://www.buymeacoffee.com/effelle)

---

## Legal Disclaimer

**ChimeraFX** is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED project, the ESPHome project, or Nabu Casa. WLED, ESPHome, and Nabu Casa are trademarks of their respective owners.

This work incorporates code and logic derived from WLED and is licensed under the **European Union Public Licence v1.2 (EUPL-1.2)**.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.