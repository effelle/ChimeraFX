![ChimeraFX](assets/ChimeraFX_new_banner_github.png)

# Welcome to ChimeraFX

**ChimeraFX** is a native C++ custom component for ESPHome that brings the beauty of iconic RGB LED effects to your Home Assistant setup. By integrating these effects directly into your existing ESPHome nodes, you can run premium animations and your sensors on a single MCU, eliminating the need for a separate, dedicated lighting controller.

It allows you to run complex RGB LED effects with high performance on ESP32 devices, completely avoiding the overhead and "spaghetti code" of old YAML lambda implementations.

---

### Is this for me?
**ChimeraFX** is not intended to be a full WLED replacement. You should choose the tool that best fits your hardware and requirements:
 
*   **Install [WLED](https://kno.wled.ge/) if:** You want the complete WLED experience (E1.31/DDP support, 150+ effects, 2D matrix, audio reactive effects, etc.) and are dedicating an MCU solely to lighting.
*   **Use `ChimeraFX` if:** You want to consolidate! If you need a single ESP32 to handle sensors, relays, or switches **and** run smooth, high-quality lighting effects simultaneously, ChimeraFX is for you.

### Reality Check

**Your mileage may vary.** Visual effects are computationally expensive.

*   **Use a dual-core ESP32.** Single-core devices (C3, S2) can run ChimeraFX, but may stutter under load — see the [hardware compatibility table](Installation.md#prerequisites).
*   **LED count matters.** 1-wire NRZ strips (WS2812B, SK6812) are protocol-limited to ~800 kHz. As a rule of thumb, target **60–70% of theoretical maximums** of 1101 LEDs at 800kHz to leave breathing room for Wi-Fi and other tasks. To mantain a stable 30 FPS on a dual core you should target ~650 LEDs per GPIO.
*   **SPI strips (APA102, SK9822) shift the bottleneck to the CPU**, not the wire. More LEDs = more math per frame.
*   **Heavy co-residents hurt.** Running ChimeraFX alongside *Bluetooth Proxy* or *Camera* components will likely cause instability.

For detailed FPS targets, RMT limits, RAM budgets, and power planning, see [Performance & Troubleshooting](Troubleshooting.md).

---

## Key Features

*   **Native Performance**: Optimized for ESP-IDF and dual-core ESP32s.
*   **ChimeraFX Light Platform**: A custom ESPHome light platform that support segments, allows you to run parallel complex RGB LED effects on ESP32 devices.
*   **ChimeraFX Sequencer**: High-performance logic layer for hardware-precise event triggers and responsive Home Assistant automations.
*   **Zero-Lambda Config**: Uses a clean `external_components` setup.
*   **Rich Effect Library**: Ports of complex effects that were previously impossible or slow in pure YAML using `addressable_lambda`.
*   **Custom Palettes**: A curated selection of palettes to choose from plus a smart palette generator.
*   **Intro and Outro Effects**: Run a special effect when the light turns on or off.
*   **Presets**: Create your own effect configurations.
*   **Full Control**: Support for Speed, Intensity, Palettes, and Mirroring in real time or through presets.
*   **Autotuning**: Automatically load default parameters and tune the effect for you .
*   **Debug Logger**: An easy way to enable/disable the logger at runtime level.

## Quick Links

*   **[Installation Guide](Installation.md)** - Get up and running in minutes.
*   **[Controls Guide](Controls.md)** - How to set up inputs and switches.
*   **[Effect Library](Effects-Library.md)** - Browse available effects and palettes.
*   **[ChimeraFX Sequencer](cfx_sequence.md)** - Logic, events, and reactive automations.
*   **[Troubleshooting](Troubleshooting.md)** - Fix common issues (flickering, memory).

---

Donations are never expected, but always appreciated. If you find ChimeraFX useful and would like to support its development, you can buy me a coffee **[here](https://www.buymeacoffee.com/effelle)**!

---

## Legal Disclaimer

**ChimeraFX** is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED project, the ESPHome project, or Nabu Casa. WLED, ESPHome, and Nabu Casa are trademarks of their respective owners.

This work incorporates code and logic derived from WLED and is licensed under the **European Union Public Licence v1.2 (EUPL-1.2)**.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.