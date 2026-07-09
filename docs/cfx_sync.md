# ChimeraFX Sync

`cfx_sync` lets one light share its state with other devices.

It is designed for ChimeraFX lights, but the base sync uses ESPHome's normal light state. That means standard ESPHome lights can follow power, brightness, and supported color channels too. ChimeraFX-only features, like effects and live controls, are copied only when the receiving light also has those ChimeraFX features.

> **New feature:** Start with one leader and one follower. After that works, add controllers, satellites, or more groups.

## Simple Idea

Think about a sync group as a small room:

| Role | What It Does |
| --- | --- |
| `leader` | Owns the light state for the group. |
| `follower` | Copies the leader. |
| `controller` | Has an exposed button or switch, but no local synced light. It tells the leader what the user pressed. |
| `satellite` | Has a local light that follows the leader. It can also report local light changes or exposed local inputs back to the leader. |

Most users need only `leader` and `follower`.

## Which Role Should I Use?

Use this as the quick rule:

| Device Type | Use This Role | Why |
| --- | --- | --- |
| Main light for the group | `leader` | This light is the source of truth. |
| Extra light that should copy the main light | `follower` | It only receives the leader state. |
| Wall button with no local light | `controller` | It only sends button or switch input to the leader. |
| Light that should follow the group and can also be changed locally | `satellite` | It has a light, so it is part of the group. |
| Tuya MCU dimmer with hidden buttons | `satellite` | The buttons are not exposed, so `cfx_sync` watches the Tuya light state instead. |

The important distinction is simple:

- Use `controller` only for an input-only device.
- Use `satellite` whenever the device has its own synced light.

A controller cannot declare `lights`. A satellite must declare `lights`.

## First Test

Use the same `group` and `key` on every device in the group.

Leader:

```yaml
cfx_sync:
  id: room_sync
  role: leader
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
```

Follower:

```yaml
cfx_sync:
  id: room_sync
  role: follower
  lights: other_room_light
  group: living_room
  key: !secret cfx_sync_key
```

Secret:

```yaml
cfx_sync_key: "living-room-sync"
```

The key can be a normal string or a secret. Use at least 8 characters. You do not need to generate a hexadecimal key manually.

## One Leader, Many Follower Lights

A follower can apply the same leader state to more than one local light.

```yaml
cfx_sync:
  id: room_sync
  role: follower
  lights:
    - segment_a
    - segment_b
    - segment_c
  group: living_room
  key: !secret cfx_sync_key
```

This is useful when one physical strip is split into multiple ESPHome lights or when one device has multiple lights that should act together.

## Enable Sync

ESP32 followers get an **Enable Sync** switch automatically.

When the switch is on, the follower listens to the leader. When it is off, the follower ignores sync packets until you turn it on again. Turning it back on asks the leader for a fresh state, so you do not need to wait for the heartbeat.

ESP8266 followers and satellites use UDP and do not get the automatic **Enable Sync** switch in this first version.

## Remote Push Button

Use `role: controller` when a device has only a button or switch and no synced light.

Do not use `controller` for a dimmer or light device that also belongs to the group. Use `satellite` for that case.

Controller:

```yaml
binary_sensor:
  - platform: gpio
    id: wall_button
    pin:
      number: GPIO10
      mode:
        input: true
        pullup: true
      inverted: true

cfx_sync:
  id: room_sync
  role: controller
  local_input: wall_button
  group: living_room
  key: !secret cfx_sync_key
```

Leader:

```yaml
cfx_sync:
  id: room_sync
  role: leader
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
```

Because `input_mode` defaults to `momentary`, a short press toggles the leader light. You can leave `input_mode` out for normal push buttons.

## Rocker Switches

Use `input_mode: maintained` for a real on/off rocker switch.

```yaml
cfx_sync:
  id: room_sync
  role: controller
  local_input: wall_switch
  input_mode: maintained
  group: living_room
  key: !secret cfx_sync_key
```

In maintained mode:

| Switch Edge | Leader Action |
| --- | --- |
| ON | Turn on |
| OFF | Turn off |

Use `input_mode: toggle` only when both stable switch edges should toggle the leader. That is uncommon for normal wall wiring, but useful for some latching or relay-style inputs.

## When To Use `input_mode`

`input_mode` is only for a `local_input` that points directly to a plain ESPHome `binary_sensor`.

