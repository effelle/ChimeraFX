# ChimeraFX Magic Buttons

ChimeraFX magic buttons are small on-device helpers for physical buttons. They
do not create Home Assistant entities. The `cfx_button` platform
binds an existing ESPHome `binary_sensor` to one ChimeraFX controller:

```yaml
binary_sensor:
  - platform: gpio
    id: my_button
    pin: GPIO05     # GPIO pin where the button is connected

cfx_button:
  - id: my_button_binding
    button: my_button
    dimmer:
      lights:
        - my_light
```

Exactly one controller is allowed per wrapper: `dimmer`, `cct_sweeper`,
`hue_cycler`, or `effect_selector`. The referenced binary sensor remains
responsible for GPIO configuration, inversion, filtering, debounce, touch
inputs, expanders, and template state. Its logical convention must be
`ON = pressed`.

Each controller may target a segmented master or its child segments, but the
same `lights` list cannot contain both the master and any of its children.
Configuration validation rejects that overlap.

Each controller is configured directly inside its wrapper; no separate helper
declaration or helper ID is required. The wrapper waits for the first valid
`OFF` state before accepting input. A
button that is already `ON` during startup is ignored until released, preventing
an accidental gesture after reboot. Controller timing and short/long behavior
remain configured inside the selected controller block.

Every short-press light command respects the target light's
`default_transition_length`. Long-press dimming, CCT sweeping, hue cycling, and
effect browsing keep their own configured timing. Effects with authored power
choreography, including architectural and signature effects with embedded
intros, continue to ignore the default transition through ChimeraFX's existing
effect eligibility rules. A CCT `native_white` endpoint that uses only the
physical white channel switches immediately, preventing ESPHome's addressable
transition engine from carrying stale RGB channels into native-white mode.

## Button Dimmer

`cfx_dimmer` controls brightness. A short press toggles one or more lights. A
long press ramps brightness, and each new long press alternates between ramping
up and ramping down.

```yaml
binary_sensor:
  - platform: gpio
    id: desk_lamp_button
    pin: GPIO05     # GPIO pin where the button is connected

cfx_button:
  - id: desk_lamp_button_binding
    button: desk_lamp_button
    dimmer:
      lights:
        - desk_lamp_segment_1
        - desk_lamp_segment_2
        - desk_lamp_segment_3
        - desk_lamp_segment_4
```

| Option | Default | Description |
| --- | --- | --- |
| `id` | optional | Internal dimmer helper ID. Generated automatically when omitted. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before dimming starts. |
| `ramp_time` | `2s` | Time to ramp from minimum to maximum brightness. |
| `min_brightness` | `15%` | Lowest brightness used by the ramp. Values below 15% are rejected. |
| `max_brightness` | `100%` | Highest brightness used by the ramp. |

Brightness changes use ESPHome light transitions, preserve the currently
selected ChimeraFX effect, and stop sending updates when the ramp reaches the
upper or lower bound.

Each accepted press records whether any configured light is on. A short release
uses that snapshot to turn the targets off or on, so transitions started during
the gesture cannot reverse the requested action. Immediately before turning
targets off, the dimmer captures each active target independently. The next
dimmer turn-on restores a solid snapshot's brightness, color mode, and RGBW
values. An effect snapshot instead restores its brightness and effect name
without forcing possibly stale solid-color channels. Child segments therefore
retain independent state. A segmented master has no effect state to capture, so
its brightness and color values are restored as a solid snapshot.

These snapshots exist only at runtime and do not survive reboot. A target that
has not yet been captured, including one that was already off when another
target was captured, uses normal light turn-on behavior, so its configured
restore mode or defaults may apply.
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

`cfx_cct_sweeper` controls white tone with RGB/RGBW approximations. It keeps an
immutable native-white endpoint for the physical white channel and a preferred
white that can be selected with a long press.

```yaml
binary_sensor:
  - platform: gpio
    id: living_room_cct_button
    pin: GPIO06     # GPIO pin where the button is connected

cfx_button:
  - id: living_room_cct_binding
    button: living_room_cct_button
    cct_sweeper:
      lights:
        - couch_lamp
        - tv_backlight
      native_white: [0%, 0%, 0%, 100%]
      preferred_white: [100%, 90%, 75%, 60%]
      restore: true
```

| Option | Default | Description |
| --- | --- | --- |
| `id` | optional | Internal CCT sweeper helper ID. Generated automatically when omitted. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before sweeping starts. |
| `sweep_time` | `4s` | Time to sweep from warm to cool white. |
| `native_white` | `[0%, 0%, 0%, 100%]` | Immutable native white-channel endpoint as `[red, green, blue, white]`. |
| `preferred_white` | `[100%, 100%, 100%, 100%]` | Boot/default preferred white as `[red, green, blue, white]`. A long press updates it at runtime. |
| `restore` | `false` | Restore the last preferred white selected by long press after reboot. |

The legacy `favorite_white` option remains accepted as an alias for
`preferred_white`, but the two names cannot be configured together.
White-only endpoints such as the default `native_white` use ESPHome's native
white color mode, keeping the RGB emitters off instead of normalizing them to
full output.

A short press from the initial off state turns the targets on at
`preferred_white`. After another controller or Home Assistant has turned the
targets off, the first CCT short press restores their retained ESPHome state
without replacing its effect, color, or brightness. The next short press enters
CCT control. While the targets are on, a non-CCT output changes to the last
white endpoint selected by this helper; later short presses alternate between
`native_white` and `preferred_white`.

