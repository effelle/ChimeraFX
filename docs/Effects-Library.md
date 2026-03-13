<style>
  /* Target the first column (ID) */
  .md-typeset table th:nth-child(1), 
  .md-typeset table td:nth-child(1) {
    width: 40px !important;       /* Force a small width */
    min-width: 40px !important;   /* Prevent theme from expanding it */
    padding-left: 8px !important; /* Reduce default theme padding */
    padding-right: 8px !important;
    text-align: center !important;
    white-space: nowrap !important;
  }

  /* 1. Shrink accordion title padding and font size on mobile */
  @media screen and (max-width: 45em) {
    .md-typeset details summary {
      padding-left: 2.5rem !important; /* Space for the opening arrow */
      padding-top: 0.4rem !important;
      padding-bottom: 0.4rem !important;
      /* font-size: 0.65rem !important;   Slightly smaller text */
    }
    
    /* 2. Reduce the gap between your custom icons/labels */
    .md-typeset details summary ::marker, 
    /* .md-typeset details summary {
      letter-spacing: -0.02em; Tighten character spacing slightly */
    }
  }

  /* 3. Global fix: Reduce the "dead space" created by &nbsp; */
  .md-typeset details summary span.extra-info {
    margin-left: 8px;
    display: inline-flex;
    align-items: center;
    gap: 4px; /* Use CSS gaps instead of multiple &nbsp; */
  }
</style>

# Available Effects, Palettes, Intros and Outros

## Beyond the Port: A New Standard

While ChimeraFX began as a project to bring WLED classics to ESPHome, it has evolved into something unique. The focus has shifted from simple replication to **innovation**.

Today, ChimeraFX is a precision-engineered lighting engine offering a curated suite of effects designed specifically for the ESPHome architecture, prioritizing **visual fidelity and resource efficiency** over raw quantity.

*Note: To maintain this focus on performance and quality, unoptimized effects or those requiring complex 2D matrices/multi-segment layering are intentionally excluded.*

---

## Icon Legend
| Icon | Description | Icon | Description |
|:---:|:---|:---:|:---|
| :material-tag-outline: | **Minimum Version**: The ChimeraFX version where this effect was introduced. | :material-speedometer: | **Performance Class**: The resource intensity of the effect (Low, Mid, High). |
| :material-palette: | **Palette Support**: Whether the effect supports color palettes. | :material-bullseye-arrow: | **Sequencer Ready**: Fully compatible with position-based triggers (**on_reach**, **on_pixel_num**). |

---

## 1. ChimeraFX Original Effects
**Signature algorithms engineered exclusively for this engine.**

These are the flagship animations of ChimeraFX. They feature advanced fluid simulations, chaos theory, and custom physics engines not found in any other library.

??? abstract "152 | Center Gauge | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Center_Gauge.webm" type="video/webm">
        </video>
        
        *A symmetrical progress bar expanding smoothly from the middle to the edges.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Percent fill**: determines how much of the strip is occupied. |
        | **Speed** | `128` | **Smoothness**: controls the transition speed of the fill. |
        | **Palette** | — | **Supported**: Affects the color of the gauge bars. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Center Gauge"
              effect_id: 152
        ```

??? abstract "159 | Chaos Theory | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: High</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Chaos_Theory.webm" type="video/webm">
        </video>
        
        *Scrolling color bands with noise-driven organic shifts between calm flow and twinkling chaos.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Peak Chaos**: adjusts the frequency of twinkling eruptions. |
        | **Speed** | `128` | **Flow speed**: controls the velocity of the underlying color bands. |
        | **Palette** | — | **Not Supported**: Uses its internal organic color engine. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Chaos Theory"
              effect_id: 159
        ```

??? abstract "164 | Collider | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Collider.webm" type="video/webm">
        </video>
        
        *Chromatic liquid nodes that expand from drifting origins. Collisions trigger a "sticky" bridge with additive color mixing.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `170` | **Grid density**: determines the number and size of liquid nodes. |
        | **Speed** | `100` | **Pulse speed**: controls the drifting and expansion velocity. |
        | **Palette** | — | **Supported**: Colors the liquid nodes and collision bridges. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Collider"
              effect_id: 164
        ```

??? abstract "151 | Dropping Time | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Dropping_Time.webm" type="video/webm">
        </video>
        
        *Falling drops filling a bucket. A visual timer that physically fills the strip over time.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Not used**: Reserved for future visual refinements. |
        | **Speed** | `15` | **Timer Duration**: controls how long it takes to fill the strip. |
        | **Palette** | — | **Supported**: Defaults to **Ocean** (11). |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Dropping Time"
              effect_id: 151
        ```

??? abstract "158 | Energy | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: High</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Energy.webm" type="video/webm">
        </video>
        
        *Rainbow flow with chaotic agitation and white-hot eruptions.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Zoom**: adjusts the scale of the energy eruptions. |
        | **Speed** | `128` | **Agitation**: controls the wipe and fluctuation speed. |
        | **Palette** | — | **Not Supported**: Uses a custom high-energy rainbow engine. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Energy"
              effect_id: 158
        ```

??? abstract "160 | Fluid Rain | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: High | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Fluid_Rain.webm" type="video/webm">
        </video>
        
        *A realistic liquid simulation. Raindrops hit the strip, creating organic ripples that travel, collide, and bounce off the edges.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Damping**: controls the viscosity (how long ripples last). |
        | **Speed** | `128` | **Rainrate**: determines how frequently new drops fall. |
        | **Palette** | — | **Supported**: Defaults to **Ocean** (11). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Fluid Rain"
              effect_id: 160
        ```

