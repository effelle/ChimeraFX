# Controls Guide

Now you have the effect up and running, but how about control them? And what can you control? 
`ChimeraFX` can create inputs (sliders, dropdowns, switches) to help you tweak the effect for your liking. The `cfx_control` component is optional and only needed (but strongly suggested) if you want to control the effect from Home Assistant or another controller. Whitout it you will still be able to use the effects at default values.

## Controller Logic

The `addressable_cfx` effect listens to specific global variables (numbers, selects and switches) to adjust its behavior in real-time.

To generate controls automatically, use the `cfx_control` component. You need to declare it under the `cfx_effect` component, and it will create all the necessary entities (Speed, Intensity, Palette, Mirror, Intro Effects and Timer) for you, using core `number`, `select` and `switch` components, and link them to your light.

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

> [!IMPORTANT]
> **Component Dependencies:** The `cfx_control` component automatically generates entities using ESPHome's standard `number`, `select` and `switch` components. You do **not** need to manually define them.

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
      light_id: led_strip_1

    - id: desk_controller
      name: "Desk Lights"
      light_id: led_strip_2
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
This generates one set of entities (e.g. `Global Speed`) that updates all listed lights at once.

## Preset Configurations

> **Moved:** [Click here for Effect Presets documentation](Effect-Presets.md).


## Intro & Turn-On Effects

This feature allows a specific effect to run **once** when the light is turned on, before transitioning smoothly to the main effect.

### Options
*   **None**: Standard behavior (Main effect starts immediately).
*   **Wipe**: A color wiper from start to end (Color 1).
*   **Fade**: Fade in from black (Color 1).
*   **Center**: A "curtain" open effect (Wipe from center to edges).
*   **Glitter**: Sparkles appearing and fading out.

### Transition Behavior
When the Intro Duration ends, the Intro Effect will **Dissolve** (Soft Fairy Dust) into the Main Effect over 1.5 seconds. This creates a seamless, premium startup experience.

## Alternative: Arduino Framework

While ESP-IDF is the standard for high performance, we also fully support the **Arduino framework**. It is particularly robust for timing stability via NeoPixelBus:

```yaml
esp32:
  board: esp32-s3-devkitc-1  # Or your board
  framework:
    type: arduino
```


> TIP: The Arduino framework provides more stable NeoPixelBus timing, which is critical for smooth WLED effects. ESP-IDF is still supported but may exhibit slightly higher latency on complex effects.

This is particularly important when:
- Running multiple LED strips simultaneously
- Using effects with high per-pixel math (Ocean, Aurora, Plasma)

