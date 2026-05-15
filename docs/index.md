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
*   **LED count matters.** 1-wire NRZ strips (WS2812B, SK6812) are protocol-limited to ~800 kHz. For smooth installs on ESP32 Classic, target roughly **512-600 LEDs per GPIO**; higher counts can run, but physical refresh drops as RMT wire time takes over.
*   **Do not mix RMT and SPI on the same V1.41 node.** Physical testing showed that sustained SPI traffic can disturb RMT timing on ESP32 Classic. Use RMT-only or SPI-only nodes, or split the transports across separate controllers.
*   **SPI strips (APA102, SK9822) shift the bottleneck to the CPU**, not the wire. The current ESP32 Classic test rig ran 2x2000 SPI LEDs smoothly, but power delivery and inrush still matter.
*   **Heavy co-residents hurt.** Running ChimeraFX alongside *Bluetooth Proxy* or *Camera* components will likely cause instability.

For detailed FPS targets, RMT limits, RAM budgets, and power planning, see [Performance & Troubleshooting](Troubleshooting.md).

When testing performance, use `LedFPS` as the visible strip-refresh metric. `RenderFPS` shows how fast ChimeraFX calculates frames, which is useful for diagnosis but can be higher than the true LED output rate on long RMT strips.

---
## The "Good Citizen" Philosophy

ChimeraFX is built as a **transparent tool** designed to coexist peacefully with the rest of your ESPHome node. Unlike "greedy" implementations that sequester system resources, ChimeraFX operates under a "Good Citizen" principle: it respects the MCU's ability to handle multiple critical tasks simultaneously.

* **System Integrity:** It never overwrites core ESPHome components, ensuring compatibility with future updates.
* **Intelligent Resource Sharing:**
    * **Memory Safety:** It employs a dynamic **"Heap Floor"** to ensure critical stacks (Wi-Fi, API, Bluetooth) always have the memory they need to function safely.
    * **Power Awareness:** Includes integrated **Power Monitoring & Reduction** to estimate LED strip current and PSU load, with optional automatic brightness reduction to prevent hardware strain.
* **Resource Awareness:** It avoids "hijacking" the CPU, allowing your sensors, climate controls, and security features to remain responsive and reliable.
* **Active Transparency:** Through a built-in runtime debugger, the engine reports real-time CPU and memory usage, empowering you to optimize your configuration based on hard data.

This philosophy distinguishes ChimeraFX as a lighting engine that prioritizes the stability of your entire smart home ecosystem over just "showing pretty lights".

---

## Key Features

*   **Native Performance**: Optimized for ESP-IDF and dual-core ESP32s.
*   **ChimeraFX Light Platform**: A custom ESPHome light platform that support segments, allows you to run parallel complex RGB LED effects on ESP32 devices.
*   **ChimeraFX Sequencer**: High-performance logic layer for hardware-precise event triggers and responsive Home Assistant automations.
*   **Power Monitoring & Reduction**: Estimate LED strip current, power, PSU load, and energy usage, with optional manual or automatic brightness reduction.
*   **Zero-Lambda Config**: Uses a clean `external_components` setup.
*   **Rich Effect Library**: Ports of complex effects that were previously impossible or slow in pure YAML using `addressable_lambda`.
*   **Custom Palettes**: A curated selection of palettes to choose from plus a smart palette generator.
*   **Intro and Outro Effects**: Run a special effect when the light turns on or off.
* **Presets**: Capture and recall custom configurations from a near-infinite matrix of possibilities—layering specific intro/outro transitions, granular speed and intensity settings, and dynamic palettes into a single, cohesive lighting state.
*   **Full Control**: Support for Speed, Intensity, Palettes, and Mirroring in real time or through presets.
*   **Autotuning**: Automatically load default parameters and tune the effect for you .
*   **Debug Logger**: An easy way to enable/disable the logger at runtime level.

## Quick Links

*   **[Installation Guide](Installation.md)** - Get up and running in minutes.
*   **[Controls Guide](Controls.md)** - How to set up inputs and switches.
*   **[Power Monitor](Power-Monitor.md)** - Estimate LED power use and manage brightness reduction.
*   **[Effect Library](Effects-Library.md)** - Browse available effects and palettes.
*   **[ChimeraFX Sequencer](cfx_sequence.md)** - Logic, events, and reactive automations.
*   **[Troubleshooting](Troubleshooting.md)** - Fix common issues (flickering, memory).

---

Donations are never expected, but always appreciated. If you find ChimeraFX useful and would like to support its development, you can buy me a coffee **[here](https://www.buymeacoffee.com/effelle)**!

---

## Legal Disclaimer

**ChimeraFX** is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED project, the ESPHome project, or Nabu Casa. WLED, ESPHome, and Nabu Casa are trademarks of their respective owners.

This work incorporates some code and logic derived from WLED and is licensed under the **European Union Public Licence v1.2 (EUPL-1.2)**.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
