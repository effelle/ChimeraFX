# 🔥 ChimeraFX: Made for ESPHome

> **High-performance WLED effects running natively within ESPHome.**

---

## 🐉 The Lore of the Chimera

A **Chimera** is a legendary creature composed of three animals. ChimeraFX merges three worlds:

- **🦁 The Lion (Power):** Raw WLED logic and effects — the proven algorithms that make lights come alive.
- **🐐 The Goat (Structure):** The reliable ESPHome framework — robust, maintainable, and Home Assistant native.
- **🐍 The Serpent (Connection):** Our custom abstraction layer — the binding force that seamlessly connects them.

---

## ⚠️ Platform Compatibility

- **Framework:** **ESP-IDF Only** (Arduino is NOT supported)
- **Chips:** **ESP32 Classic** and **ESP32-S3** Only
  - *ESP32-C3 is not supported due to single-core limitations.*

---

## ✨ Features

- **Native C++ Performance** — Optimized for multi-core ESP32s
- **Clean YAML Syntax** — Simple `addressable_cfx` configuration
- **20 Built-in Palettes** — Easily customizable
- **Smooth Transitions** — Professional-grade animations
- **Dynamic Controls** — Speed, intensity, palette, and mirror direction
- **Intro Animations** — Wipe, Fade, Center, and Glitter effects on turn-on

---

## 🚀 Quick Start

See [QUICKSTART.md](QUICKSTART.md) for installation and configuration.

Add to your ESPHome YAML:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    components: [cfx_effect]
```

---

## 📋 Available Effects

| ID | Name | Description |
|----|------|-------------|
| `0` | Static | Solid color with palette support |
| `2` | Breathe | Apple-style standby breathing LED |
| `3` | Wipe | Primary/Secondary color wipe |
| `6` | Sweep | Ping-pong wipe animation |
| `8` | Colorloop | Solid color cycling through palette |
| `9` | Rainbow | Per-pixel rainbow with density control |
| `18` | Dissolve | Random pixel color transitions |
| `38` | Aurora | Northern lights animation |
| `63` | Pride 2015 | Rainbow flag with breathing motion |
| `64` | Juggle | Eight bouncing dots with trails |
| `66` | Fire 2012 | Realistic fire simulation |
| `74` | Colortwinkle | Magical fairy-dust twinkles |
| `76` | Meteor | Meteor with random decay trail |
| `91` | Bouncing Balls | Real gravity physics |
| `97` | Plasma | Smooth plasma animation |
| `101` | Pacifica | Gentle ocean waves |
| `104` | Sunrise | Gradual sunrise/sunset simulation |
| `105` | Phased | Sine wave interference pattern |
| `110` | Flow | Smooth color zones animation |

---

## 🎨 Available Palettes

Default, Forest, Ocean, Rainbow, Fire, Sunset, Ice, Party, Lava, Pastel, Pacifica, HeatColors, Sakura, Rivendell, Cyberpunk, OrangeTeal, Christmas, RedBlue, Matrix, SunnyGold, Solid

---

## 🙏 Credits

- **[WLED](https://github.com/wled/WLED)** by Aircoookie — Original effect algorithms
- **FastLED** library — Color handling and math utilities
- **ESPHome** — Framework integration

---

## 📄 License

**EUPL-1.2** (European Union Public Licence) — See [LICENSE](LICENSE) for details.

---

## ⚖️ Legal Disclaimer

**ChimeraFX** is an independent project. It is not affiliated with, maintained by, or endorsed by the WLED project or Nabu Casa. WLED is a trademark of its respective owners. This work is a derivative of WLED, licensed under EUPL-1.2.
