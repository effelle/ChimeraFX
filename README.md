![ChimeraFX](media/ChimeraFX_new_banner_github.png)

# ChimeraFX: Advanced LED Effects Engine for ESPHome

*A high-performance C++ rendering engine bringing premium fluid simulations, architectural transitions, and optimized WLED classics natively into your ESPHome and Home Assistant ecosystem.*

Unlike the old `addressable_lambda` method, this implementation runs as a proper native component optimized for the ESP-IDF framework, freeing up your YAML and maximizing frame rates.

### Getting Started

**[Read the Official Documentation](https://effelle.github.io/ChimeraFX/)**

The documentation includes everything you need:
- [Installation & Quick Start](https://effelle.github.io/ChimeraFX/Installation/)
- [Visual Effects Gallery](https://effelle.github.io/ChimeraFX/Effects-Library/)

---

### Key Features

* **Zero YAML Overhead** — Pure C++ implementation for maximum ESP32 frame rates.
* **Dual Framework Support** — Runs as a proper native component under both ESP-IDF and Arduino.
* **ChimeraFX Originals** — Exclusive physics-based fluid and noise algorithms.
* **Architectural Transitions** — Premium intro/outro sweeps for monochromatic setups.
* **Intelligent Autotune** — Automatically snaps to optimal parameters, but instantly yields to manual slider adjustments.
* **Smart Random Palette** — Procedural color theory engine for aesthetic, non-repeating palettes.
* **100% Home Assistant Native** — Instantly exposes speed, intensity, palette, and other controls.
* **Built-in Toolkit** — Supports Presets, Timers, and a runtime Debug Logger.

---

### Beyond WLED (The Philosophy)

ChimeraFX stands as a distinct, high-performance lighting engine. It bridges the gap between hand-tuned open-source favorites and **exclusive original algorithms** designed specifically for architectural integration and fluid motion.

Every effect in this library is selected for visual fidelity and rewritten for maximum efficiency. To ensure your device remains fast, responsive, and stable, unoptimized 2D matrix effects or animations requiring multi-color segment layering are intentionally excluded in favor of streamlined, high-speed rendering.

#### Is this for me?
This project is **not** a full WLED replacement. Choose the right tool for your hardware:
* **Install [WLED](https://kno.wled.ge/) if:** You want the full lighting-only experience (SPI strips, E1.31, 150+ effects, sound reactivity).
* **Use ChimeraFX if:** You want to consolidate! You need a single ESP32 to handle sensors, relays, or switches **AND** run smooth, high-quality lighting effects simultaneously.

---

### Platform Compatibility

- **Framework:** **ESP-IDF** and **Arduino** (via RMT DMA)
- **Chips:** **ESP32 Classic** and **ESP32-S3**
  - *ESP32-C3/S2/C6 and ESP8266 are not officially supported due to single-core limitations.*
- **Protocol Support:** **1-wire NRZ** only (WS2812X, SK6812, WS2811)
  - *2-wire SPI strips (APA102, WS2801, etc.) are **not supported**.*

> **⚠️ Reality Check:** Visual effects are computationally expensive. A dual-core ESP32 is highly recommended. Trying to run complex effects alongside heavy ESPHome components (like Bluetooth Proxies or Cameras) will likely cause instability. Manage your load accordingly.

---

### Credits

- **[WLED](https://github.com/wled/WLED)** by Aircoookie — Original effect algorithms
- **[ESPHome](https://github.com/esphome/esphome)** by ESPHome — Framework integration
- **[FastLED](https://github.com/FastLED/FastLED)** by FastLED — Color handling and math utilities

---

### Support the Project

If you find **ChimeraFX** useful and would like to support the time and effort put into engineering this component, donations are never expected but always greatly appreciated!

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-ffdd00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black)](https://www.buymeacoffee.com/effelle)

---

### License & Legal Disclaimer

**EUPL-1.2** (European Union Public Licence) — See [LICENSE](LICENSE) for details.

**ChimeraFX** is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED project, the ESPHome project, or Nabu Casa. WLED, ESPHome, and Nabu Casa are trademarks of their respective owners.

This work incorporates code and logic derived from WLED and is licensed under the **European Union Public Licence v1.2 (EUPL-1.2)**.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.