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
          set_palette: 5            # Force 'Fire' palette (ID 5), Because everybody loves a good fireball 
          set_intensity: 170        # Longer tail 
          set_mirror: true          # Enable mirroring
          set_intro: 3              # Set Intro to Center
          set_intro_dur: 2.5        # Set Intro execution time to 2.5 seconds
          set_intro_palette: true   # Instruct Intro to use the palette we selected with set_palette
          set_timer: 30             # Set the timer to turn off the light after 30 minutes 
```

This is particularly useful if you want to use the same effect with different settings. Simply declare the same effect ID with your new parameters and you’re good to go! While you could use a script or a scene in Home Assistant to achieve the same result, this gives you the freedom to choose the method that works best for you.

**Note:** When a light is turned off and then on again, the effect will reset to its default speed, intensity, and timer settings. This ensures a clean and predictable setup for the light every time. If you are not using a preset, the effect will always revert to its default state.

## Parameters

*   **`set_speed`**: (0-255) Sets the default speed.
*   **`set_intensity`**: (0-255) Sets the default intensity.
*   **`set_palette`**: (0-255) Sets the default palette ID. See [here](Effects-Library.md#palettes) for a list of available palettes.
*   **`set_mirror`**: (true/false) Sets the default mirror state. It will affect both intro and main effect.
*   **`set_intro`**: (Int) Sets the default Intro effect index (0=None, 1=Wipe, 2=Fade, 3=Center, 4=Glitter).
*   **`set_intro_dur`**: (Float) Sets the intro duration in seconds with a minimum of 0.5 seconds and a maximum of 10 seconds.
*   **`set_intro_palette`**: (true/false) Enable/Disable using the effect palette for the Intro.
*   **`set_timer`**: (Int) Sets the sleep timer in minutes (e.g., `30` for 30min), where 0 means no timer. The maximum allowed value is 360 minutes.