??? abstract "156 | Follow Me | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Follow_Me.webm" type="video/webm">
        </video>
        
        *Single cursor running from one side to another with a persisting trail.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `40` | **Fade rate**: determines how quickly the trail vanishes. |
        | **Speed** | `140` | **Cursor speed**: controls the velocity of the running dot. |
        | **Palette** | — | **Not Supported**: Uses the primary selected color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Follow Me"
              effect_id: 156
        ```

??? abstract "157 | Follow Us | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Follow_Us.webm" type="video/webm">
        </video>
        
        *Three cursors running from one side to another in a coordinated sequence.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Distance**: adjusts the spacing between the three cursors. |
        | **Speed** | `128` | **Velocity**: controls the combined speed of the cursors. |
        | **Palette** | — | **Not Supported**: Uses the primary selected color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Follow Us"
              effect_id: 157
        ```

??? abstract "180 | Interference | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: High | :material-palette:</span>"

    === "🎬 Preview"
        *A "Batch 2" signature effect. High-frequency light waves collide and interfere, creating a shimmering, high-energy field. Unlike other monochromatic effects, Interference fully supports palettes and features a dedicated "Interference" soft fade-in.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `170` | **Wave Frequency**: adjusts the density of the interference bands. |
        | **Speed** | `160` | **Shift Speed**: controls how fast the patterns collide. |
        | **Palette** | — | **Supported**: Defaults to **Cyberpunk** (15). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Interference"
              effect_id: 180
        ```

??? abstract "155 | Kaleidos | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Kaleidos.webm" type="video/webm">
        </video>
        
        *Symmetrical mirroring of animations. Creates rhythmic, geometric patterns that fold over the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `150` | **Segment count**: determines how many mirror folds are created. |
        | **Speed** | `60` | **Scroll speed**: controls the movement of the underlying pattern. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Kaleidos"
              effect_id: 155
        ```

??? abstract "154 | Reactor Beat | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Reactor_Beat.webm" type="video/webm">
        </video>
        
        *A rhythmic, high-energy heartbeat pushing outward from the center.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Pulse decay**: controls how fast the beat energy dissipates. |
        | **Speed** | `128` | **BPM**: sets the frequency of the heartbeats. |
        | **Palette** | — | **Supported**: Affects the color of the kinetic energy pulses. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Reactor Beat"
              effect_id: 154
        ```

??? abstract "153 | Twin Flames | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Twin_Flames.webm" type="video/webm">
        </video>
        
        *A symmetric variation of the classic Fire simulation. Two flames ignite and burn towards the center.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `160` | **Sparking rate**: determines how frequently new embers are born. |
        | **Speed** | `64` | **Cooling rate**: controls how fast the fire dissipates upwards. |
        | **Palette** | — | **Supported**: Defaults to **Fire** (5). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Twin Flames"
              effect_id: 153
        ```

---

## 2. Monochromatic & Architectural
**Kinetic animations designed specifically for single-color setups.**

Best for modern interiors and architectural lighting. These effects focus on elegant brightness modulation rather than color cycling, and feature integrated "Horizon Sweep" transitions for seamless power-on/off sequences.

??? abstract "170 | Assembly | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Mid | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Assembly.webm" type="video/webm">
        </video>
        *A kinetic, gravity-based transition. Blocks of varying sizes free-fall and stack to physically construct the strip, and systematically peel away to collapse into darkness when turning off.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Not used**: Reserved for future layout variations. |
        | **Speed** | `128` | **Gravity**: controls the velocity of falling and peeling blocks. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Assembly"
              effect_id: 170
        ```


??? abstract "162 | Curtain Sweep | <span class='extra-info'>:material-tag-outline: 1.3.1 | :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Curtain_Sweep.webm" type="video/webm">
        </video>
        
        *A mirrored on/off transition. Converges from the sides to fill the center, then expands outward from the middle to clear the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Edge blur radius**: controls the softness of the sweep edge. |
        | **Speed** | `1` | **Sweep duration**: controls the speed of the architectural transition. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Curtain Sweep"
              effect_id: 162
        ```
??? abstract "168 | Hydro-Pulse | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Mid | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Hydro-Pulse.webm" type="video/webm">
        </video>
        
        *A high-pressure fluid dynamic flow simulation. Features a turbulent "Pressurize" intro and a "Drain" outro that leaves realistic droplets succumbing to gravity.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Viscosity**: determines the fluid dampening and drop cling. |
        | **Speed** | `128` | **Pressure**: controls the surge velocity and impact force. |
        | **Palette** | — | **Not Supported**: Features a dedicated monochromatic fluid engine. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Hydro-Pulse"
              effect_id: 168
        ```
??? abstract "161 | Horizon Sweep | <span class='extra-info'>:material-tag-outline: 1.3.1 | :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Horizon_Sweep.webm" type="video/webm">
        </video>
        
        *A smooth, directional on/off transition for solid colors. Sweeps the light across the strip from a snappy 0.5s zip to a luxurious 10s reveal.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Edge blur radius**: controls the softness of the sweep edge. |
        | **Speed** | `1` | **Sweep duration**: controls the speed of the architectural transition. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Horizon Sweep"
              effect_id: 161
        ```

??? abstract "163 | Stardust Sweep | <span class='extra-info'>:material-tag-outline: 1.3.1 | :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Stardust_Sweep.webm" type="video/webm">
        </video>
        
        *A sparkling on/off transition. Builds a solid color through a flurry of twinkling lights, and dissolves back into shimmering glitter on exit.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Edge blur radius**: controls the softness of the sweep edge. |
        | **Speed** | `1` | **Sweep duration**: controls the speed of the architectural transition. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Stardust Sweep"
              effect_id: 163
        ```

??? abstract "165 | Twin Pulse Sweep | <span class='extra-info'>:material-tag-outline: 1.3.1 | :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Twin_Pulse_Sweep.webm" type="video/webm">
        </video>
        
        *A cinematic monochromatic reveal. Dual light pulses race across the strip to lead the entry, and return to eat the light during the exit.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Edge blur radius**: controls the softness of the sweep edge. |
        | **Speed** | `1` | **Sweep duration**: controls the speed of the architectural transition. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Twin Pulse Sweep"
              effect_id: 165
        ```

