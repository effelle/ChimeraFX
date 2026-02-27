# Effects Library

This is the central repository for all ChimeraFX effects. Each effect is designed to provide a high-performance visual experience using specialized math and DMA-driven output.

## Table of Contents
- [Intro Effects](#intro-effects)
- [Outro Effects](#outro-effects)
- [Main Effects](#main-effects)

---

## Intro Effects
(Intro effects documentation here)

---

## Outro Effects
(Outro effects documentation here)

---

## Main Effects

??? abstract "38 | Aurora &nbsp;&nbsp; :material-tag-outline: v1.0.0 &nbsp;&nbsp; :material-speedometer: Low &nbsp;&nbsp; :material-palette: ❌"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/aurora.webm" type="video/webm">
        </video>
        
        *Northern lights animation with drifting waves and horizontal color movements.*

    === "⚙️ Controls"
        | Parameter | Default | Function Description |
        | :--- | :--- | :--- |
        | **Intensity** | `128` | **Wave width**: determines how broad the light bands are. |
        | **Speed** | `128` | **Drift speed**: controls the horizontal movement velocity. |
        | **Palette** | — | **Not Supported**: Aurora is locked to its internal rainbow colors. |

    === "💻 Config"
        ```yaml
        - name: "Aurora"
          id: 38
        ```
        **Note:** This effect does support presets.

    :material-lightbulb-outline: **Tip:** Aurora looks best on dense strips (>60 LEDs/m) where the smooth color drifting can be fully appreciated.
