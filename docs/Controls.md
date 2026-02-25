# Controls Guide

Now that your effects are up and running, you’ll likely want to customize them. `ChimeraFX` can expose various control entities—such as sliders, dropdowns, and switches—allowing you to fine-tune each effect to your preference.
While the `cfx_control` component is optional, it is **highly recommended** if you want to manage effects directly from Home Assistant. Without it, effects will run using their default parameters.

## Controller Logic

The `addressable_cfx` effect listens to specific global variables (numbers, selects and switches) to adjust its behavior in real-time.
To generate controls automatically, use the `cfx_control` component. You need to declare it under the `cfx_effect` component, and it will create all the necessary entities (Speed, Intensity, Palette, Mirror, Intro Effects and Timer) for you, using core `number`, `select` and `switch` components, and link them to your light automatically.

```yaml
cfx_effect:
  cfx_control:
    - id: my_cfx_controller   # The ID of the controller. Customizable
      name: "LED Strip"       # The prefix name of the controller. Customizable
      light_id: led_strip     # The ID of the light you want to manage 
    

      # Optional: Exclude controls you don't need
      # exclude: [5, 6] # This example will exclude Intro Effects and Timer
```

**Note:** The `light_id` parameter is the magic sauce that links the controller to a specific light. This is necessary because the `addressable_cfx` effect needs to know which light it is controlling.

### Generated Controls
The component creates the following entities based on your controller name (e.g., `LED Strip`):

- `LED Strip Speed` (Number)
- `LED Strip Intensity` (Number)
- `LED Strip Palette` (Select)
- `LED Strip Mirror` (Switch)
- `LED Strip Intro` (Select)
- `LED Strip Use Palette` (Switch)
- `LED Strip Intro Duration` (Number)
- `LED Strip Timer` (Number)
- `LED Strip Debug` (Switch)
- `LED Strip Autotune` (Switch)

### Exclusion Options
If you want to keep your dashboard clean, you can exclude specific controls by adding their IDs to the `exclude` list (e.g., `exclude: [5, 6]`).

| ID | Control |
|:---|:---|
| 1 | Speed |
| 2 | Intensity |
| 3 | Palette |
| 4 | Mirror |
| 5 | Intro Effects (Intro, Duration, Use Palette) |
| 6 | Timer |
| 7 | Autotune |
| 9 | Debug (Diagnostic) |

That’s it! The effects will automatically detect this controller and respect **any** exclusions you have configured.

---

## Advanced: Multiple Strips

### Option 1: Separate Controls (Independent)
If you want independent control for each strip (e.g., Roof vs Desk), create separate controllers:

```yaml
cfx_effect:
  cfx_control:
    - id: my_cfx_controller   # The ID of the first controller. Customizable
      name: "LED Strip"       # The prefix name of the first control set. Customizable
      light_id: led_strip     # The ID of the first light you want to manage

    - id: desk_controller     # The ID of the second controller. Customizable
      name: "Desk Lights"     # The prefix name of the second control set. Customizable
      light_id: led_strip_2   # The ID of the second light you want to manage
```

### Option 2: Unified Control (Grouped)
If you want a **single set of controls** to operate multiple lights simultaneously (e.g., "Global Control"), you can pass a list of light IDs:

```yaml
cfx_effect:
  cfx_control:
    - id: global_controller   # The ID of the controller. Customizable
      name: "Global"          # The prefix name of the controller. Customizable
      light_id: [led_strip_1, led_strip_2] # IDs of the lights in a list format
```
This generates only one set of entities (e.g. `Global Speed`) that updates all listed lights at once.

## Overview of Controls

### ID 1: Speed
Control slider to manage the speed of the effect. The higher the value, the faster the effect will run. Range (0-255)

### ID 2: Intensity
Control slider to manage the intensity of the effect. Depending on the effect, the slider can manage different parameters like saturation, pattern length, etc. Range (0-255)

### ID 3: Palette
Controls the palette used by the effect. A palette is a set of colors that are used by the effect, and every effect has its default palette that can be overridden by this control. Some effects like Fire, Fire Dual and Ocean doesn't allow to change the palette this was a deliberate choice by me to make them more realistic. List of selectable palettes can be found in the [Palettes](Effects-Library.md#palettes) section.

### ID 4: Mirror
Controls the starting point of an effect start-to-finish or finish-to-start. Useful if you can't physically invert the strip. Affects Intro Animation too.

### ID 5: Intro and Outro Animation
A group of three controls: Intro Style (None, Wipe, Fade, Center, Glitter), Intro Duration (0.5 - 10.0 seconds) and Intro Palette Support. The latter allows the intro animation to inherit the colors of the active effect palette rather than using a default solid color. More details can be found in the [Intro Animations](Effects-Library.md#intro-animations) section.

### ID 6: Timer
Controls how long a light stays on. From 0 (timer OFF) to 360 minutes.

### ID 7: Autotune
Enables or disables **Intelligent Autotune**. When enabled, the effect will automatically snap its Speed, Intensity, and Palette to the recommended defaults immediately upon being selected.

- **Intelligent Yield:** If you manually adjust a slider or pick a different palette while Autotune is active, the system detects your intervention and automatically toggles Autotune **OFF**, giving you full manual control instantly.
- **Default Behavior:** If the `cfx_control` component is not used (or Autotune is excluded), the engine defaults to Autotune **ON** to ensure every effect looks its best without manual setup.
- **Manual Reset:** If you get lost in manual tweaks, simply flip Autotune back **ON** to snap everything back to the factory defaults. 
- **Presets:** Autotune can be overridden by explicit values stored in [Presets](Presets-and-States.md).

### ID 9: Debug
Enables or disables runtime debug logging. This switch is available under the Diagnostic tab in Home Assistant. Useful for troubleshooting issues by providing detailed output in the ESPHome logs. **Defaults to OFF.** Enabling debug mode may slightly impact animation smoothness due to logging overhead.
