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

**Your mileage may vary.**
Visual effects are computationally expensive.

*   **Hardware:** A dual-core ESP32 is highly recommended. Its architecture allows for smooth effect rendering independent of network tasks. Single-core devices may function, but performance cannot be guaranteed and will vary significantly based on the specific effect and overall system load.

*   **Hardware Framerate Bottlenecks:** While ChimeraFX targets a fluid **60 FPS**, the WS281x protocol is locked to a fixed **800kHz** data rate. Each pixel requires a specific amount of time to receive its data (~30 µs for RGB; ~40 µs for RGBW), followed by a "reset" pulse (typically >50 µs). Because this data is sent serially, your **pixel count per pin**—not the ESP32’s CPU speed—determines the maximum possible framerate.

    To maintain a stable **30 FPS** (a ~33ms frame budget), follow these per-pin limits:
    *   **RGB (WS2812B/WS2811):** Maximum **~1,000 LEDs** per data pin.
    *   **RGBW (SK6812):** Maximum **~800 LEDs** per data pin.

    Pushing beyond these limits (e.g., 1,200+ LEDs on a single pin) forces the hardware to drop frames simply because the binary packet for a single update becomes longer than the time available between frames. To control more LEDs without sacrificing smoothness, distribute your strips across multiple ESP32 pins to take advantage of parallel driving.


*   **Resources:** Trying to run complex effects alongside heavy components (like *Bluetooth Proxy* or *Cameras*) will likely cause instability.

*   **Optimization:** This library is optimized for ESP-IDF, but hardware resources are finite. Manage your load accordingly.

---

## Key Features

*   **Native Performance**: Optimized for ESP-IDF and dual-core ESP32s.
*   **ChimeraFX Light Platform**: A custom ESPHome light platform that allows you to run up to 4 parallel complex RGB LED effects on ESP32 devices.
*   **Zero-Lambda Config**: Uses a clean `external_components` setup.
*   **Rich Effect Library**: Ports of complex effects that were previously impossible or slow in pure YAML using `addressable_lambda`.
*   **Custom Palettes**: A curated selection of palettes to choose from.
*   **Intro and Outro Effects**: Run a special effect when the light turns on or off.
*   **Presets**: Create your own effect configurations.
*   **Timers**: Run an effect for a specific amount of time.
*   **Full Control**: Support for Speed, Intensity, Palettes, timers and Mirroring in real time or through presets.
*   **Autotuning**: Automatically load default parameters and tune the effect for you .
*   **Debug Logger**: An easy way to enable/disable the logger at runtime level.

## Quick Links

*   **[Installation Guide](Installation.md)** - Get up and running in minutes.
*   **[Controls Guide](Controls.md)** - How to set up inputs and switches.
*   **[Effect Library](Effects-Library.md)** - Browse available effects and palettes.
*   **[Troubleshooting](Troubleshooting.md)** - Fix common issues (flickering, memory).

---

Donations are never expected, but always appreciated. If you find ChimeraFX useful and would like to support its development, you can buy me a coffee **[here](https://www.buymeacoffee.com/effelle)**!

---

## Legal Disclaimer

**ChimeraFX** is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED project, the ESPHome project, or Nabu Casa. WLED, ESPHome, and Nabu Casa are trademarks of their respective owners.

This work incorporates code and logic derived from WLED and is licensed under the **European Union Public Licence v1.2 (EUPL-1.2)**.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.