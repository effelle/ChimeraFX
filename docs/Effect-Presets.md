# Effect Presets (Hardcoded Defaults)

You can hardcode specific settings for each effect directly in the YAML. This is useful if you want an effect to always start with a specific speed, color palette, or intensity, regardless of the global controls. 

Use the `set_*` parameters (e.g., `set_speed`, `set_palette`) to enforce these values.

## Example

```yaml
    effects:
      - addressable_cfx:
          name: "Custom Meteor"
          effect_id: 76
          set_speed: 70             # Slowing down the animation 
          set_palette: 5            # Force 'Fire' palette (ID 5)
          set_intensity: 170        # Longer tail 
          set_brightness: 80%       # Start the light at a known brightness
          set_mirror: true          # Enable mirroring
          set_intro: 3              # Set Intro to Center
          set_outro: 5              # Set Outro to Twin Pulse
          set_inout_dur: 1.5        # Set Outro duration to 1.5 seconds
```

This is particularly interesting if you want to use the same effect with different settings. Simply declare the same effect ID with another name and new parameters and you’re good to go! 
If you use the same name as an existing effect, it will be overridden with the new presets. 

While you could use a script or a scene in Home Assistant to achieve the same result, this gives you the freedom to choose the method that works best for you.

**Note:** Effect presets intentionally do not support `set_autotune`. Hard presets already prevent [autotune](Controls.md#id-7-autotune) from overwriting those same parameters, and runtime autotune overrides belong on orchestration surfaces like `cfx_set`, `cfx_sequence`, and `cfx_run`. 
⚠️ **Important:** When a preset effect is turned OFF, all the parameters reset to their defaults values.

## Parameters

*   **`set_speed`**: (0-255) Sets the default speed.
*   **`set_intensity`**: (0-255) Sets the default intensity.
*   **`set_palette`**: (0-255) Sets the default palette ID. See [here](Effects-Library.md#palettes) for a list of available palettes.
*   **`set_brightness`**: (0-100%) Sets the default light brightness when the effect starts.
*   **`set_color`**: (`[R, G, B]` or `[R, G, B, W]` in percent) Sets the default light color when the effect starts.
*   **`set_mirror`**: (true/false) Sets the default mirror state. Affects both intro and main effect.
*   **`set_force_white`**: (true/false) Forces the white channel on eligible monochromatic/static effects.
*   **`set_intro`**: (int) Sets the default Intro effect. See [here](Effects-Library.md#intro-and-outro-animations) for a list of available.
*   **`set_outro`**: (int) Sets the default Outro effect (uses the same indices as `set_intro`).
*   **`set_inout_dur`**: (Float) Sets the intro and Outro duration in seconds (0.5s - 10.0s).