??? abstract "166 | Morse Sweep | <span class='extra-info'>:material-tag-outline: 1.3.1 | :material-speedometer: Low</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Transmission.webm" type="video/webm">
        </video>
        
        *A "hidden in plain sight" reveal. Flashes the words "ON" and "OFF" in Morse code using the current solid color.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | — | **Not used**: Reserved for future refinements. |
        | **Speed** | `128` | **Morse speed**: controls the playback speed of the Morse sequence. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Morse Sweep"
              effect_id: 166
        ```

??? abstract "167 | Four Times the Charm | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/quadrant_io.webm" type="video/webm">
        </video>
        *A multi-center architectural sweep. Splitting the strip into quadrants, it runs two simultaneous 'Curtain Sweeps' starting from the 25% and 75% positions. Perfect for complex architectural highlight sequences.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Edge blur radius**: controls the softness of the sweep edges. |
        | **Speed** | `1` | **Sweep duration**: controls the speed of the architectural transitions. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Four Times the Charm"
              effect_id: 167
        ```

??? abstract "169 | Dropping Fill | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Dropping_Fill.webm" type="video/webm">
        </video>
        
        *Falling droplets that accumulate at the bottom of the strip. Similar to Dropping Time but with a monochromatic focus and physics-based accumulation.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Not used**: Reserved for future visual refinements. |
        | **Speed** | `1` | **Duration**: controls the frequency of droplets and total fill time. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Dropping Fill"
              effect_id: 169
        ```


??? abstract "174 | Crystallize | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"

    === "🎬 Preview"
        *Four crystallization seeds simultaneously nucleate and grow outward, their edges sharp and luminous. A formation sequence that feels geological — ancient and precise.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Not used**: Monochromatic — no blur. |
        | **Speed** | `1` | **Duration**: slider maps to intro/outro duration. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Crystallize"
              effect_id: 174
        ```

??? abstract "175 | Deep Breathe | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"

    === "🎬 Preview"
        *The strip awakens like a slow, deep breath — gamma-corrected brightness rises from absolute darkness to full presence, with a subtle triangular shimmer that gives it warmth and depth.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Not used**: Monochromatic — no blur. |
        | **Speed** | `1` | **Duration**: slider maps to intro/outro duration. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Deep Breathe"
              effect_id: 175
        ```

??? abstract "171 | Inertia Sweep | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"

    === "🎬 Preview"
        *A precisely-eased fill that sweeps from dark to full brightness in a single pass, with a hot leading edge that pulses three pixels of decreasing intensity ahead of the cursor.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Not used**: Monochromatic — no blur. |
        | **Speed** | `1` | **Duration**: slider maps to intro/outro duration. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Inertia Sweep"
              effect_id: 171
        ```

??? abstract "172 | Sonar Reveal | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"

    === "🎬 Preview"
        *A radar-like scanning beam sweeps back and forth four times, each pass permanently lifting the brightness floor — like sonar gradually resolving a picture from nothing to full presence.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Not used**: Monochromatic — no blur. |
        | **Speed** | `1` | **Duration**: slider maps to intro/outro duration. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Sonar Reveal"
              effect_id: 172
        ```

??? abstract "173 | Venetian | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"

    === "🎬 Preview"
        *Like venetian blinds opening in two stages — even pixels rotate into light first, then odd pixels follow in a second sweep. Clean, architectural, precise.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Not used**: Monochromatic — no blur. |
        | **Speed** | `1` | **Duration**: slider maps to intro/outro duration. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Venetian"
              effect_id: 173
        ```

??? abstract "176 | Moiré Shift | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Mid</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Moire_Shift.webm" type="video/webm">
        </video>
        *A high-frequency interference pattern that shifts and breathes. Features a complex overlapping sinewave engine that creates organic, shifting shadows and light pockets. Now refined with a proper architectural transition that resolves to a solid floor.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Complexity**: adjusts the frequency of the overlapping waves. |
        | **Speed** | `128` | **Drift**: controls the velocity of the interference shift. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Moiré Shift"
              effect_id: 176
        ```

??? abstract "177 | Resonance Fill | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Resonance_Fill.webm" type="video/webm">
        </video>
        *A liquid-reactive fill sequence. Light surges across the strip with a decaying ripple effect, resonating at the leading edge. The "Resonance" intro features a high-impact surge, while the "Resonance Fade" outro provides a rhythmic, decaying drain.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Ripple Decadence**: controls the frequency and decay of the feedback ripples. |
        | **Speed** | `100` | **Flow Pressure**: controls the velocity of the fill surge. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Resonance Fill"
              effect_id: 177
        ```

??? abstract "178 | Telemetry | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Telemetry.webm" type="video/webm">
        </video>
        *A digital, block-based construction sequence. Dashes of light "transmit" across the strip to build the solid floor. Features a "Telemetry" intro with sharp data-bursts and a refined "Telemetry Fade" outro with softened trail-retraction.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Data Density**: adjusts the length and spacing of the telemetry dashes. |
        | **Speed** | `128` | **Baud Rate**: controls the transmission speed across the strip. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Telemetry"
              effect_id: 178
        ```

??? abstract "179 | Stellar Dust | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Stellar_Dust.webm" type="video/webm">
        </video>
        *The strip awakens through a flurry of distant, breathing stars. Each pixel enters with a randomized phase-shifted pulse, creating a deep field of light that eventually stabilizes into a solid floor. Fixed in 1.4.1 for instant response.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Star Density**: adjusts the contrast between breathing pixels. |
        | **Speed** | `128` | **Blink Rate**: controls the frequency of the stellar breathing. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Stellar Dust"
              effect_id: 179
        ```

