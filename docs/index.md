

# Welcome to ChimeraFX

**ChimeraFX** is a native C++ custom component for ESPHome that brings the beauty of iconic RGB LED effects to your Home Assistant setup. By integrating these effects directly into your existing ESPHome nodes, you can run premium animations and your sensors on a single MCU, eliminating the need for a separate, dedicated lighting controller.

It allows you to run complex RGB LED effects with high performance on ESP32 devices, completely avoiding the overhead and "spaghetti code" of old YAML lambda implementations.

---

### Is this for me?
**ChimeraFX** is not intended to be a full WLED replacement. You should choose the tool that best fits your hardware and requirements:
 
*   **Install [WLED](https://kno.wled.ge/) if:** You want the complete WLED experience (Segments, E1.31/DDP support, 150+ effects, 2D matrix) and are dedicating an MCU solely to lighting.
*   **Use `ChimeraFX` if:** You want to consolidate! If you need a single ESP32 to handle sensors, relays, or switches **and** run smooth, high-quality lighting effects simultaneously, ChimeraFX is for you.

### Reality Check

**Your mileage may vary.**
Visual effects are computationally expensive.

*   **Hardware:** A dual-core ESP32 is highly recommended. Its architecture allows for smooth effect rendering independent of network tasks. Single-core devices may function, but performance cannot be guaranteed and will vary significantly based on the specific effect and overall system load.

*   **Resources:** Trying to run complex effects alongside heavy components (like *Bluetooth Proxy* or *Cameras*) will likely cause instability.

*   **Optimization:** This library is optimized for ESP-IDF, but hardware resources are finite. Manage your load accordingly.

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

Donations are never expected, but always appreciated. If you find ChimeraFX useful and would like to support its development, you can buy me a coffee **[here](https://www.buymeacoffee.com/effelle)**!

---

## Legal Disclaimer

**ChimeraFX** is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED project, the ESPHome project, or Nabu Casa. WLED, ESPHome, and Nabu Casa are trademarks of their respective owners.

This work incorporates code and logic derived from WLED and is licensed under the **European Union Public Licence v1.2 (EUPL-1.2)**.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


