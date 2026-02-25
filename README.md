
![ChimeraFX](media/ChimeraFX_new_banner_github.png)

# ChimeraFX: Made for ESPHome


> **High-performance WLED-style effects running natively within ESPHome.**

---

## The Lore of the Chimera

A **Chimera** is a legendary creature composed of three animals. ChimeraFX merges three worlds:

- **The Lion (Power):** Raw WLED logic and effects — the proven algorithms that make lights come alive.
- **The Goat (Structure):** The reliable ESPHome framework — robust, maintainable, and Home Assistant native.
- **The Serpent (Connection):** My custom abstraction layer — the binding force that seamlessly connects them.

---

### Is this for me?

This project is **not** a full WLED replacement and it will never be. Choose the right tool for your hardware:

*   **Install [WLED](https://kno.wled.ge/) if:** You want the full experience (E1.31, 150+ effects, audio reactive, etc.) or are dedicating an MCU solely to lighting.
*   **Use `ChimeraFX` if:** You want to consolidate! You need a single ESP32 to handle sensors, relays, or switches **AND** run smooth, high-quality lighting effects simultaneously.

## Platform Compatibility

- **Framework:** **ESP-IDF** (via RMT DMA) and **Arduino** (via NeoPixelBus)
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

## Features

- **Native C++ Performance** — Optimized for multi-core ESP32s
- **Clean YAML Syntax** — Simple `addressable_cfx` configuration
- **Many Built-in Palettes** — Easily customizable
- **Smooth Transitions** — Professional-grade animations
- **Dynamic Controls** — Speed, intensity, palette, and mirror direction
- **Intelligent Autotune** — Automatically snaps to optimal creator defaults, but instantly yields manual control when you touch a slider.
- **Intro and Outro Animations** — Wipe, Fade, Center, and Glitter and more effects on turn-on and turn-off
- **Timer** — Turn off after a specified amount of time
- **Presets** — Save and restore effect configurations
- **Debug Logger** — Turn on/off a logger at runtime level for the component

---

## Quick Start

See the [Wiki](https://effelle.github.io/ChimeraFX/) for a complete and detailed installation and configuration guide.

Add the component to your ESPHome YAML:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    components: [cfx_effect]

cfx_effect:
  cfx_control:
    - id: my_cfx_controller   # The ID of the controller generator, customizable
      name: "LED Strip"       # The prefix name of the controller, customizable
      light_id: led_strip     # The ID of the light you want to control 
```

**Note:** The `light_id` parameter is the magic sauce that links the controller to a specific light. This is necessary because the `addressable_cfx` effect needs to know which light it is controlling.

Add the effects to your light:

```yaml
light:
  - platform: cfx_light             # The high-performance ChimeraFX DMA driver
    id: led_strip                   # The ID of the light needed for cfx_control
    pin: GPIO16
    num_leds: 60
    chipset: WS2812X
    all_effects: true               # Auto-loads all effects!
    # If you prefer not to use all_effects, you can add them manually:
    # effects:
    #   - addressable_cfx:
    #       name: "Aurora"    # The name of the effect, can be customized
    #       effect_id: 38     # See the available effects below for the ID
```
---

### Available Effects and Palettes

ChimeraFX currently supports over 50+ effects optimized for the ESP32—including Aurora, Fire, Rainbow, Meteor, Ocean, and Bouncing Balls, plus some original effects, with some dedicated to monochromatic lighting. The component also includes a wide variety of built-in palettes, giving you complete creative control over your lighting.

#### [Click here to see the full Effect and Palette List](https://effelle.github.io/ChimeraFX/Effects-Library)


**Why aren't all WLED effects here?**  
`ChimeraFX`uses a curated selection of effects. I meticulously rewrite each effect to ensure it runs efficiently alongside your other ESPHome components without sacrificing visual quality. However, 2D matrix animations and effects requiring two or three separate color selections will not be ported, as they are incompatible with the current ESPHome and ChimeraFX architecture. 

---

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