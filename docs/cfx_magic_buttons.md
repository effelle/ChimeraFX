# ChimeraFX Magic Buttons

ChimeraFX Magic Buttons let you control your lights directly using physical buttons wired to your ESP32 device. 

Because magic buttons process all button presses locally on the device, they feature **instant control with zero network delay**. They will continue to work perfectly even if Home Assistant or your Wi-Fi network goes down.

> [!NOTE]
> Magic buttons operate entirely on the hardware level and do not create separate Home Assistant entities.

The `cfx_button` platform binds an existing ESPHome `binary_sensor` to one ChimeraFX controller:

```yaml
binary_sensor:
  - platform: gpio
    id: my_button
    pin:
      number: GPIO05
      mode: INPUT_PULLUP # Recommended: Keeps pin HIGH when button is open
      inverted: true     # Recommended: Reports ON (true) when button is pressed to GND

cfx_button:
  - id: my_button_binding
    button: my_button
    dimmer:
      lights:
        - my_light
```

### How It Works

*   **One Controller per Binding:** Each `cfx_button` configuration can have exactly one controller type: `dimmer`, `cct_sweeper`, `hue_cycler`, or `effect_selector`.
*   **Button State Convention:** The physical button must report `ON` when pressed and `OFF` when released. If your physical button is wired to GND (which is standard for most DIY ESPHome setups), ensure you set `inverted: true` and `mode: INPUT_PULLUP` on your `binary_sensor` pin configuration.
*   **Segment Validation:** Each controller can target a master light or individual child light segments, but a single controller's `lights` list cannot contain both a master light and its child segments.
*   **Startup Protection:** The button controller waits for the first valid `OFF` state (button released) after a reboot before accepting any input. If a button is held down or stuck during startup, it is ignored until released, preventing accidental triggers.
*   **Transition Speeds:**
    *   A quick tap (short-press) turns the light on/off using the light's normal transition speed (`default_transition_length`).
    *   Holding the button (long-press) dims the light or cycles colors/effects using custom configured timings.
    *   If your light has a dedicated white channel (like RGBW), CCT mode switches to it instantly, preventing the light from flashing stale RGB colors.

---

## Button Dimmer

`cfx_dimmer` controls light brightness. A short press toggles the target lights on or off. A long press ramps the brightness up or down, automatically alternating directions with each subsequent hold.

```yaml
binary_sensor:
  - platform: gpio
    id: desk_lamp_button
    pin:
      number: GPIO05
      mode: INPUT_PULLUP
      inverted: true

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

For rocker dimmers or wall controls with separate brighten/dim contacts, the dimmer can also bind directional inputs. The main `button` remains optional (it keeps the normal short-press toggle plus alternating long-press dimming when present). `inputs.up` and `inputs.down` start ramping immediately while held and never perform a short-press toggle.

```yaml
binary_sensor:
  - platform: gpio
    id: desk_lamp_toggle
    pin:
      number: GPIO05
      mode: INPUT_PULLUP
      inverted: true
  - platform: gpio
    id: desk_lamp_up
    pin:
      number: GPIO06
      mode: INPUT_PULLUP
      inverted: true
  - platform: gpio
    id: desk_lamp_down
    pin:
      number: GPIO07
      mode: INPUT_PULLUP
      inverted: true

cfx_button:
  - id: desk_lamp_dimmer_binding
    button: desk_lamp_toggle
    dimmer:
      lights:
        - desk_lamp
      inputs:
        up: desk_lamp_up
        down: desk_lamp_down