| Local Input | Use `input_mode`? | Why |
| --- | --- | --- |
| Plain push button `binary_sensor` | Optional | Default is `momentary`, so a press toggles the leader. |
| Plain rocker switch `binary_sensor` | Yes | Use `maintained` when ON should mean ON and OFF should mean OFF. |
| `cfx_button` id | No | The `cfx_button` already sends its own actions, such as primary, dimmer up, and dimmer down. |

If `local_input` points to a `cfx_button`, do not add `input_mode`. It is not needed.

## Remote Dimmer And Magic Buttons

`cfx_button` can be used from a controller or satellite. The sender sends a resolved command to the leader, such as toggle, dimmer up/down, RGB color, or white/CCT value. The leader applies that command to its own light, then sends the resulting authoritative light state to the group.

The leader does not need `remote_input` for normal remote magic buttons.

Controller:

```yaml
binary_sensor:
  - platform: gpio
    id: wall_button
    pin:
      number: GPIO10
      mode:
        input: true
        pullup: true
      inverted: true

cfx_button:
  - id: wall_dimmer
    button: wall_button
    dimmer:
      lights: []

cfx_sync:
  id: room_sync
  role: controller
  local_input: wall_dimmer
  group: living_room
  key: !secret cfx_sync_key
```

Leader:

```yaml
cfx_sync:
  id: room_sync
  role: leader
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
```

The empty `lights: []` on the controller is intentional. It lets the controller define the button behavior without controlling a local light.

When `local_input` points to `wall_dimmer`, `cfx_sync` receives the `cfx_button` actions directly. You may also point `local_input` to `main_button`; if exactly one `cfx_button` uses that physical button, `cfx_sync` will automatically use the richer `cfx_button` command. Do not add `input_mode` in this layout.

The leader applies only the command fields its own light supports. After that,
followers and satellites adapt the leader state to their own light type.

For a controller with separate dimmer buttons:

```yaml
binary_sensor:
  - platform: gpio
    id: main_button
    # pin: ...

  - platform: gpio
    id: up_button
    # pin: ...

  - platform: gpio
    id: down_button
    # pin: ...

cfx_button:
  - id: wall_dimmer
    button: main_button
    dimmer:
      lights: []
      inputs:
        up: up_button
        down: down_button

cfx_sync:
  id: room_sync
  role: controller
  local_input: wall_dimmer
  group: living_room
  key: !secret cfx_sync_key
```

If the button is physically connected to the leader, keep it local and use the normal `cfx_button` configuration. You do not need `cfx_sync` for that local button.

## Satellite Devices

Use `role: satellite` when a device has a local light that follows the group and also has a local input for the leader.

A satellite is still a light device first. The leader remains authoritative, but the satellite can contribute local changes back to the group.

```yaml
binary_sensor:
  - platform: gpio
    id: bedside_button
    pin:
      number: GPIO10
      mode:
        input: true
        pullup: true
      inverted: true

cfx_sync:
  id: room_sync
  role: satellite
  lights: bedside_light
  local_input: bedside_button
  group: living_room
  key: !secret cfx_sync_key
```

The satellite light still follows the leader. The local input is sent to the leader, and the leader remains authoritative.

If the satellite uses a `cfx_button`, point `local_input` to the `cfx_button` id and omit `input_mode`. You can also point `local_input` to the physical binary sensor used by that `cfx_button`; `cfx_sync` will detect the magic-button binding automatically.

```yaml
cfx_sync:
  id: room_sync
  role: satellite
  lights: bedside_light
  local_input: bedside_dimmer
  group: living_room
  key: !secret cfx_sync_key

cfx_button:
  - id: bedside_dimmer
    button: bedside_button
    dimmer:
      lights:
        - bedside_light
      inputs:
        up: up_button
        down: down_button
```

ESP8266 followers and satellites use UDP and can follow normal ESPHome light state. They do not run ChimeraFX effects or apply ChimeraFX controls.

## Tuya MCU Dimmers With Hidden Buttons

Some Tuya MCU dimmers do not expose their physical buttons as ESPHome `binary_sensor` entities. The secondary MCU handles the buttons and only reports the final light state.

For that layout, make the device a `satellite` and enable `local_light_input`.

