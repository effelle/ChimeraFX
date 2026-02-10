# Controls Guide

Now you have the effects up and running, but how about controlling them? And what can you control? 
`ChimeraFX` can create inputs (sliders, dropdowns, switches) to help you tweak the effect for your liking. The `cfx_control` component is optional and only needed (but strongly suggested) if you want to control the effect from Home Assistant or another controller. Without it you will still be able to use the effects at default values.

## Controller Logic

The `addressable_cfx` effect listens to specific global variables (numbers, selects and switches) to adjust its behavior in real-time.

To generate controls automatically, use the `cfx_control` component. You need to declare it under the `cfx_effect` component, and it will create all the necessary entities (Speed, Intensity, Palette, Mirror, Intro Effects and Timer) for you, using core `number`, `select` and `switch` components, and link them to your light automatically.

```yaml
cfx_effect:
  cfx_control:
    - id: my_cfx_controller # The ID of the controller generator
      name: "LED Strip"
      light_id: led_strip # The ID of the light you want to control

      # Optional: Exclude controls you don't need
      # exclude: [5, 6] # This example will exclude Intro Effects and Timer
```

**Note:** The `light_id` parameter is the magic sauce that links the controller to a specific light. This is necessary because the `addressable_cfx` effect needs to know which light it is controlling.

### Generated Controls
The component creates the following entities based on your controller name (e.g., `LED Strip`):

- `LED Strip_speed` (Number)
- `LED Strip_intensity` (Number)
- `LED Strip_palette` (Select)
- `LED Strip_mirror` (Switch)
- `LED Strip_intro` (Select)
- `LED Strip_use_palette` (Switch)
- `LED Strip_intro_dur` (Number)
- `LED Strip_timer` (Number)

**Note:** The naming scheme could differ based on how you use ESPHome `friendly_name`. 

### Exclusion Options
You can exclude specific controls by adding their ID to the `exclude` list (e.g., `exclude: [5, 6]`).

| ID | Control |
|:---|:---|
| `1` | Speed |
| `2` | Intensity |
| `3` | Palette |
| `4` | Mirror |
| `5` | Intro Effects (Intro & Duration) |
| `6` | Timer |

That's it! The effects will automatically find this controller and respect eventual exclusions.


## Advanced: Multiple Strips

### Option 1: Separate Controls (Independent)
If you want independent control for each strip (e.g., Roof vs Desk), create separate controllers:

```yaml
cfx_effect:
  cfx_control:
    - id: roof_controller
      name: "Roof Lights"
      light_id: led_strip_1  # The ID of the first light you want to control

    - id: desk_controller
      name: "Desk Lights"
      light_id: led_strip_2  # The ID of the second light you want to control
```

### Option 2: Unified Control (Grouped)
If you want a **single set of controls** to operate multiple lights simultaneously (e.g., "Global Control"), you can pass a list of light IDs:

```yaml
cfx_effect:
  cfx_control:
    - id: global_controller
      name: "Global"
      light_id: [led_strip_1, led_strip_2] # Key: Use a list format
```
This generates only one set of entities (e.g. `Global Speed`) that updates all listed lights at once.

## Overview of Controls

### ID 1: Speed
Control slider to manage the speed of the effect. The higher the value, the faster the effect will run. Range (0-255)

### ID 2: Intensity
Control slider to manage the intensity of the effect. Intensity can manage different parameters depending on the effect. Range (0-255)

### ID 3: Palette
Controls the palette used by the effect. A palette is a set of colors that are used by the effect, and every effect has its default palette that can be overridden by this control. Some effects like Fire, Fire Dual and Ocean doesn't allow to change the palette. List of selectable palettes can be found in the [Palettes](Effects-Library.md#palettes) section.

### ID 4: Mirror
Controls the starting point of an effect. Useful if you can't physically invert the strip. Affect Into Animation too.

### ID 5: Intro Animation
A group of three controls: Intro Style (None, Wipe, Fade, Center, Glitter), Intro Duration (0.5 - 10.0 seconds) and Intro Palette Support, allow the intro to use the same palette as the main effect. More details can be found in the [Intro Animations](Effects-Library.md#intro-animations) section.

### ID 6: Timer
Controls how longer a light stay on. From 0 (timer off) to 360 minutes