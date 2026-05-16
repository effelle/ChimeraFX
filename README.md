![ChimeraFX](media/ChimeraFX_new_banner_github.png)

# ChimeraFX: Advanced LED Effects Engine for ESPHome

*A high-performance C++ rendering engine featuring a powerful event-driven sequencer to deliver premium fluid simulations, architectural transitions, and optimized WLED classics natively within the ESPHome ecosystem.*

Unlike the old `addressable_lambda` method, this implementation runs as a proper native component optimized for both ESP-IDF and Arduino frameworks, freeing up your YAML and maximizing frame rates.

### Getting Started

#### Quick Configuration

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main

light:
  - platform: cfx_light
    name: "LED Strip"
    id: led_strip
    pin: GPIO16             # GPIO pin where the LED strip is connected
    num_leds: 120           # Number of LEDs in the strip
    chipset: WS2812X        # Type of LED strip (WS2812X, SK6812, WS2811, etc.)
```

That’s it! You already have everything you need to run ChimeraFX!

**[Or read the Official Documentation to unlock the full potential of ChimeraFX.](https://effelle.github.io/ChimeraFX/)**

The documentation includes everything you need:
- [Installation & Quick Start](https://effelle.github.io/ChimeraFX/Installation/)
- [The Sequencer Guide](https://effelle.github.io/ChimeraFX/cfx_sequence/)
- [Visual Effects Gallery](https://effelle.github.io/ChimeraFX/Effects-Library/)

---

### Key Features

* **Zero YAML Overhead** — Pure C++ implementation for maximum ESP32 frame rates.
* **Dual Framework Support** — Runs as a proper native component under both ESP-IDF and Arduino.
* **ChimeraFX Originals** — Exclusive physics-based fluid and noise algorithms.
* **Architectural Transitions** — Premium intro/outro sweeps for monochromatic setups.
* **Segment Runner Core** — High-performance parallel rendering for up to 4 independent segments per light, with platform-specific multi-output limits for RMT-only or SPI-only nodes.
* **Powerful Sequencer** — A native C++ event-driven engine for multi-light orchestration and complex timelines, running locally on-device with seamless Home Assistant integration.
* **Smart Random Palette** — Procedural color theory engine for aesthetic, non-repeating palettes.
* **Modular Preset Architecture** — Build bespoke configurations by mixing-and-matching transitions, settings and effects. Leverage effects and procedural color groups to create a nearly infinite library of architectural and decorative lighting styles.
* **Intelligent Autotune** — Automatically snaps to optimal parameters, but instantly yields to manual slider adjustments.
* **100% Home Assistant Native** — Instantly exposes speed, intensity, palette, and other controls.
* **Advanced Debugging** — Real-time runtime logger to fine-tune behavior and optimize frame-by-frame performance.

---

### Beyond WLED (The Philosophy)

ChimeraFX stands as a distinct, high-performance lighting engine and sequencer. It bridges the gap between hand-tuned open-source favorites and **exclusive original algorithms** designed specifically for architectural integration and fluid motion.

#### Is this for me?
This project is **not** a full WLED replacement. Choose the right tool for your hardware:
* **Install [WLED](https://kno.wled.ge/) if:** You want the full lighting-only experience (E1.31, 150+ effects, sound reactivity, 2D matrix support, etc.).
* **Use ChimeraFX if:** You want to consolidate! You need a single ESP32 to handle sensors, relays, or switches **AND** run smooth, high-quality lighting effects simultaneously.

---

### Platform Compatibility

> ⚠️**[IMPORTANT]** Minimum Requirement: ChimeraFX requires ESPHome **2026.3.0 or later** to leverage the latest C++ performance optimizations and core event loop improvements.

- **Framework:** **ESP-IDF** and **Arduino**
- **Recommended V1.41 target:** **ESP32 Classic**. It has the strongest validated multi-output RMT performance in this release.
- **Chips:** **ESP32 Classic**, **ESP32-S2**, **ESP32-S3**, **ESP32-C3**, **ESP32-C6**.
  - **ESP32-S3:** Supported, but not the preferred V1.41 target for large multi-output RMT installations. Use up to 2 RMT outputs or 2 SPI outputs per node; the first RMT output can use GDMA, while additional RMT outputs use non-DMA transmission.
  - **⚠️ ESP8266 IS NOT SUPPORTED:** Due to architectural differences, lack of hardware FPU, and severe memory constraints, ESP8266 will not compile or run with ChimeraFX.
- **Protocol Support:** **1-wire NRZ** (WS2812X, SK6812, WS2811), **2-wire SPI** (APA102, SK9822) are working but still in **BETA** and under development.

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