??? abstract "255 | Ambient Roulette | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low</span>"


    === "🎬 Preview"
        *Keeps your space dynamic but grounded. Randomly cycles through the premium monochromatic animation library, ensuring your lighting feels alive without being overwhelming.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | — | **Delegated**: Passed to the randomly selected monochromatic effect. |
        | **Speed** | — | **Delegated**: Passed to the randomly selected monochromatic effect. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        
    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Ambient Roulette"
              effect_id: 255
        ```


??? abstract "181 | Eclipse | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        *A masterclass in negative space. The strip remains gently illuminated while a smooth, deep shadow slowly glides across the light, creating a calming, reverse-kinetic effect.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Shadow width**: determines the size of the negative space. |
        | **Speed** | `128` | **Glide velocity**: controls the speed of the drifting shadow. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Eclipse"
              effect_id: 181
        ```

??? abstract "182 | Gas Discharge | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        *A digital homage to analog ignition. The strip stutters to life with high-frequency flashes before finally catching and settling into a solid block of light.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Flutter intensity**: controls the contrast and aggressiveness of the stutter sequence. |
        | **Speed** | `128` | **Ignition duration**: controls the length of the ignition phase. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Gas Discharge"
              effect_id: 182
        ```

??? abstract "183 | Harmonic Settle | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        *A physical collision transformed into light. The strip fills rapidly, overshoots its brightness target, and then rhythmically 'bounces' back into its stable state like a tuned spring.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Spring stiffness**: determines the tension and number of bounces before settling. |
        | **Speed** | `128` | **Surge velocity**: controls the speed of the initial brightness jump. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Harmonic Settle"
              effect_id: 183
        ```

??? abstract "184 | Lithograph | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        *A precise, machine-like drawing process. A bright, sharp cursor 'prints' the light onto the strip line by line, leaving a solid trail behind it until the entire sequence is complete.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Cursor sharpness**: controls the brightness and width of the leading edge. |
        | **Speed** | `128` | **Print speed**: controls the velocity of the print cursor. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Lithograph"
              effect_id: 184
        ```

---
## 3. WLED Classics (Remastered)

**The definitive collection of open-source favorites.**

A hand-picked selection of the community's best effects, meticulously optimized and rewritten to run natively within the ChimeraFX engine with zero overhead.

??? abstract "38 | Aurora | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Aurora.webm" type="video/webm">
        </video>
        
        *Northern lights animation with drifting waves and horizontal color movements.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Wave width**: determines how broad the light bands are. |
        | **Speed** | `24` | **Drift speed**: controls the horizontal movement velocity. |
        | **Palette** | — | **Supported**: Defaults to **Aurora** (1). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Aurora"
              effect_id: 38
        ```

??? abstract "1 | Blink | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Blink.webm" type="video/webm">
        </video>
        
        *Simple on/off blinking of the entire strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | — | **Not used**: Reserved for future refinements. |
        | **Speed** | `128` | **Blink rate**: controls the frequency of the on/off transitions. |
        | **Palette** | — | **Supported**: Sets the color of the active blink. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Blink"
              effect_id: 1
        ```

??? abstract "26 | Blink Rainbow | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Blink_Rainbow.webm" type="video/webm">
        </video>
        
        *Blinking with color cycling across the selected palette.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Color speed**: controls how fast the colors cycle between blinks. |
        | **Speed** | `128` | **Blink rate**: controls the frequency of the on/off transitions. |
        | **Palette** | — | **Supported**: Defines the color cycle range. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Blink Rainbow"
              effect_id: 26
        ```

??? abstract "91 | Bouncing Balls | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Bouncing_Balls.webm" type="video/webm">
        </video>
        
        *Real gravity physics with multiple balls bouncing off the strip's edges.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Count**: determines the number of active bouncing balls. |
        | **Speed** | `128` | **Gravity**: controls the acceleration and bounce height. |
        | **Palette** | — | **Supported**: Assigns colors to individual balls. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Bouncing Balls"
              effect_id: 91
        ```

??? abstract "2 | Breathe | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Breathe.webm" type="video/webm">
        </video>
        
        *Classic standby breathing LED effect with smooth brightness pulsing.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | — | **Not used**: Reserved for future refinements. |
        | **Speed** | `128` | **Breathe rate**: controls the speed of the expansion and contraction. |
        | **Palette** | — | **Supported**: Sets the color of the breathing pulse. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Breathe"
              effect_id: 2
        ```

??? abstract "28 | Chase | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Mid | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Chase.webm" type="video/webm">
        </video>
        
        *Moving dot with a fading trail that sweeps across the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `40` | **Trail length**: determines how long the tail persists. |
        | **Speed** | `110` | **Chase speed**: controls the velocity of the moving dot. |
        | **Palette** | — | **Supported**: Sets the color of the chase and trail. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Chase"
              effect_id: 28
        ```

??? abstract "54 | Chase multi | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Mid | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Chase_multi.webm" type="video/webm">
        </video>
        
        *Three-color chase animation with sub-pixel anti-aliasing for smooth motion.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `70` | **Spacing**: adjusts the distance between the chase segments. |
        | **Speed** | `60` | **Chase speed**: controls the velocity of the triple-chase. |
        | **Palette** | — | **Supported**: Defines the colors of the different segments. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Chase tri"
              effect_id: 54
        ```

??? abstract "8 | Colorloop | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Colorloop.webm" type="video/webm">
        </video>
        
        *Solid color cycling smoothly through the selected palette.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Saturation**: adjusts the color depth of the loop. |
        | **Speed** | `128` | **Cycle speed**: controls how fast colors rotate. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Colorloop"
              effect_id: 8
        ```

??? abstract "74 | Colortwinkle | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Colortwinkle.webm" type="video/webm">
        </video>
        
        *Magical fairy-dust twinkles that pop in and out across the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Spawn rate**: determines how many twinkles appear. |
        | **Speed** | `128` | **Fade speed**: controls how fast the sparks vanish. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Colortwinkle"
              effect_id: 74
        ```

