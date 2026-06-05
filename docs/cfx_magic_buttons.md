# ChimeraFX Magic Buttons

ChimeraFX magic buttons are small on-device helpers for physical buttons. They
do not create Home Assistant entities. Instead, you bind an existing ESPHome
`binary_sensor` button to a helper with `press` and `release` actions.

```yaml
binary_sensor:
  - platform: gpio
    pin: GPIO05     # GPIO pin where the button is connected
    on_press:
      - helper_name.press: helper_id
    on_release:
      - helper_name.release: helper_id
```

## Button Dimmer

`cfx_dimmer` controls brightness. A short press toggles one or more lights. A
long press ramps brightness, and each new long press alternates between ramping
up and ramping down.

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
    pin: GPIO05     # GPIO pin where the button is connected
    on_press:
      - cfx_dimmer.press: desk_lamp_dimmer
    on_release:
      - cfx_dimmer.release: desk_lamp_dimmer
```

| Option | Default | Description |
| --- | --- | --- |
| `id` | required | Dimmer helper ID. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before dimming starts. |
| `ramp_time` | `2s` | Time to ramp from minimum to maximum brightness. |
| `min_brightness` | `15%` | Lowest brightness used by the ramp. Values below 15% are rejected. |
| `max_brightness` | `100%` | Highest brightness used by the ramp. |
| `restore_direction` | `false` | Persist the next ramp direction across reboot. |

Brightness changes use ESPHome light transitions, preserve the currently
selected ChimeraFX effect, and stop sending updates when the ramp reaches the
upper or lower bound.

Each accepted press records whether any configured light is on. A short release
uses that snapshot to turn the targets off or on, so transitions started during
the gesture cannot reverse the requested action. Short presses change only the
ON/OFF state; ESPHome remains authoritative for the retained brightness, color,
and active effect. Changes made from Home Assistant are therefore preserved.
Running effects are dimmed with direct brightness steps instead of ESPHome light
transitions to avoid effect stop/start churn while the button is held.
A long press only ramps when the gesture began with at least one configured
light on. Long presses that begin while all targets are off are ignored. On
release, the dimmer freezes at the brightness selected at that exact time. A
completed down-ramp stops at `min_brightness` and leaves the light on; only a
short press turns it off. The minimum applies to dimmer ramps and does not
rewrite a lower brightness deliberately selected through Home Assistant.
The first long press after boot defaults to ramping down. At 70% brightness or
higher, the dimmer always ramps down; at 30% or lower, it always ramps up.
Between those barriers, consecutive long presses alternate direction.
Classification also checks the total held time at release, so a busy device
loop cannot misclassify a completed hold as a short-press toggle. The first
release is applied immediately; repeated input edges are then suppressed until
the button has remained quiet for 350 ms.

## CCT Sweeper

`cfx_cct_sweeper` controls white tone with RGB/RGBW approximations. A short
press toggles one or more lights between the previous solid color and a
favorite white. A long press sweeps between warm and cool white; each new long
press alternates direction.

```yaml
cfx_cct_sweeper:
  - id: living_room_cct
    lights:
      - couch_lamp
      - tv_backlight

binary_sensor:
  - platform: gpio
    pin: GPIO06     # GPIO pin where the button is connected
    on_press:
      - cfx_cct_sweeper.press: living_room_cct
    on_release:
      - cfx_cct_sweeper.release: living_room_cct
```

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

CCT actions switch targets to solid color mode. If an effect is running, the
helper replaces it with the selected solid output.

While sweeping, the helper locks the release color from its own commanded sweep
timeline so ESPHome transition sampling cannot jump the output to a stale color.

## Hue Cycler

`cfx_hue_cycler` controls RGB color. A short press toggles one or more lights
between the previous solid RGB color and a standard white. A long press cycles
hue around the color wheel, and release locks the current color.

The default boot/start hue is cyan-blue (`0%, 62%, 100%`) so it does not
conflict with red error signaling.

```yaml
cfx_hue_cycler:
  - id: game_room_rgb
    lights:
      - monitor_backlight
      - desk_underglow

binary_sensor:
  - platform: gpio
    pin: GPIO07     # GPIO pin where the button is connected
    on_press:
      - cfx_hue_cycler.press: game_room_rgb
    on_release:
      - cfx_hue_cycler.release: game_room_rgb
```

| Option | Default | Description |
| --- | --- | --- |
| `id` | required | Hue cycler helper ID. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before hue cycling starts. |
| `cycle_time` | `6s` | Time for one full hue loop. |
| `white` | `[100%, 100%, 100%, 100%]` | Short-press white color as `[red, green, blue, white]`. |
| `saturation` | `100%` | Hue-cycle saturation. Lower values cycle pastel colors. |
| `restore_hue` | `false` | Persist the last locked hue across reboot. |

Hue actions switch targets to solid color mode. If an effect is running, the
helper replaces it with the selected solid output.

While cycling, the helper locks the release color from its own commanded hue
timeline so ESPHome transition sampling cannot jump the output to a stale color.

## Effect Selector

`cfx_effect_selector` controls a configured list of light effects. A short press
turns the target lights on or off. A long press walks through the configured
effect names, one effect at a time, and release keeps the currently selected
effect running.

```yaml
cfx_effect_selector:
  - id: desk_lamp_fx
    lights:
      - c3_Strip1
      - c3_Strip2
      - c3_Strip3
      - c3_Strip4
    effects:
      - Energy
      - Horizon Sweep
      - Ocean

binary_sensor:
  - platform: gpio
    pin: GPIO08     # GPIO pin where the button is connected
    on_press:
      - cfx_effect_selector.press: desk_lamp_fx
    on_release:
      - cfx_effect_selector.release: desk_lamp_fx
```

| Option | Default | Description |
| --- | --- | --- |
| `id` | required | Effect selector helper ID. |
| `lights` | required | One or more light entities to control. |
| `effects` | required | One or more effect names to cycle through. Names must match the target light effect names. |
| `long_press` | `500ms` | Press duration before effect selection starts. |
| `effect_interval` | `900ms` | Time between effect changes while the button remains held. |

Short press on uses the last selected effect, starting with the first configured
effect after boot. Long press starts from the currently active configured effect
when possible, then advances to the next effect immediately. If the lights are
off, it first restores the last effect selected during the current runtime and
then continues cycling while held. The runtime selection resets after reboot.
