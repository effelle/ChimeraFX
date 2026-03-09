# ChimeraFX Sequencer

The ChimeraFX Sequencer (`cfx_sequence`) is a powerful orchestration component that allows you to create complex, event-driven lighting sequences. It can control one or more ChimeraFX lights, or segments of a light, applying specific effects with optional overrides for speed, intensity, palette, and brightness. Sequences can chain together based on triggers.

## Configuration Overview

```yaml
cfx_sequence:
  - id: my_sequence
    name: "Sweep Sequence"
    lights: [rgb_light]
    effect: "Wipe"
    set_speed: 150
    set_intensity: 200
    set_palette: 5
    set_brightness: 80%
    iterations: 2
    restore: true
    on_complete:
      - cfx_sequence.start: next_sequence
```

### Configuration Variables

| Variable | Type | Required | Default | Description |
|----------|------|----------|---------|-------------|
| **id** | ID | Yes | — | Unique identifier for referencing in actions and triggers |
| **name** | string | Yes | — | Display name in the "Active Sequence" select dropdown |
| **lights** | list of IDs | Yes | — | Target `cfx_light` or segment light IDs |
| **effect** | string | Yes | — | ChimeraFX effect name (e.g., "Wipe", "Chase", "Fire") |
| **set_speed** | int (0-255) | No | — | Override the effect's default speed |
| **set_intensity** | int (0-255) | No | — | Override the effect's default intensity |
| **set_palette** | int (0-255) | No | — | Override the effect's default palette by ID |
| **set_brightness** | percentage | No | — | Set the light brightness (e.g., `80%`, `0.5`) |
| **iterations** | int | No | 0 | Number of effect cycles before `on_complete`. 0 = run indefinitely |
| **restore** | boolean | No | true | Restore the light's previous state when the sequence stops |

> [!NOTE]
> `set_speed`, `set_intensity`, and `set_palette` override the shared controls during the sequence. When the sequence stops (with `restore: true`), the previous values are restored.

---

## Triggers

### `on_start`
Fires exactly once when the sequence starts.

```yaml
on_start:
  - logger.log: "Sequence started!"
```

### `on_complete`
Fires when the sequence finishes all `iterations`. Used to chain sequences.

```yaml
iterations: 1
on_complete:
  - cfx_sequence.start: next_step
```

### `on_reach`
Fires when a **progressive effect** reaches a specific percentage of the strip.

```yaml
on_reach:
  - position: 50%
    then:
      - light.turn_on: {id: led_strip, red: 1, green: 0, blue: 0}
```

### `on_pixel_num`
Fires when a **progressive effect** reaches a specific absolute pixel index.

```yaml
on_pixel_num:
  - pixel: 30
    then:
      - light.turn_on: {id: led_strip, brightness: 80%}
```

> [!NOTE]
> **Position-based triggers** (`on_reach` and `on_pixel_num`) require a **Progressive Effect** — effects with a clear leading edge that moves across the strip. Compatible effects are marked with :material-bullseye-arrow: in the [Effects Library](Effects-Library.md).

---

## State Restoration

When `restore: true` (default), the sequencer takes a snapshot of the light's state before starting (On/Off, brightness, color, and active effect). When the sequence stops, it restores that state instantly.

- If the light was **OFF** before → it returns to **OFF**
- If the light was running a different effect → it switches back
- `restore: false` → the light stays in the sequence's final state

---

## Active Sequence Control

When sequences are defined, a select entity **"Active Sequence"** is automatically exposed to Home Assistant. Selecting a sequence starts it; selecting **"None"** stops the active sequence.

---

## Actions

### `cfx_sequence.start`
```yaml
on_...:
  then:
    - cfx_sequence.start:
        id: my_sequence_id
```

### `cfx_sequence.stop`
```yaml
on_...:
  then:
    - cfx_sequence.stop:
        id: my_sequence_id
```