??? abstract "63 | Colorwaves | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Colorwaves.webm" type="video/webm">
        </video>
        
        *Pride-style color waves with smooth breathing motion across the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Saturation**: adjusts the vibrancy of the waves. |
        | **Speed** | `128` | **Wave speed**: controls the motion velocity. |
        | **Palette** | — | **Supported**: Defaults to **Party** (8). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Colorwaves"
              effect_id: 63
        ```

??? abstract "18 | Dissolve | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Dissolve.webm" type="video/webm">
        </video>
        
        *Sequential random pixel fill and clear, creating a dissolve transition.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Change rate**: determines how many pixels change per tick. |
        | **Speed** | `128` | **Hold time**: controls the duration of the fill/clear states. |
        | **Palette** | — | **Supported**: Sets the dissolve colors. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Dissolve"
              effect_id: 18
        ```

??? abstract "96 | Drip | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Drip.webm" type="video/webm">
        </video>
        
        *Falling water drop physics with splashes and gravity-driven motion.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Drop size**: determines the volume of the water drops. |
        | **Speed** | `128` | **Gravity**: controls the fall velocity and splash height. |
        | **Palette** | — | **Supported**: Colorizes the water drops. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Drip"
              effect_id: 96
        ```

??? abstract "66 | Fire | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Fire.webm" type="video/webm">
        </video>
        
        *Realistic 1D fire simulation with heat tracking and spark generation.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `160` | **Sparking rate**: determines the frequency of new embers. |
        | **Speed** | `64` | **Cooling rate**: controls the flame height and dissipation. |
        | **Palette** | — | **Supported**: Defaults to **Fire** (5). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Fire"
              effect_id: 66
        ```

??? abstract "90 | Fireworks | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Fireworks.webm" type="video/webm">
        </video>
        
        *Exploding rocket simulation with launch and burst phases.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Explosion size**: determines the radius of the burst. |
        | **Speed** | `128` | **Launch frequency**: controls how often rockets fire. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Fireworks"
              effect_id: 90
        ```

??? abstract "110 | Flow | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Flow.webm" type="video/webm">
        </video>
        
        *Smooth moving color zones that overlap for fluid transitions.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Zone count**: determines the number of active color bands. |
        | **Speed** | `128` | **Flow speed**: controls the movement velocity. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Flow"
              effect_id: 110
        ```

??? abstract "87 | Glitter | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Glitter.webm" type="video/webm">
        </video>
        
        *Moving rainbow pattern with randomly spawning white sparkles.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Sparkle density**: determines the spark frequency. |
        | **Speed** | `128` | **Flow speed**: controls the background motion. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Glitter"
              effect_id: 87
        ```

??? abstract "100 | HeartBeat | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/HeartBeat.webm" type="video/webm">
        </video>
        
        *Anatomical heartbeat simulation with dual-beat rhythmic pulsing.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Pulse decay**: controls the dissipation of the beat surge. |
        | **Speed** | `128` | **BPM**: sets the frequency of the heartbeats. |
        | **Palette** | — | **Supported**: Assigns colors to the heart pulses. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "HeartBeat"
              effect_id: 100
        ```

??? abstract "64 | Juggle | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Juggle.webm" type="video/webm">
        </video>
        
        *Eight bouncing dots with trails that weave in and out of each other.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Trail length**: determines the persisting trail depth. |
        | **Speed** | `64` | **Movement speed**: controls the dot oscillation frequency. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Juggle"
              effect_id: 64
        ```

??? abstract "76 | Meteor | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Meteor.webm" type="video/webm">
        </video>
        
        *Meteor with random decay trail that sweeps across the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Trail length**: determines the particle persistence. |
        | **Speed** | `128` | **Fall speed**: controls the movement velocity. |
        | **Palette** | — | **Supported**: Colors the meteor and its tail. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Meteor"
              effect_id: 76
        ```

??? abstract "25 | Multi Strobe | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Multi_Strobe.webm" type="video/webm">
        </video>
        
        *Multiple strobe pulses in rapid sequence for high-intensity visuals.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Burst count**: determines the number of strobe flashes per cycle. |
        | **Speed** | `128` | **Strobe rate**: controls the flash frequency. |
        | **Palette** | — | **Supported**: Colors the strobe bursts. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Multi Strobe"
              effect_id: 25
        ```
??? abstract "107 | Noise Pal | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Noise_Pal.webm" type="video/webm">
        </video>
        
        *Perlin noise driven color movement that creates organic, flowing light patterns.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Zoom**: adjusts the scale/resolution of the noise pattern. |
        | **Speed** | `128` | **Drift speed**: controls the velocity of the color movement. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Noise Pal"
              effect_id: 107
        ```

??? abstract "101 | Ocean | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Ocean.webm" type="video/webm">
        </video>
        
        *Gentle ocean waves (Pacifica simulation) with overlapping light layers.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Zoom/Scale**: adjusts the breadth of the wave movements. |
        | **Speed** | `128` | **Wave speed**: controls the velocity of the ocean surge. |
        | **Palette** | — | **Not Supported**: Forced to the deep sea Ocean palette. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Ocean"
              effect_id: 101
        ```

??? abstract "98 | Percent | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Percent.webm" type="video/webm">
        </video>
        
        *Percent-based fill (Progress Bar) for visual status tracking.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Percent fill**: determines the completion level (0-255 map). |
        | **Speed** | `128` | **Smoothness**: controls the transition velocity between values. |
        | **Palette** | — | **Supported**: Colors the fill bar. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Percent"
              effect_id: 98
        ```

??? abstract "97 | Plasma | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Plasma.webm" type="video/webm">
        </video>
        
        *Multi-layer Perlin noise plasma that generates complex, morphing light fields.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Frequency**: adjusts the density of the plasma nodes. |
        | **Speed** | `128` | **Morph speed**: controls how fast the plasma fluctuates. |
        | **Palette** | — | **Supported**: Defaults to **Party** (8). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Plasma"
              effect_id: 97
        ```

??? abstract "95 | Popcorn | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Mid | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Popcorn.webm" type="video/webm">
        </video>
        
        *Bouncing particles simulation with energy dissipation and floor collisions.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Particle count**: determines the number of active popcorn grains. |
        | **Speed** | `128` | **Bounciness**: controls the kinetic energy retention on impact. |
        | **Palette** | — | **Supported**: Colors the individual particles. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Popcorn"
              effect_id: 95
        ```