Do not configure this kind of Tuya dimmer as a `controller`. There is no exposed GPIO button for `controller` mode to listen to.

```yaml
cfx_sync:
  id: room_sync
  role: satellite
  lights:
    - tuya_light
  local_light_input: true
  group: living_room
  key: !secret cfx_sync_key

tuya:

uart:
  rx_pin: GPIO3
  tx_pin: GPIO1
  baud_rate: 9600

light:
  - platform: tuya
    id: tuya_light
    name: Dimmer test
    dimmer_datapoint: 2
    switch_datapoint: 1
    min_value_datapoint: 50
    min_value: 25
    max_value: 255
```

When the Tuya MCU changes `tuya_light`, the satellite sends that new light state to the leader. The leader remains authoritative and sends the final state back to the group.

Use this only when the device buttons are not exposed as ESPHome binary sensors. If you have normal GPIO buttons, prefer `local_input` or a `cfx_button`.

The satellite must have exactly one light when `local_light_input: true` is used.

## What Gets Copied

`cfx_sync` watches the leader light. When the leader changes, followers are updated automatically.

| Feature | ChimeraFX Follower | Normal ESPHome Follower |
| --- | --- | --- |
| ON/OFF | Yes | Yes |
| Brightness | Yes | Yes, if the light supports brightness |
| RGB color | Yes | Yes, if the light supports RGB |
| White channel | Yes | Yes, if the light supports white |
| Color temperature | Color temperature when both lights support it. | Color temperature when both lights support it. |
| Cold/warm white | Cold white and warm white channels when both lights support them. | Cold white and warm white channels when both lights support them. |
| ChimeraFX effects | Yes, if the same effect exists on the follower | No |
| ChimeraFX controls | Yes, if the same control exists on the follower | No |

Not copied:

- Home Assistant entity names.
- Non-ChimeraFX effects and lambda effects.
- Arbitrary ESPHome automations.
- Full ChimeraFX rendering on ESP8266.

## Normal ESPHome Lights

You can use `cfx_sync` with a normal ESPHome light when you only need the usual light state.

Examples:

- PWM, Tuya, or monochrome ESPHome light.
- ESPHome RGB, RGBW, RGBWW, or CWWW light.
- A simple relay-style light that only supports ON/OFF.

For these lights, `cfx_sync` applies only the fields supported by the follower. A Tuya dimmer, for example, can follow ON/OFF and brightness while ignoring color.

On ESP8266, the default `transport: auto` uses UDP automatically. You normally do not need to write `transport: udp` yourself. ESP8266 devices cannot be leaders in this version.

## Mixed Light Types

Best results come from matching the leader and follower light type. RGBW to RGBW is predictable. RGB to RGB is predictable. Mixed lights are supported, but unsupported fields are ignored.

| Leader Change | Follower Without That Feature |
| --- | --- |
| RGB color | RGB followers copy it. Monochrome followers ignore color and keep following brightness. |
| RGBW white channel | RGB followers approximate white only when the packet also carries RGB color. |
| RGBWW/CWWW white-only change | Followers without cold/warm white ignore the white-only change. |
| Brightness | Followers copy it if brightness is supported. |
| Effect | Normal ESPHome lights ignore it. ChimeraFX followers use `None` if the effect is missing. |

If the leader sends a white-only change and the follower has no white channels, the white-only change is ignored. The follower keeps its current color, but it still follows ON/OFF and brightness changes from the leader.

This behavior is intentional. It avoids surprising color jumps when an RGB-only or monochrome follower cannot represent the leader's white channels.

## Multiple Groups

A device can belong to more than one sync group by declaring more than one `cfx_sync` block. Each block owns its own `group`, `key`, role, and light list.

```yaml
cfx_sync:
  - id: left_sync
    role: leader
    lights: left_light
    group: left_side
    key: !secret cfx_sync_key

  - id: right_sync
    role: leader
    lights: right_light
    group: right_side
    key: !secret cfx_sync_key
```

Use a different group when you want a different set of lights to act together.

## Transport

The recommended setting is the default:

```yaml
cfx_sync:
  id: room_sync
  role: follower
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
  transport: auto
```

`transport: auto` means:

| Device | Auto Behavior |
| --- | --- |
| ESP32 leader | ESP-NOW plus UDP bridge |
| ESP32 follower/controller/satellite | ESP-NOW |
| ESP8266 follower/controller/satellite | UDP |

