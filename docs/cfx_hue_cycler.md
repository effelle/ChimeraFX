# cfx_hue_cycler

`cfx_hue_cycler` is an on-device helper for a physical color button. A short
press toggles one or more lights between the previous solid RGB color and a
standard white. A long press cycles hue around the color wheel, and release
locks the current color.

It does not create a Home Assistant entity. Bind it to an existing ESPHome
`binary_sensor` button.

With the normal GitHub install, no component allow-list is needed:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    refresh: always
```

If your existing `external_components` block still has a `components:` list,
remove that list or add `cfx_hue_cycler` to it. ESPHome treats `components:` as
a hard allow-list, so omitted components cannot be imported.

```yaml
cfx_hue_cycler:
  - id: game_room_rgb
    lights:
      - monitor_backlight
      - desk_underglow

binary_sensor:
  - platform: gpio
    pin: GPIO07
    on_press:
      - cfx_hue_cycler.press: game_room_rgb
    on_release:
      - cfx_hue_cycler.release: game_room_rgb
```

## Configuration

| Option | Default | Description |
| --- | --- | --- |
| `id` | required | Hue cycler helper ID. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before hue cycling starts. |
| `cycle_time` | `6s` | Time for one full hue loop. |
| `white` | `[100%, 100%, 100%, 100%]` | Short-press white color as `[red, green, blue, white]`. |
| `saturation` | `100%` | Hue-cycle saturation. Lower values cycle pastel colors. |
| `restore_hue` | `false` | Persist the last locked hue across reboot. |

The helper uses RGB/RGBW light calls so it works with ChimeraFX strips and
segments. Button actions switch targets to solid color mode.
