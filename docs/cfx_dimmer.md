# cfx_dimmer

`cfx_dimmer` is a small on-device helper for physical buttons. A short press
toggles one or more lights. A long press ramps brightness, and each new long
press alternates between ramping up and ramping down.

It does not create a Home Assistant entity. Bind it to an existing ESPHome
`binary_sensor` button.

If your `external_components` entry uses a `components:` allow-list, include
`cfx_dimmer` there too:

```yaml
external_components:
  - source: github://effelle/ChimeraFX@stage
    refresh: always
    components: [cfx_light, cfx_effect, cfx_sequence, cfx_power, cfx_dimmer]
```

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
| `restore_direction` | `false` | Persist the next ramp direction across reboot. |

Brightness changes preserve the currently selected ChimeraFX effect.