You can force a transport when needed:

```yaml
cfx_sync:
  id: room_sync
  role: follower
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
  transport: udp
```

Use UDP when:

- The device is ESP8266.
- The device uses Ethernet or another setup where ESP-NOW is not suitable.
- You want to avoid Wi-Fi channel limitations.

ESP-NOW is usually faster for ESP32-to-ESP32 input sync, especially buttons and dimmers. UDP is more universal, but it may feel slightly slower for button ON/OFF actions.

## ESP-NOW And Mesh Wi-Fi

ESP-NOW works only when devices are on the same Wi-Fi channel.

This matters with mesh Wi-Fi. Two mesh nodes can share the same network name while using different channels. If one device joins channel 1 and another joins channel 6, ESP-NOW packets will not cross that gap.

Recommended mesh setup:

- Keep mesh APs on the same 2.4 GHz channel when using ESP-NOW sync.
- Avoid `fast_connect` unless you already know why you need it.
- If needed, pin an ESPHome device to a known BSSID in the normal `wifi:` configuration.

This is an ESP-NOW rule, not a ChimeraFX limitation.

## Offline Fallback

ESP-NOW can keep working without the router only if devices are on the same fallback channel.

```yaml
cfx_sync:
  id: room_sync
  role: follower
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
  fallback_channel: 6
```

Default fallback channel is `6`.

This is best effort. ESPHome may still reboot a device if Wi-Fi stays down long enough. `cfx_sync` does not disable ESPHome's normal Wi-Fi behavior.

## Options

| Option | Required | Default | Notes |
| --- | --- | --- | --- |
| `id` | Recommended | Generated | Use an explicit id for clarity. |
| `role` | Yes | - | `leader`, `follower`, `controller`, or `satellite`. |
| `lights` | For leader, follower, satellite | - | One light for a leader. One or more lights for followers and satellites. Not allowed on controllers. |
| `group` | Yes | - | Same group means same sync room. |
| `key` | Yes | - | Same key on every device in the group. Minimum 8 characters. |
| `local_input` | Controller or satellite | - | A `binary_sensor` id or a `cfx_button` id used as the local input. Use when the input is exposed to ESPHome. |
| `local_light_input` | Optional satellite | `false` | Watches the satellite light itself and sends local light changes to the leader. Use when the physical buttons are hidden behind a Tuya MCU or similar device. |
| `remote_input` | Optional leader | - | Legacy advanced hook for old remote magic-button layouts. Normal controller and satellite `cfx_button` inputs do not need it. |
| `input_mode` | No | `momentary` | Used only when `local_input` is a plain `binary_sensor`. Options: `momentary`, `maintained`, or `toggle`. |
| `heartbeat` | No | `30s` | Regular state refresh from leader. |
| `transport` | No | `auto` | `auto`, `espnow`, or `udp`. |
| `udp_port` | No | `45678` | Change only if another service uses the same port. |
| `fallback_channel` | No | `6` | Used by ESP-NOW offline fallback. |

## Troubleshooting

No follower reaction:

- Check every device uses the same `group`.
- Check every device uses the same `key`.
- Check the leader is really `role: leader`.
- Check the follower uses `role: follower` or `role: satellite`.
- If using ESP-NOW, confirm devices are on the same Wi-Fi channel.
- If using UDP, confirm the devices are on the same network and can reach each other.

Button does nothing:

- `controller` and `satellite` need `local_input`.
- A leader does not need `remote_input` for normal controller or satellite `cfx_button` commands.
- Plain momentary buttons can omit `input_mode`; the default is `momentary`.
- Real on/off rocker switches usually use `input_mode: maintained`.
- If `local_input` points to a `cfx_button`, remove `input_mode`.
- If `local_input` points to a binary sensor already used by exactly one `cfx_button`, `cfx_sync` treats it as that `cfx_button` automatically.

Follower color looks different:

- Match the leader and follower light type for best results.
- RGB-only lights cannot reproduce a real white channel.
- Monochrome and Tuya dimmers follow power and brightness, but ignore color.

Follower logs are very quiet:

- That is expected. Healthy sync traffic is mostly verbose-level logging.
- Warnings remain visible for missing effects, missing controls, send failures, and startup recovery problems.
