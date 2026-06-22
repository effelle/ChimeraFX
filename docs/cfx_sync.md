# ChimeraFX Sync

`cfx_sync` lets one ChimeraFX light control other ChimeraFX lights on nearby
ESP32 devices.

Use it when you want, for example:

- One lamp to copy another lamp.
- One master strip to control segments on another device.
- A small wall-button ESP32 to control a ChimeraFX light somewhere else.

It uses ESP-NOW, so the devices talk directly to each other. You do not need
Home Assistant automations, MQTT, or copied MAC addresses.

!!! warning "New feature"
    `cfx_sync` is ready for testing, but it is still new. Keep your first
    setup simple: one leader, one follower, then add more followers or a
    controller after the basic sync works.

## The Simple Idea

Every sync setup has one `group` and one `key`.

Devices with the same `group` and `key` can find and trust each other.

There are three roles:

| Role | What it does |
| --- | --- |
| `leader` | The light everyone follows. |
| `follower` | A light that copies the leader. |
| `controller` | A device with only a button or switch. |

## Sync Two Lights

Leader device:

```yaml
cfx_sync:
  id: room_sync
  role: leader
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
```

Follower device:

```yaml
cfx_sync:
  id: room_sync
  role: follower
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
```

In `secrets.yaml`:

```yaml
cfx_sync_key: "living-room-sync"
```

That is enough for the first test.

When the leader turns on, changes brightness, color, effect, or supported
ChimeraFX controls, the follower copies it.

## Sync One Leader To Multiple Follower Lights

A follower can apply the same leader state to more than one local light:

```yaml
cfx_sync:
  id: room_sync
  role: follower
  lights:
    - wall_segment_left
    - wall_segment_center
    - wall_segment_right
  group: living_room
  key: !secret cfx_sync_key
```

This is useful when the follower device has multiple segments and they should
all follow the same leader.

## Add A Remote Push Button

A controller device can have only a button and no ChimeraFX light.

Controller device:

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
  group: living_room
  key: !secret cfx_sync_key
  local_input: wall_button
```

Leader device:

```yaml
cfx_sync:
  id: room_sync
  role: leader
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
```

With this setup, pressing the remote button toggles the leader light. The
leader then syncs the new light state to all followers.

`input_mode` defaults to `momentary`, which is the right mode for normal push
buttons.

## Add A Rocker Switch

For a real ON/OFF switch, use `input_mode: maintained`.

Controller device:

```yaml
binary_sensor:
  - platform: gpio
    id: wall_switch
    pin:
      number: GPIO10
      mode:
        input: true
        pullup: true
      inverted: true

cfx_sync:
  id: room_sync
  role: controller
  group: living_room
  key: !secret cfx_sync_key
  local_input: wall_switch
  input_mode: maintained
```

With `maintained` mode:

- Switch ON turns the leader light ON.
- Switch OFF turns the leader light OFF.

## Add A Remote Dimmer Or Magic Button

If the remote button should dim, change CCT, cycle colors, or select effects,
put the ChimeraFX button behavior on the leader with `cfx_button`.

Leader device:

```yaml
cfx_button:
  - id: room_dimmer_button
    dimmer:
      lights:
        - room_light

cfx_sync:
  id: room_sync
  role: leader
  lights: room_light
  group: living_room
  key: !secret cfx_sync_key
  remote_input: room_dimmer_button
```

Controller device:

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
  group: living_room
  key: !secret cfx_sync_key
  local_input: wall_button
```

In this setup, the controller sends the button press to the leader. The
leader's `cfx_button` decides if it is a short press, long press, dimmer hold,
or another ChimeraFX button action.

Do not use `input_mode: maintained` for a dimmer button.

## Local Buttons On The Leader

If the button is physically connected to the leader device, you do not need
`cfx_sync` for that button.

For a simple local push button, use normal ESPHome:

```yaml
binary_sensor:
  - platform: gpio
    id: local_button
    pin:
      number: GPIO10
      mode:
        input: true
        pullup: true
      inverted: true
    on_press:
      - light.toggle: room_light
```

For a local dimmer, CCT button, hue cycler, or effect selector, use
[`cfx_button`](cfx_magic_buttons.md).

`cfx_sync` watches the leader light. When the leader changes, followers are
updated automatically.

## What Gets Copied

