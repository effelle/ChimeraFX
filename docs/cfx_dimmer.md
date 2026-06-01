# cfx_dimmer

`cfx_dimmer` is a small on-device helper for physical buttons. A short press
toggles one or more lights. A long press ramps brightness, and each new long
press alternates between ramping up and ramping down.

It does not create a Home Assistant entity. Bind it to an existing ESPHome
`binary_sensor` button.

With the normal GitHub install, no component allow-list is needed:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@main
    refresh: always
```

If your existing `external_components` block still has a `components:` list,
remove that list or add `cfx_dimmer` to it. ESPHome treats `components:` as a
hard allow-list, so omitted components cannot be imported.

```yaml
cfx_dimmer:
  - id: desk_lamp_dimmer
    lights:
      - desk_lamp_segment_1
      - desk_lamp_segment_2
      - desk_lamp_segment_3
      - desk_lamp_segment_4

binary_sensor:
  - platform: gpio
    pin: GPIO0
    on_press:
      - cfx_dimmer.press: desk_lamp_dimmer
    on_release:
      - cfx_dimmer.release: desk_lamp_dimmer
```

## Configuration

| Option | Default | Description |
| --- | --- | --- |
| `id` | required | Dimmer helper ID. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before dimming starts. |
| `ramp_time` | `2s` | Time to ramp from minimum to maximum brightness. |
| `min_brightness` | `1%` | Lowest brightness used by the ramp. |
| `max_brightness` | `100%` | Highest brightness used by the ramp. |
| `off_brightness` | `10%` | Down-ramp cutoff that turns target lights off. |
| `restore_direction` | `false` | Persist the next ramp direction across reboot. |

Brightness changes use ESPHome light transitions, preserve the currently
selected ChimeraFX effect, and stop sending updates when the ramp reaches the
upper or lower bound. On a down-ramp, targets turn off when they reach
`off_brightness`.
