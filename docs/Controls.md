# Controls Guide

`ChimeraFX` exposes various control entities, such as sliders, dropdowns, and switches, allowing you to fine-tune each effect to your preference. While the controls are optional, they are **highly recommended** if you want to manage effects from Home Assistant. Without them, effects will run using their default parameters.

## Controller Logic

The effects always listen to specific global variables, such as numbers, selects, and switches, to adjust their behavior in real time.
A `cfx_light` entity generates all the controls for you automatically. You do not need to declare anything else for that: it creates the necessary entities, such as Speed, Intensity, Palette, Mirror, Intro, Outro, and more, using core `number`, `select`, and `switch` components and links them to your light automatically.

### Generated Controls
The component creates the following entities based on your light's name (for example, `LED Strip`):

- `LED Strip Speed` (Number)
- `LED Strip Intensity` (Number)
- `LED Strip Palette` (Select)
- `LED Strip Mirror` (Switch)
- `LED Strip Intro` (Select)
- `LED Strip Outro` (Select)
- `LED Strip In/Out Duration` (Number)
- `LED Strip Force White` (Switch, RGBW/WRGB only)
- `LED Strip Autotune` (Switch)
- `LED Strip Debug` (Switch)

### Exclusion Options
Not every user wants every control. If you want to keep things minimal, you can exclude individual controls or disable the whole control set.
You can achieve that in two ways under your `cfx_light` entity:

- Remove specific controls with the `ctrl_exclude` option by listing the IDs you want to exclude:

## Overview of Controls

### Controls IDs List
| ID | Control |
|:---|:---|
| 1 | Speed |
| 2 | Intensity |
| 3 | Palette |
| 4 | Mirror |
| 5 | Intro Effects (Intro, Outro and Duration) |
| 6 | HA Events |
| 7 | Autotune |
| 8 | Force White |
| 9 | Debug (Diagnostic) |

### ID 1: Speed
Controls the speed of the effect. The higher the value, the faster the effect will run. Range: `0-255` *(default: 128 when Autotune is disabled)*.

### ID 2: Intensity
Controls the intensity of the effect. Depending on the effect, this slider may influence parameters such as saturation, pattern length, and more. Range: `0-255` *(default: 128 when Autotune is disabled)*.

### ID 3: Palette
Controls the palette used by the effect. A palette is the set of colors the effect draws from, and each effect has a default palette that this control can override. Some effects, such as Fire, Fire Dual, and Ocean, do not allow palette changes; this is intentional to preserve their visual character. The list of selectable palettes can be found in the [Palettes](Effects-Library.md#palettes) section.

### ID 4: Mirror
Controls the direction of an effect, from start-to-finish or finish-to-start. This is useful if you cannot physically invert the strip. It also affects Intro and Outro animations.

### ID 5: Intro and Outro Animation
A group of three controls: Intro style, Outro style, and In/Out Duration (`0.5-10.0` seconds). The Intro uses the average color of the effect that will follow, while the Outro inherits colors from the active effect palette instead of using a default solid color. More details can be found in the [Intro and Outro Animations](Effects-Library.md#intro-and-outro-animations) section.

### ID 6: Home Assistant Events
Lets you use Home Assistant as the orchestrator for more complex animation flows through special event triggers (`cfx_begin`, `cfx_start`, `cfx_reach`, `cfx_stop`, and `cfx_complete`). More details can be found in the [Sequencer](cfx_sequence.md) section.

### ID 7: Autotune
Enables or disables **Intelligent Autotune**. When enabled, the effect will automatically snap its Speed, Intensity, and Palette to the recommended defaults immediately upon being selected.

- **Intelligent Yield:** If you manually adjust a slider or pick a different palette while Autotune is active, the system detects your intervention and automatically toggles Autotune **OFF**, giving you full manual control instantly.

- **Manual Reset:** If you get lost in manual tweaks, simply flip Autotune back **ON** to snap everything back to the factory defaults.

### ID 8: Force White
Available only on RGBW/WRGB lights with a dedicated white channel. When enabled, ChimeraFX moves the shared part of eligible white and pastel RGB colors into the white LED channel, and it automatically stays out of palettes that do not meaningfully use those tones, so neutral whites use the native white emitter without contaminating saturated RGB effects.

### ID 9: Debug
Enables or disables runtime debug logging. This switch is available under the Diagnostic tab in Home Assistant. Useful for troubleshooting issues by providing detailed output in the ESPHome logs. **Defaults to OFF.** Enabling debug mode may slightly impact animation smoothness due to logging overhead. See [Troubleshooting](Troubleshooting.md) for more details.

#### Examples
Example of how to remove specific controls or all controls:

```yaml
light:
  - platform: cfx_light
    name: "LED Strip"
    id: led_strip
    pin: GPIO16
    num_leds: 120
    chipset: WS2812X
    ctrl_exclude: [3,4,5,6,7,8,9] # Exclude Palette, Mirror, Intro/Outro, 
                                  # HA Events, Autotune, Force White, Debug
```

- Or use `controls: false` to disable all controls at once.

```yaml
light:
  - platform: cfx_light
    name: "LED Strip"
    id: led_strip
    pin: GPIO16
    num_leds: 120
    chipset: WS2812X
    controls: false # Disable all controls
```