??? abstract "9 | Rainbow | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Rainbow.webm" type="video/webm">
        </video>
        
        *Classic full HSV spectrum moving across the strip for a vibrant light flow.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Zoom/Density**: adjusts the breadth of the rainbow bands. |
        | **Speed** | `128` | **Flow speed**: controls the velocity of the spectrum movement. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Rainbow"
              effect_id: 9
        ```

??? abstract "79 | Ripple | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Ripple.webm" type="video/webm">
        </video>
        
        *Expanding ripple waves that propagate from random origins across the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Wave strength**: determines the ripple amplitude. |
        | **Speed** | `128` | **Propagation**: controls how fast waves expand. |
        | **Palette** | — | **Supported**: Defaults to **Rainbow** (4). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Ripple"
              effect_id: 79
        ```

??? abstract "52 | Running Dual | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Running_Dual.webm" type="video/webm">
        </video>
        
        *Two running light trails originating from the center and sweeping outward.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Spacing**: adjusts the distance between trails. |
        | **Speed** | `128` | **Run speed**: controls the movement velocity. |
        | **Palette** | — | **Supported**: Defaults to **Sakura** (13). |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Running Dual"
              effect_id: 52
        ```

??? abstract "15 | Running lights | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Running_lights.webm" type="video/webm">
        </video>
        
        *Moving light trails with subtle decay, creating a flowing rhythmic effect.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Trail spacing**: adjusts the density of the light peaks. |
        | **Speed** | `128` | **Run speed**: controls the motion velocity. |
        | **Palette** | — | **Supported**: Sets the color of the trails. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Running lights"
              effect_id: 15
        ```

??? abstract "16 | Saw | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Saw.webm" type="video/webm">
        </video>
        
        *Sawtooth wave color movement with sharp ramps and snappy transitions.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Wave count**: determines the density of the sawtooth pattern. |
        | **Speed** | `128` | **Speed**: controls the movement velocity. |
        | **Palette** | — | **Supported**: Colors the sawtooth peaks. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Saw"
              effect_id: 16
        ```

??? abstract "40 | Scanner | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Scanner.webm" type="video/webm">
        </video>
        
        *Classic Larson Scanner (Cylon Eye) ping-ponging across the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Trail length**: determines the persisting trail depth. |
        | **Speed** | `128` | **Scan speed**: controls the movement frequency. |
        | **Palette** | — | **Supported**: Sets the scanner color. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Scanner"
              effect_id: 40
        ```

??? abstract "60 | Scanner Dual | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Scanner_Dual.webm" type="video/webm">
        </video>
        
        *Two scanners synchronizing and meeting in the center of the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Trail length**: determines the trail persistence. |
        | **Speed** | `128` | **Scan speed**: controls the meeting frequency. |
        | **Palette** | — | **Supported**: Sets the color of the dual scanners. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Scanner Dual"
              effect_id: 60
        ```

??? abstract "20 | Sparkle | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Sparkle.webm" type="video/webm">
        </video>
        
        *Randomly flashing pixels that pop in and out across the solid strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Sparkle density**: determines how many pixels flash at once. |
        | **Speed** | `128` | **Cycle speed**: controls how fast sparks update. |
        | **Palette** | — | **Supported**: Sets the sparkle colors. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Sparkle"
              effect_id: 20
        ```

??? abstract "22 | Sparkle + | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Sparkle.webm" type="video/webm">
        </video>
        
        *Intense high-frequency sparkles with overlapping flash cycles.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Density**: determines the frequency of new light births. |
        | **Speed** | `128` | **Flash rate**: controls the shutter speed of the sparkles. |
        | **Palette** | — | **Supported**: Colors the intense flash events. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Sparkle +"
              effect_id: 22
        ```

??? abstract "21 | Sparkle Dark | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Sparkle_Dark.webm" type="video/webm">
        </video>
        
        *Inverse sparkling where random pixels turn off briefly against a solid background.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Density**: determines the number of dark nodes. |
        | **Speed** | `128` | **Blink rate**: controls the frequency of dark events. |
        | **Palette** | — | **Supported**: Sets the background solid color. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Sparkle Dark"
              effect_id: 21
        ```

??? abstract "0 | Static | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Static.webm" type="video/webm">
        </video>
        
        *Solid color or stationary palette mapping for constant illumination.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | — | **Not used**: Reserved for future refinements. |
        | **Speed** | — | **Not used**: Reserved for future refinements. |
        | **Palette** | — | **Supported**: Maps a static gradient across the strip. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Static"
              effect_id: 0
        ```

??? abstract "23 | Strobe | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: High | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Strobe.webm" type="video/webm">
        </video>
        
        *Rapid full-strip high-frequency flashing for stroboscopic effects.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | — | **Not used**: Reserved for future refinements. |
        | **Speed** | `128` | **Strobe rate**: controls the flash frequency. |
        | **Palette** | — | **Supported**: Colors the strobe bursts. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Strobe"
              effect_id: 23
        ```

??? abstract "24 | Strobe Rainbow | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: High | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Strobe_Rainbow.webm" type="video/webm">
        </video>
        
        *Strobe flashing with automatic color cycling across the palette.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Color speed**: controls how fast colors cycle during the strobe. |
        | **Speed** | `128` | **Strobe rate**: controls the flash frequency. |
        | **Palette** | — | **Supported**: Defines the strobe color cycle. |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Strobe Rainbow"
              effect_id: 24
        ```

??? abstract "104 | Sunrise | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Sunrise.webm" type="video/webm">
        </video>
        
        *Gradual brightness and color increase simulation for alarm/wake-up light.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `128` | **Not used**: Reserved for future refinements. |
        | **Speed** | `60` | **Duration**: controls the length of the sunrise transition. |
        | **Palette** | — | **Supported**: Defaults to **HeatColors** (12). |
        | **Sequencer Triggers** | — | **Not Supported**: Only `on_start` and `on_complete` are available. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Sunrise"
              effect_id: 104
        ```