```

### Configuration Options

| Option | Default | Description |
| --- | --- | --- |
| `id` | optional | Internal dimmer helper ID. Generated automatically when omitted. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before dimming starts. |
| `ramp_time` | `2s` | Time to ramp from minimum to maximum brightness. |
| `min_brightness` | `15%` | Lowest brightness used by the ramp. Values below 15% are rejected. |
| `max_brightness` | `100%` | Highest brightness used by the ramp. |
| `inputs.up` | unset | Optional binary sensor that ramps brightness upward immediately while held. |
| `inputs.down` | unset | Optional binary sensor that ramps brightness downward immediately while held. |

### Key Features & Behaviors

*   **Smart Dimming Direction:**
    *   The first long press after boot defaults to ramping down.
    *   **High Brightness (70%+):** Ramping down is always used.
    *   **Low Brightness (30% or lower):** Ramping up is always used.
    *   **In-between:** Consecutive long presses alternate directions (up then down, down then up).
*   **State Snapshots:** Before turning a light off, the dimmer captures its current state. The next turn-on restores these values:
    *   If it was showing a solid color: Restores brightness, color mode, and RGBW channels.
    *   If it was running an effect: Restores brightness and the effect name (avoiding conflicting solid color channels).
    *   *Note: Snapshots are kept in temporary memory and do not survive a reboot.*
*   **Smart Ramping Restrictions:**
    *   A single-button long press will only start dimming if the light is **already on**. Long presses starting while all targets are off are ignored (preventing accidental dimming in the dark).
    *   A down-ramp stops at `min_brightness` (15% by default) and leaves the light on; only a short tap turns the light completely off.
*   **Directional Inputs (Rocker Switches):**
    *   `inputs.up` can turn an off light ON and brighten it starting from `min_brightness`.
    *   `inputs.down` is ignored when all target lights are off.
    *   Holding directional inputs starts ramping immediately and will not toggle the light state on release.
*   **Anti-Flicker & Debounce Protection:** 
    *   The dimmer uses direct brightness steps while the button is held. This keeps local and synced lights on the same commanded ramp position and avoids sampling ESPHome's internal transition state on release.
    *   After releasing the button, duplicate keypress signals are ignored for 350ms to ensure a double-trigger doesn't toggle your light by accident.

---

## CCT Sweeper

`cfx_cct_sweeper` controls white color temperature using RGB or RGBW approximations. It lets you alternate between a clean physical white light (`native_white`) and a custom tuned color temperature (`preferred_white`).

```yaml
binary_sensor:
  - platform: gpio
    id: living_room_cct_button
    pin:
      number: GPIO06
      mode: INPUT_PULLUP
      inverted: true

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

### Configuration Options

| Option | Default | Description |
| --- | --- | --- |
| `id` | optional | Internal CCT sweeper helper ID. Generated automatically when omitted. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before sweeping starts. |
| `sweep_time` | `4s` | Time to sweep from warm to cool white. |
| `native_white` | `[0%, 0%, 0%, 100%]` | Immutable native white-channel endpoint as `[red, green, blue, white]`. |
| `preferred_white` | `[100%, 100%, 100%, 100%]` | Boot/default preferred white as `[red, green, blue, white]`. A long press updates it at runtime. |
| `restore` | `false` | Restore the last preferred white selected by long press after reboot. |

### Key Features & Behaviors

*   **Understanding Color Formats:** 
    *   The arrays `[red, green, blue, white]` represent the power levels (0% to 100%) sent to the light channels.
    *   For example, `[100%, 90%, 75%, 60%]` uses all three RGB colors plus the physical white channel to approximate a specific white temperature.
*   **Short Press Actions:**
    *   If the light is off, a short press turns it on using your `preferred_white`.
    *   If the light was turned off by Home Assistant or another control, the first short press restores its last state without changing the active effect or color.
    *   If the light is on and showing a color/running an effect, a short press switches it to the last active white endpoint.
    *   If it is already showing white, subsequent short presses toggle back and forth between `native_white` (dedicated white LED) and `preferred_white`.
*   **Long Press Sweeping:**
    *   Long presses starting while all target lights are off are ignored.
    *   The first sweep moves toward a **warm white** limit. Subsequent sweeps alternate between **warm** and **cool** limits.
    *   Releasing the button freezes the current white tone and updates your `preferred_white` color dynamically.
    *   If you set `restore: true`, this dynamically selected preferred white will be saved and survive device reboots.
*   **Transition Lock:** During a sweep, the color state is locked to the sweep position, preventing ESPHome's transition engine from causing the color to jump or shift.

