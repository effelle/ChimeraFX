import re
import os

path = r"c:\Users\effel\OneDrive\Desktop\Antigravity_projects\docs\Effects-Library.md"

with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Remove duplicate Sonar Reveal blocks
# 2. Fix Venetian section
# 3. Remove Moiré Shift remnants

# Correct block for Sonar Reveal
sonar_correct = """??? abstract "172 | Sonar Reveal | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Sonar_Reveal.webm" type="video/webm">
        </video>
        *A radar-like scanning beam sweeps back and forth four times, each pass permanently lifting the brightness floor — like sonar gradually resolving a picture from nothing to full presence.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Not used**: Monochromatic — no blur. |
        | **Speed** | `1` | **Duration**: slider maps to intro/outro duration. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Triggers** | — | `cfx_begin`, `cfx_start`, `cfx_reach`, `cfx_stop`, `cfx_complete`. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Sonar Reveal"
              effect_id: 172
        ```"""

# Correct block for Venetian
venetian_correct = """??? abstract "173 | Venetian | <span class='extra-info'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"

    === "🎬 Preview"
        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">
            <source src="/ChimeraFX/assets/effects/Venetian.webm" type="video/webm">
        </video>
        *Like venetian blinds opening in two stages — even pixels rotate into light first, then odd pixels follow in a second sweep. Clean, architectural, precise.*

    === "⚙️ Controls"
        | Parameter | Autotune | Function Description |
        | :--- | :--: | :--- |
        | **Intensity** | `1` | **Not used**: Monochromatic — no blur. |
        | **Speed** | `1` | **Duration**: slider maps to intro/outro duration. |
        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |
        | **Triggers** | — | `cfx_begin`, `cfx_start`, `cfx_reach`, `cfx_stop`, `cfx_complete`. |

    === "💻 Config"
        **YAML Setup / Custom Preset:**
        *(Optional if `all_effects: true`)*
        ```yaml
          - addressable_cfx:
              name: "Venetian"
              effect_id: 173
        ```"""

# Find the start of 172 and the end of 173/176 remnant
# We search for the pattern starting from 172 up to 177
pattern = r'\?\?\? abstract "172 \| Sonar Reveal.*?\?\?\? abstract "177 \| Resonance Fill'
replacement = sonar_correct + "\n\n" + venetian_correct + "\n\n??? abstract \"177 | Resonance Fill"

new_content = re.sub(pattern, replacement, content, flags=re.DOTALL)

with open(path, 'w', encoding='utf-8') as f:
    f.write(new_content)

print("Documentation cleaned successfully.")