A long press that begins while all targets are off is ignored. The first valid
sweep after boot moves toward the built-in warm limit; later long presses
alternate between the internal warm and cool limits. Releasing the button freezes one shared white value
across all targets and makes it the new `preferred_white`. With `restore:
false`, reboot returns to the YAML value. With `restore: true`, the learned
value survives reboot. When a sweep begins from `native_white`, the helper first
enters the stored `preferred_white` RGBW domain so the warm/cool movement is
visible immediately; `native_white` itself remains immutable.

While sweeping, the helper locks the release color from its own commanded sweep
timeline so ESPHome transition sampling cannot jump the output to a stale color.

## Hue Cycler

`cfx_hue_cycler` controls RGBW color. A short press toggles one or more lights
between the selected hue and an immutable `fixed_color`. The fixed endpoint can
be white or any other RGBW color. Release keeps the color currently shown.

Choose one long-press mode:

- **Hue mode:** omit `colors`. The helper continuously cycles around the hue
  wheel using `cycle_time` and `saturation`.
- **Palette mode:** configure `colors`. The helper steps only through those
  RGBW entries using `color_interval`. Do not configure `cycle_time` or
  `saturation` with `colors`.

The default boot/start hue is cyan-blue (`0%, 62%, 100%`) so it does not
conflict with red error signaling.

```yaml
binary_sensor:
  - platform: gpio
    id: game_room_rgb_button
    pin: GPIO07     # GPIO pin where the button is connected

cfx_button:
  - id: game_room_rgb_binding
    button: game_room_rgb_button
    hue_cycler:
      lights:
        - monitor_backlight
        - desk_underglow
      fixed_color: [100%, 100%, 100%, 100%]
```

| Option | Default | Description |
| --- | --- | --- |
| `id` | optional | Internal hue cycler helper ID. Generated automatically when omitted. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before hue cycling starts. |
| `cycle_time` | `6s` | Hue mode only. Time for one full hue loop. Cannot be used with `colors`. |
| `colors` | unset | Optional list of RGBW colors. Enables palette mode and cannot be combined with `cycle_time` or `saturation`. |
| `color_interval` | `900ms` | Time between palette entries while held. Valid only with `colors`. |
| `fixed_color` | `[100%, 100%, 100%, 100%]` | Immutable short-press endpoint as `[red, green, blue, white]`. It does not need to be white. |
| `saturation` | `100%` | Hue mode only. Lower values cycle pastel colors. Cannot be used with `colors`. |
| `restore` | `false` | Restore the last locked hue or palette entry after reboot. |

Palette mode uses the same RGBW format on every strip. RGB-only lights ignore
the white entry. The first palette entry is selected when long press activates;
later entries follow at `color_interval` and wrap to the beginning.

```yaml
cfx_button:
  - button: game_room_rgb_button
    hue_cycler:
      lights:
        - monitor_backlight
        - desk_underglow
      colors:
        - [10%, 30%, 50%, 0%]
        - [90%, 30%, 0%, 50%]
        - [10%, 100%, 10%, 100%]
      color_interval: 700ms
      restore: true
```

Hue actions switch targets to solid color mode. If an effect is running, the
helper replaces it with the selected solid output.

The helper recognizes only its configured `fixed_color` and its own selected
hue. A short press while the selected hue is active applies `fixed_color`.
A short press from `fixed_color`, a CCT selection, a Home Assistant change, or
any other external state restores the selected hue. This avoids guessing
whether an arbitrary RGBW value represents white. Repeated input edges are
suppressed until the button has remained quiet for 350 ms.

While cycling, the helper locks the release color from its own commanded hue
timeline so ESPHome transition sampling cannot jump the output to a stale color.

## Effect Selector

`cfx_effect_selector` controls a configured list of light effects. A short press
turns the target lights on or off. A long press walks through the configured
effect names, one effect at a time, and release keeps the currently selected
effect running.

```yaml
binary_sensor:
  - platform: gpio
    id: desk_lamp_fx_button
    pin: GPIO08     # GPIO pin where the button is connected

cfx_button:
  - id: desk_lamp_fx_binding
    button: desk_lamp_fx_button
    effect_selector:
      lights:
        - c3_Strip1
        - c3_Strip2
        - c3_Strip3
        - c3_Strip4
      effects:
        - Energy
        - Horizon Sweep
        - Ocean
```

| Option | Default | Description |
| --- | --- | --- |
| `id` | optional | Internal effect selector helper ID. Generated automatically when omitted. |
| `lights` | required | One or more light entities to control. |
| `effects` | required | One or more effect names to cycle through. Names must match the target light effect names. |
| `long_press` | `500ms` | Press duration before effect selection starts. |
| `effect_interval` | `900ms` | Time between effect changes while the button remains held. |
| `restore` | `false` | Restore the last selected effect after reboot. |

Short press on uses the last selected effect, starting with the first configured
effect after boot. Long press starts from the currently active configured effect
when possible, then advances to the next effect immediately. If the lights are
off, it first restores the last selected effect and then continues cycling
while held. With `restore: true`, that selection survives reboot.

`cfx_button` is the only supported configuration interface. Standalone helper
declarations and direct `press`/`release` actions are not available.
