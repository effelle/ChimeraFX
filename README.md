
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

*   **Install [WLED](https://kno.wled.ge/) if:** You want the full experience (Segments, E1.31, 150+ effects) or are dedicating an MCU solely to lighting.
*   **Use `ChimeraFX` if:** You want to consolidate! You need a single ESP32 to handle sensors, relays, or switches **AND** run smooth, high-quality lighting effects simultaneously.

## Platform Compatibility

- **Framework:** **ESP-IDF** and **Arduino**
- **Chips:** **ESP32 Classic** and **ESP32-S3**
  - *ESP32-C3/S2/C6 and ESP8266 are not officially supported due to single-core limitations.*

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
- **Intro Animations** — Wipe, Fade, Center, and Glitter effects on turn-on
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
  - platform: esp32_rmt_led_strip   # Or Neopixelbus for Arduino framework
    id: led_strip                   # The ID of the light needed for cfx_control
    # ... your light config ...
    effects:
      - addressable_cfx:
          name: "Aurora"    # The name of the effect, can be customized
          effect_id: 38     # See the available effects below for the ID
    # ... Add some other effect ...
```
---

## Available Effects

| ID | Name | Description |
|----|------|-------------|
| `0` | Static | Solid color with palette support |
| `2` | Breathe | Apple-style standby breathing LED |
| `3` | Wipe | Single color wipe |
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

### Mass Inclusion

To maintain a clean configuration file, you can load all 20+ effects at once using the provided `chimera_fx_effects.yaml` file.

1.  **Download** `chimera_fx_effects.yaml` from the repository root.
2.  **Save** it to your ESPHome configuration folder (e.g. `/config/`).
3.  **Include** it in your light configuration:

```yaml
light:
  - platform: esp32_rmt_led_strip # Or Neopixelbus for Arduino framework
    # ... your light config ...
    effects: !include chimera_fx_effects.yaml
```

> **Why aren't all WLED effects here yet?**  
> Bringing WLED effects to `ChimeraFX` is a meticulous process. Each effect is partially rewritten to "squeeze" every bit of performance out of the hardware with minimal resource overhead. My goal is to preserve the original look while ensuring the code runs perfectly within ESPHome alongside your other components. Thank you for your patience as I port them over incrementally!
---

## Available Palettes

Aurora, Forest, Halloween, Rainbow, Fire, Sunset, Ice, Party, Pastel, Ocean, HeatColors, Sakura, Rivendell, Cyberpunk, OrangeTeal, Christmas, RedBlue, Matrix, SunnyGold, Fairy, Twilight, Solid.

*(This list could be expanded in the future)*

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

**ChimeraFX** is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED project, the ESPHome project, or Nabu Casa. WLED, ESPHome, and Nabu Casa are trademarks of their respective owners.

This work incorporates code and logic derived from WLED and is licensed under the **European Union Public Licence v1.2 (EUPL-1.2)**.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.