`cfx_sync` copies:

- ON/OFF state.
- Brightness.
- RGB and RGBW color.
- White channel when available.
- ChimeraFX effect selection.
- ChimeraFX controls such as Force White, Speed, Intensity, Palette, Mirror,
  Intro, Outro, and In/Out Duration when those controls exist.

It does not yet copy:

- Exact animation phase.
- Random effect seed.
- Multiple sync groups on the same device.
- ESP8266 devices.

## Effects And Presets

Effects are matched by ID and name.

If the leader uses `Energy`, the follower must also have the `Energy` effect.
If the leader uses a custom preset, the follower needs the same preset name and
effect ID.

If the follower cannot find the effect, it falls back to `None` and keeps the
synced power, brightness, and color.

Non-ChimeraFX and lambda effects are not synchronized yet.

## RGB And RGBW Lights

The best result comes from matching hardware:

- RGB leader to RGB follower.
- RGBW leader to RGBW follower.

Mixed RGB and RGBW devices are supported, but they may not look perfectly
identical. ChimeraFX converts the color in a predictable way:

- RGBW to RGB folds white into RGB.
- RGB to RGBW moves neutral RGB into the white channel.

Different strips, white LEDs, power supplies, and calibration can still make
two lights look slightly different.

## Wi-Fi And Mesh Networks

ESP-NOW needs all synced devices to be on the same 2.4 GHz Wi-Fi channel.

This matters especially with mesh Wi-Fi. A mesh system can use the same Wi-Fi
name on different mesh nodes, but those nodes may use different channels. If
two ESP32 devices are on different 2.4 GHz channels, they will not hear each
other over ESP-NOW.

Recommended setup:

- Put all 2.4 GHz mesh nodes used by ChimeraFX sync on the same channel.
- Check ESPHome logs for channel and BSSID if discovery is unreliable.
- Avoid `fast_connect` unless you are sure the device will join the correct AP.

## Offline Fallback Channel

`fallback_channel` is used when Wi-Fi is offline but the device is still
running:

```yaml
cfx_sync:
  # ...
  fallback_channel: 6
```

Default: `6`.

Use the same fallback channel on every device in the group.

!!! note
    This is best-effort. ESPHome may still reboot the device if Wi-Fi is down
    for too long. `cfx_sync` does not disable ESPHome's normal Wi-Fi behavior.

## Options

| Option | Default | Used by | Description |
| --- | --- | --- | --- |
| `id` | generated | all | ESPHome component ID. |
| `role` | required | all | `leader`, `follower`, or `controller`. |
| `lights` | none | leader, follower | The light or lights controlled by sync. |
| `group` | required | all | Name of the sync group. |
| `key` | required | all | Shared passphrase, 8 to 64 characters. |
| `heartbeat` | `30s` | leader, follower | Regular full-state update. Valid range: `10s` to `5min`. |
| `fallback_channel` | `6` | all | ESP-NOW channel used while Wi-Fi is offline. |
| `local_input` | none | controller | Binary sensor used by a controller device. |
| `input_mode` | `momentary` | controller | `momentary` for push buttons, `maintained` for rocker switches. |
| `remote_input` | none | leader | Optional leader-side `cfx_button` for remote dimmer or magic-button behavior. |

Important rules:

- A leader must have exactly one light.
- A follower must have at least one light.
- A controller must have `local_input` and no `lights`.
- `remote_input` is only for leaders.
- Do not use `espnow_id` or `peer`; discovery is automatic.

## Troubleshooting

Follower does nothing:

- Check that `group` and `key` are identical on every device.
- Check that every device is ESP32.
- Check that the devices are on the same 2.4 GHz Wi-Fi channel.
- On mesh Wi-Fi, check that the BSSID/channel did not split between mesh nodes.

Remote button does nothing:

- Check that the button device uses `role: controller`.
- Check that `local_input` points to the binary sensor ID.
- For a simple push button, leave `input_mode` as `momentary`.
- For a rocker switch, use `input_mode: maintained`.

Remote dimmer does not dim:

- The leader needs `remote_input`.
- `remote_input` must point to a `cfx_button`.
- The controller should use `input_mode: momentary`.

Follower effect becomes `None`:

- The follower does not have the same ChimeraFX effect ID and name.
- Non-ChimeraFX effects are not supported yet.
