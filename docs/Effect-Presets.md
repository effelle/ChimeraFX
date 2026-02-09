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
          set_palette: 12           # Force 'Fire' palette (ID 5), Because everybody loves a good fireball 
          set_intensity: 170        # Longer tail 
          set_mirror: True          # Enable mirroring
          set_intro: 3              # Set Intro to Center
          set_intro_dur: 2.5        # Set Intro execution time to 2.5 seconds
          set_intro_palette: True   # Instruct Intro to use the palette we selected with set_palette
          set_timer: 30             # Set the timer to turn of the light after 30 minutes 
```

## Parameters

*   **`set_speed`**: (0-255) Sets the default speed.
*   **`set_intensity`**: (0-255) Sets the default intensity.
*   **`set_palette`**: (0-255) Sets the default palette ID.
*   **`set_mirror`**: (true/false) Sets the default mirror state.
*   **`set_intro`**: (Int) Sets the default Intro effect index (0=None, 1=Wipe, 2=Fade, 3=Center, 4=Glitter).
*   **`set_intro_dur`**: (Float) Sets the intro duration in seconds.
*   **`set_intro_palette`**: (true/false) Enable/Disable using the effect palette for the Intro.
*   **`set_timer`**: (Int) Sets the sleep timer in minutes (e.g., `30` for 30min).

> **TIP: Palette IDs can be retreived from [here](Effect-Library.md#palettes):** 0=Default, 1=Aurora, 2=Forest, 3=Halloween, 4=Rainbow, 5=Fire, 6=Sunset, 7=Ice, 8=Party, 9=Lava, 10=Pastel, 11=Ocean, 12=HeatColors, 13=Sakura, 14=Rivendell, 15=Cyberpunk, 16=OrangeTeal, 17=Christmas, 18=RedBlue, 19=Matrix, 20=SunnyGold, 21=Solid, 22=Fairy, 23=Twilight.
