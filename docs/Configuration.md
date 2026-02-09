# YAML Configuration Guide

ESPHome requires you to define your inputs (sliders, dropdowns, switches) explicitly at compile-time. This gives you the power to create multiple independent light setups or simplify controls as you see fit.

## The Controller Logic

The `addressable_cfx` effect listens to specific global variables (numbers, selects and switches) to adjust its behavior in real-time.

**Two Ways to Configure: Automatic or Manual**

## **1. Automatic Configuration(Recommended):** 
Use the `cfx_control` component. It generates all necessary entities for you using core `number`, `select`, and `switch` components.

Add the `cfx_control` component to automatically generate all controls (Speed, Intensity, Palette, Mirror, Intro, Timer) and link them to your light.

```yaml
cfx_effect:
  cfx_control:
    - id: my_cfx_controller # The ID of the controller generator
      name: "LED Strip"
      light_id: led_strip # The ID of the light you want to control

      # Optional: Exclude controls you don't need
      # exclude: [timer, intro]  # or IDs [6, 5]
```

**Note:** The `light_id` parameter in the `cfx_control` component is used to link the controller to a specific light. This is necessary because the `addressable_cfx` effect needs to know which light it is controlling.

> [!IMPORTANT]
> **Component Dependencies:** The `cfx_control` component automatically generates entities using ESPHome's standard `number`, `select`, and `switch` components. You do **not** need to manually define them.

### Generated Controls
The component creates the following entities based on your controller name (e.g., `LED Strip`):

- `LED Strip_speed` (Number)
- `LED Strip_intensity` (Number)
- `LED Strip_palette` (Select)
- `LED Strip_mirror` (Switch)
- `LED Strip_intro` (Select)
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

If you have two or more strips (e.g., `Roof Lights`, `Desk Lights`, etc.), just create one controller for each strip. You can do this by adding multiple sets of controls under the `cfx_control` component, then link them to their respective light IDs:

```yaml
cfx_effect:
  cfx_control:
    - id: my_first_cfx_controller # The ID of the first controller generator
      name: "Roof Lights"
      light_id: led_strip # The ID of the first light you want to control

    - id: my_second_cfx_controller # The ID of the second controller generator
      name: "Desk Lights"
      light_id: led_strip_2 # The ID of the second light you want to control
```

## Preset Configurations (Hardcoded Defaults)

You can hardcode specific settings for each effect directly in the YAML. This is useful if you want an effect to always start with a specific speed, color palette, or intensity, regardless of the global controls.

Use the `set_*` parameters (e.g., `set_speed`, `set_palette`) to enforce these values.

### Example

```yaml
    effects:
      - addressable_cfx:
          name: "Slow Ocean"
          effect_id: 101
          
          # Force specific defaults for this effect
          set_speed: 10       # Very slow (0-255)
          set_palette: 11     # Force 'Ocean' palette (ID 11)
          set_intensity: 200  # High brightness (0-255)
          set_mirror: false   # Disable mirroring
```

### Parameters

*   **`set_speed`**: (0-255) Sets the default speed.
*   **`set_intensity`**: (0-255) Sets the default intensity.
*   **`set_palette`**: (0-255) Sets the default palette ID.
*   **`set_mirror`**: (true/false) Sets the default mirror state.

> **TIP: Palette IDs can be retreived from [here](Effect-Library.md#palettes):** 0=Default, 1=Aurora, 2=Forest, 3=Halloween, 4=Rainbow, 5=Fire, 6=Sunset, 7=Ice, 8=Party, 9=Lava, 10=Pastel, 11=Ocean, 12=HeatColors, 13=Sakura, 14=Rivendell, 15=Cyberpunk, 16=OrangeTeal, 17=Christmas, 18=RedBlue, 19=Matrix, 20=SunnyGold, 21=Solid, 22=Fairy, 23=Twilight.

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