---

## Hue Cycler

`cfx_hue_cycler` controls RGBW color. A short press toggles the target lights between your last selected color (hue) and a fixed color (usually white). A long press lets you cycle through colors to choose a new hue.

Choose one long-press mode:

*   **Hue mode:** Omit the `colors` list. The helper continuously cycles around the hue wheel using `cycle_time` and `saturation`.
*   **Palette mode:** Configure the `colors` list. The helper steps only through those exact colors using `color_interval`. (Do not configure `cycle_time` or `saturation` with `colors`).

```yaml
binary_sensor:
  - platform: gpio
    id: game_room_rgb_button
    pin:
      number: GPIO07
      mode: INPUT_PULLUP
      inverted: true

cfx_button:
  - id: game_room_rgb_binding
    button: game_room_rgb_button
    hue_cycler:
      lights:
        - monitor_backlight
        - desk_underglow
      fixed_color: [100%, 100%, 100%, 100%]
```

### Configuration Options

| Option | Default | Description |
| --- | --- | --- |
| `id` | optional | Internal hue cycler helper ID. Generated automatically when omitted. |
| `lights` | required | One or more light entities to control. |
| `long_press` | `500ms` | Press duration before hue cycling starts. |
| `cycle_time` | `6s` | Hue mode only. Time for one full hue loop. Cannot be used with `colors`. |
| `colors` | unset | Optional list of RGBW colors. Enables palette mode and cannot be combined with `cycle_time` or `saturation`. |
| `color_interval` | `900ms` | Time between palette entries while held. Valid only with `colors`. |
| `fixed_color` | `[100%, 100%, 100%, 100%]` | Immutable short-press endpoint as `[red, green, blue, white]`. |
| `saturation` | `100%` | Hue mode only. Lower values cycle pastel colors. Cannot be used with `colors`. |
| `restore` | `false` | Restore the last locked hue or palette entry after reboot. |

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

### Key Features & Behaviors

*   **Short Press Toggling:** A short press toggles the target lights between your last selected hue and your immutable `fixed_color` (which defaults to a warm, solid white).
*   **Smart Restore Logic:** A short press from any external state (like a CCT change, an effect, or a Home Assistant control change) immediately restores your last selected hue. This makes it easy to get back to your custom color.
*   **Effects Interaction:** Toggling or cycling hues will stop any running light effect and switch the light to a solid color mode.
*   **Cycle Lock:** While cycling, the helper locks the release color immediately to prevent ESPHome from shifting or jumping the output when the button is released.

---

## Effect Selector

`cfx_effect_selector` cycles through a list of configured light effects. A short press toggles the lights on or off. A long press cycles through the effects one by one, and releasing the button keeps the selected effect running.

```yaml
binary_sensor:
  - platform: gpio
    id: desk_lamp_fx_button
    pin:
      number: GPIO08
      mode: INPUT_PULLUP
      inverted: true

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

### Configuration Options

| Option | Default | Description |
| --- | --- | --- |
| `id` | optional | Internal effect selector helper ID. Generated automatically when omitted. |
| `lights` | required | One or more light entities to control. |
| `effects` | required | One or more effect names to cycle through. Names must match the target light effect names. |
| `long_press` | `500ms` | Press duration before effect selection starts. |
| `effect_interval` | `900ms` | Time between effect changes while the button remains held. |
| `restore` | `false` | Restore the last selected effect after reboot. |

### Key Features & Behaviors

*   **Short Press Actions:** Toggles the target lights on and off. Turning the light on restores the last active effect (or defaults to the first effect in the list after a boot).
*   **Long Press Cycling:** 
    *   Holding the button cycles through your configured `effects` list one by one.
    *   If the lights are currently off, holding the button first turns the light ON with the last active effect, and then continues cycling after the configured delay.
    *   Releasing the button locks the currently active effect.
*   **Save/Restore:** Setting `restore: true` saves the last active effect to flash memory so it is restored automatically after a reboot.
