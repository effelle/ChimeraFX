# cfx_cct_sweeper

`cfx_cct_sweeper` is an on-device helper for a physical white-temperature
button. A short press toggles one or more lights between the previous solid
color and a favorite white. A long press sweeps between warm and cool white;
each new long press alternates direction.

It does not create a Home Assistant entity. Bind it to an existing ESPHome
`binary_sensor` button.

With the normal GitHub install, no component allow-list is needed:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    refresh: always
```

If your existing `external_components` block still has a `components:` list,
remove that list or add `cfx_cct_sweeper` to it. ESPHome treats `components:`
as a hard allow-list, so omitted components cannot be imported.

```yaml
cfx_cct_sweeper:
  - id: living_room_cct
    lights:
      - couch_lamp
      - tv_backlight

binary_sensor:
  - platform: gpio
    pin: GPIO06
    on_press:
      - cfx_cct_sweeper.press: living_room_cct
    on_release:
      - cfx_cct_sweeper.release: living_room_cct
```

## Configuration

| Option | Default | Description |
| --- | --- | --- |
| `id` | required | CCT sweeper helper ID. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before sweeping starts. |
| `sweep_time` | `4s` | Time to sweep from warm to cool white. |
| `favorite_white` | `[100%, 100%, 100%, 100%]` | Short-press white color as `[red, green, blue, white]`. |
| `warm_white` | `[100%, 55%, 18%, 100%]` | Warm sweep endpoint as `[red, green, blue, white]`. |
| `cool_white` | `[70%, 85%, 100%, 100%]` | Cool sweep endpoint as `[red, green, blue, white]`. |
| `restore_direction` | `false` | Persist the next sweep direction across reboot. |

The helper uses RGB/RGBW approximations so it works with ChimeraFX strips and
segments. Button actions switch targets to solid color mode.
