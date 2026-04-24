import re

path = r"c:\Users\effel\OneDrive\Desktop\Antigravity_projects\docs\Effects-Library.md"

with open(path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

# Correct blocks as lists of lines
sonar_block = [
    '??? abstract "172 | Sonar Reveal | <span class=\'extra-info\'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"\n',
    '\n',
    '    === "🎬 Preview"\n',
    '        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">\n',
    '            <source src="/ChimeraFX/assets/effects/Sonar_Reveal.webm" type="video/webm">\n',
    '        </video>\n',
    '        *A radar-like scanning beam sweeps back and forth four times, each pass permanently lifting the brightness floor — like sonar gradually resolving a picture from nothing to full presence.*\n',
    '\n',
    '    === "⚙️ Controls"\n',
    '        | Parameter | Autotune | Function Description |\n',
    '        | :--- | :--: | :--- |\n',
    '        | **Intensity** | `1` | **Not used**: Monochromatic — no blur. |\n',
    '        | **Speed** | `1` | **Duration**: slider maps to intro/outro duration. |\n',
    '        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |\n',
    '        | **Triggers** | — | `cfx_begin`, `cfx_start`, `cfx_reach`, `cfx_stop`, `cfx_complete`. |\n',
    '\n',
    '    === "💻 Config"\n',
    '        **YAML Setup / Custom Preset:**\n',
    '        *(Optional if `all_effects: true`)*\n',
    '        ```yaml\n',
    '          - addressable_cfx:\n',
    '              name: "Sonar Reveal"\n',
    '              effect_id: 172\n',
    '        ```\n'
]

venetian_block = [
    '??? abstract "173 | Venetian | <span class=\'extra-info\'>:material-tag-outline: 1.4.1| :material-speedometer: Low</span>"\n',
    '\n',
    '    === "🎬 Preview"\n',
    '        <video loop muted playsinline autoplay preload="none" style="width: 100%; border-radius: 4px; margin-top: 10px;">\n',
    '            <source src="/ChimeraFX/assets/effects/Venetian.webm" type="video/webm">\n',
    '        </video>\n',
    '        *Like venetian blinds opening in two stages — even pixels rotate into light first, then odd pixels follow in a second sweep. Clean, architectural, precise.*\n',
    '\n',
    '    === "⚙️ Controls"\n',
    '        | Parameter | Autotune | Function Description |\n',
    '        | :--- | :--: | :--- |\n',
    '        | **Intensity** | `1` | **Not used**: Monochromatic — no blur. |\n',
    '        | **Speed** | `1` | **Duration**: slider maps to intro/outro duration. |\n',
    '        | **Palette** | — | **Not Supported**: Forced to the primary solid color. |\n',
    '        | **Triggers** | — | `cfx_begin`, `cfx_start`, `cfx_reach`, `cfx_stop`, `cfx_complete`. |\n',
    '\n',
    '    === "💻 Config"\n',
    '        **YAML Setup / Custom Preset:**\n',
    '        *(Optional if `all_effects: true`)*\n',
    '        ```yaml\n',
    '          - addressable_cfx:\n',
    '              name: "Venetian"\n',
    '              effect_id: 173\n',
    '        ```\n'
]

# Find indices
try:
    idx_172 = next(i for i, line in enumerate(lines) if '172 | Sonar Reveal' in line)
    idx_177 = next(i for i, line in enumerate(lines) if '177 | Resonance Fill' in line)
    
    # Replace the whole range
    new_lines = lines[:idx_172] + sonar_block + ['\n'] + venetian_block + ['\n'] + lines[idx_177:]
    
    with open(path, 'w', encoding='utf-8') as f:
        f.writelines(new_lines)
    print("Documentation cleaned successfully (Stage 2).")
except Exception as e:
    print(f"Error: {e}")