??? abstract "6 | Sweep | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Sweep.webm" type="video/webm">
        </video>
        
        *Ping-pong wipe animation that bounces the light front and back.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | — | **Not used**: Reserved for future refinements. |
        | **Speed** | `128` | **Sweep speed**: controls the movement velocity. |
        | **Palette** | — | **Supported**: Colors the sweeping light. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Sweep"
              effect_id: 6
        ```

??? abstract "3 | Wipe | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Wipe.webm" type="video/webm">
        </video>
        
        *Linear color wipe from start to end of the strip.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | — | **Not used**: Reserved for future refinements. |
        | **Speed** | `128` | **Wipe speed**: controls the transition velocity. |
        | **Palette** | — | **Supported**: Colors the wipe animation. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Wipe"
              effect_id: 3
        ```

??? abstract "4 | Wipe Random | <span class='extra-info'>:material-tag-outline: 1.0.0 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Wipe_Random.webm" type="video/webm">
        </video>
        
        *Linear wipe with random color changes on every pass.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | — | **Not used**: Reserved for future refinements. |
        | **Speed** | `128` | **Wipe speed**: controls the transition velocity. |
        | **Palette** | — | **Supported**: Defines the random color range. |
        | **Sequencer Triggers** | — | **Supported**: Position-based triggers (`on_reach`, `on_pixel_num`). |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Wipe Random"
              effect_id: 4
        ```

### A few notes on the credits:
*   **ChimeraFX:** Custom effects developed specifically for this component by Federico Leoni.
*   **Aircoookie:** Lead developer of WLED. Most core effect logic is derived from his work.
*   **Mark Kriegsman:** Godfather of high-quality LED math. Responsible for Fire, Juggle, and many FastLED classics.
*   **Tweaking4All:** Creator of the iconic Bouncing Balls physics logic.

---

## Palettes

You can assign any of these palettes to compatible effects using the `palette` selector.

| ID | Palette Name | Description | Preview |
|:---|:---|:---|:---|
| 0 | **Default** | Uses the effect's hardcoded colors. | |
| 1 | **Aurora** | Northern lights colors. | ![Aurora](/ChimeraFX/assets/palettes/Aurora.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 17 | **Christmas** | Red and green holiday mix. | ![Christmas](/ChimeraFX/assets/palettes/Christmas.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 15 | **Cyberpunk** | Neon pink, blue, and purple. | ![Cyberpunk](/ChimeraFX/assets/palettes/Cyberpunk.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 22 | **Fairy** | Magical pinks and purples. | ![Fairy](/ChimeraFX/assets/palettes/Fairy.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 5 | **Fire** | Intense reds, oranges, and yellows. | ![Fire](/ChimeraFX/assets/palettes/Fire.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 2 | **Forest** | Earth greens, browns, and mossy tones. | ![Forest](/ChimeraFX/assets/palettes/Forest.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 3 | **Halloween** | Orange and purple mix. | ![Halloween](/ChimeraFX/assets/palettes/Halloween.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 12 | **HeatColors** | Traditional heatmap colors. | ![HeatColors](/ChimeraFX/assets/palettes/HeatColors.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 7 | **Ice** | Cool whites, blues, and cyans. | ![Ice](/ChimeraFX/assets/palettes/Ice.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 19 | **Matrix** | Digital rain greens. | ![Matrix](/ChimeraFX/assets/palettes/Matrix.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 11 | **Ocean** | Deep blues, cyans, and white crests. | ![Ocean](/ChimeraFX/assets/palettes/Ocean.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 16 | **OrangeTeal** | Modern cinematic contrast. | ![OrangeTeal](/ChimeraFX/assets/palettes/OrangeTeal.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 8 | **Party** | Vibrant high-contrast mixed colors. | ![Party](/ChimeraFX/assets/palettes/Party.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 10 | **Pastel** | Soft, desaturated soothing colors. | ![Pastel](/ChimeraFX/assets/palettes/Pastel.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 4 | **Rainbow** | Full HSV spectrum cycle. | ![Rainbow](/ChimeraFX/assets/palettes/Rainbow.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 18 | **RedBlue** | Simple red and blue gradient. | ![RedBlue](/ChimeraFX/assets/palettes/RedBlue.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 14 | **Rivendell** | Fantasy forest greens and blues. | ![Rivendell](/ChimeraFX/assets/palettes/Rivendell.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 13 | **Sakura** | Pink and white cherry blossom tones. | ![Sakura](/ChimeraFX/assets/palettes/Sakura.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 254 | **Smart Random** | Dynamic, intelligent color combinations. | ![Smart Random](/ChimeraFX/assets/palettes/Smart_Random.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 21 | **Solid** | Solid color (uses primary color). | ![Solid](/ChimeraFX/assets/palettes/Solid.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 20 | **SunnyGold** | Bright gold and yellow. | ![SunnyGold](/ChimeraFX/assets/palettes/SunnyGold.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 6 | **Sunset** | Purple, red, orange, and yellow gradients. | ![Sunset](/ChimeraFX/assets/palettes/Sunset.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |
| 9 | **Twilight** | Dusk blues and purples. | ![Twilight](/ChimeraFX/assets/palettes/Twilight.png){ style="width: 150px; height: 8px !important; border-radius: 2px; border: 1px solid #333; display: block; object-fit: fill;" } |


## Intro and Outro Animations

These short animations play once when the light is turned ON or OFF.

??? abstract "0 | None | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/none_io.webm" type="video/webm">
        </video>
    *Standard behavior (Main effect starts immediately).*

??? abstract "3 | Center | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/center_io.webm" type="video/webm">
        </video>
    *Wipe from center outwards (or inwards if reversed).*

??? abstract "10 | Construct / Dismantle | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Mid | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/assembly.webm" type="video/webm">
        </video>
    *Simulates kinetic physical structure. **Construct** features free-falling blocks that stack to build the strip during the intro, while **Dismantle** breaks the light apart from the top down, with segments peeling off and falling away during the outro.*

??? abstract "14 | Crystallize / Erode | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *Geological nucleation transition. **Crystallize** grow seeds of light outward into a solid floor during the intro. **Erode** causes the light to fragment and dissolve at the edges, slowly consuming the strip from within.*

??? abstract "15 | Deep Breathe / Exhale | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *A slow, organic brightness ramp. **Deep Breathe** rises from darkness with a subtle triangular shimmer during the intro. **Exhale** provides a natural, lung-like curve that gently releases the light into darkness.*

??? abstract "9 | Dropping / Emptying | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Dropping_Fill.webm" type="video/webm">
        </video>
    *Simulates gravity-based physics. **Dropping** features falling droplets that fill the strip during the intro, while **Emptying** clears the light with rising air bubbles and surface highlights during the outro.*

??? abstract "21 | Eclipse | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *A masterclass in negative space. **Eclipse** sweeps a deep, smooth shadow across the strip, modulating the brightness in a calming reverse-kinetic motion. Perfect for transitions into solid, ambient colors.*

??? abstract "2 | Fade | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/fade_io.webm" type="video/webm">
        </video>
    *Smooth brightness fade-in.*

??? abstract "22 | Gas Discharge | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *The strip stutters to life with high-frequency flashes and crackling brightness surges, simulating an old neon tube or high-pressure lamp striking before it settles into stable illumination.*

??? abstract "4 | Glitter | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/glitter_io.webm" type="video/webm">
        </video>
    *Random pixels sparkle as brightness increases.*

??? abstract "23 | Harmonic Settle | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *A highly physical, spring-loaded transition. The light surges forward, overshoots the target brightness, and bounces back and forth with decaying momentum until it smoothly settles into its final solid state.*

??? abstract "11 | Inertia Sweep / Decelerate | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *A precisely-eased transition that sweeps brightness in a single pass. **Inertia Sweep** features a hot leading edge that pulses ahead during the intro, while **Decelerate** provides a smooth, momentum-based fade to black during the outro.*

??? abstract "20 | Interference / Interference Fade | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: High | :material-palette:</span>"
    *A high-frequency shimmering transition. **Interference** uses colliding light waves for a vibrant startup. **Interference Fade** dissolves the main effect into a high-energy field that quickly sinks into darkness.*

??? abstract "24 | Lithograph | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *A precise, technical construction. A razor-sharp bright cursor 'prints' the light onto the strip line by line with mechanical precision, constructng a solid trail until the entire segment is fully revealed.*

??? abstract "16 | Moiré Shift / Moiré Fade | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Mid | :material-palette:</span>"
    *A high-frequency interference transition. **Moiré Shift** builds the floor through breathing shadows during the intro. **Moiré Fade** uses shifting waves to break up the solid color before dissolving into black.*

??? abstract "6 | Morse Code | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/morse_io.webm" type="video/webm">
        </video>
    *Flashes "ON" in Morse code during the intro and "OFF" during the outro.*

??? abstract "8 | Pressurize / Drain | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Mid | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Hydro-Pulse.webm" type="video/webm">
        </video>
    *Simulates fluid dynamics within the strip. **Pressurize** surges the light forward during the intro, while **Drain** pulls it back and leaves droplets during the outro.*

??? abstract "7 | Quadrant | <span class='extra-info'>:material-tag-outline: 1.3.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/quadrant_io.webm" type="video/webm">
        </video>
    *Splits the strip into 4 wings. Converges from quadrant edges to centers during intro, and clears from centers outwards during outro.*

??? abstract "17 | Resonance / Resonance Fade | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *A liquid-reactive transition. **Resonance** surges across the strip with decaying ripples during the intro. **Resonance Fade** provides a rhythmic, decaying drain that rhythmically pulls the light away during the outro.*

??? abstract "12 | Sonar Reveal / Sonar Fade | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *A radar-like scanning transition. **Sonar Reveal** sweeps back and forth, gradually resolving the brightness floor during the intro. **Sonar Fade** uses a retreating scan to systematically dim the strip into darkness during the outro.*

??? abstract "19 | Stellar Dust / Stellar Fade | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette:</span>"
    *A randomized phase-pulsed transition. **Stellar Dust** awakens pixels as breathing stars that stabilize into a floor during the intro. **Stellar Fade** cause the pixels to drift back into random twinkling before extinguishing.*

??? abstract "18 | Telemetry / Telemetry Fade | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *A digital data-stream transition. **Telemetry** transmits sharp data-bursts to construct the floor during the intro. **Telemetry Fade** uses softened trail-retraction to "de-materialize" the strip into nothingness.*

??? abstract "5 | Twin Pulse | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/twin_io.webm" type="video/webm">
        </video>
    *Symmetrical dual-cursor pulses that race across the strip, leading a solid color wipe or erasing light during the outro.*

??? abstract "13 | Venetian / Close Blinds | <span class='extra-info'>:material-tag-outline: 1.4.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    *Architectural shutter transition. **Venetian** opens the light in two alternating stages during the intro. **Close Blinds** reverses the logic, snapping the light shut in alternating patterns for a clean exit.*

??? abstract "1 | Wipe | <span class='extra-info'>:material-tag-outline: 1.2.1 | :material-speedometer: Low | :material-palette: | :material-bullseye-arrow:</span>"
    <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/wipe_io.webm" type="video/webm">
        </video>
    *Linear wipe from start to end (respects Mirror).*



### Transition Behavior
When the Intro Duration ends, the Intro Effect will **Dissolve** (Soft Fairy Dust) into the Main Effect over 1.5 seconds. This creates a seamless, premium startup experience.

**Note:** The intro animation runs only when the light is switched from Off to On. If you change the main effect while the light is already on, the intro will not play again. It will only re-trigger the next time the light is toggled on.

