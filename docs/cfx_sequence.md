# ChimeraFX Sequencer

The ChimeraFX Sequencer (`cfx_sequence`) is a powerful orchestration component that allows you to create complex, event-driven lighting sequences. It can control one or more ChimeraFX lights, applying specific effects with optional overrides for speed, intensity, and palette, and can chain sequences together based on triggers.

## Configuration Overview

A sequence defines which light(s) to control, which effect to run, and optional parameters to override the default effect settings.

```yaml
cfx_sequence:
  - name: "Sweep Sequence"
    light_id: rgb_light
    effect: "Wipe"
    speed: 150
    intensity: 200
    palette: "Rainbow"
    iterations: 2
    on_complete:
      - cfx_sequence.start: next_sequence
```

### Configuration Variables

* **name** (*string*, Required): The unique name of the sequence. This is displayed in the "Active Sequence" select dropdown.
* **light_id** (*ID* or *list of IDs*, Required): The ID(s) of the `cfx_light` component(s) to control.
* **effect** (*string*, Required): The name of the ChimeraFX effect to run (e.g., "Wipe", "Rainbow", "Fire").
* **speed** (*int*, Optional): Override the effect's default speed (0-255).
* **intensity** (*int*, Optional): Override the effect's default intensity (0-255).
* **palette** (*string* or *int*, Optional): Override the effect's default palette. You can use the palette name (e.g., "Rainbow") or its ID.
* **iterations** (*int*, Optional): The number of times the effect should repeat before finishing. If set to `0` (default), it runs indefinitely until stopped or a trigger occurs.

---

## Triggers

The Sequencer exposes several triggers that allow you to react to the sequence's progress and chain events.

### `on_start`
Fires exactly once when the sequence starts.

```yaml
cfx_sequence:
  - name: "Intro Sequence"
    # ...
    on_start:
      - logger.log: "Sequence started!"
```

### `on_complete`
Fires when the sequence finishes its requested number of `iterations`. This is commonly used to "chain" sequences together.

```yaml
cfx_sequence:
  - name: "Step 1"
    iterations: 1
    # ...
    on_complete:
      - cfx_sequence.start: step_2
```

### `on_reach`
Fires when the animation reaches a specific percentage (0.0 to 1.0) of the strip length.

```yaml
cfx_sequence:
  - name: "Middle Trigger"
    # ...
    on_reach:
      - position: 0.5  # 50%
        then:
          - logger.log: "Reached the middle of the strip!"
```

### `on_pixel_num`
Fires when the animation reaches a specific absolute pixel index.

```yaml
cfx_sequence:
  - name: "End Trigger"
    # ...
    on_pixel_num:
      - pixel: 299
        then:
          - logger.log: "Reached the last pixel!"
```

---

## "Leave No Trace" Behavior

The ChimeraFX Sequencer is designed with architectural integrity in mind. When a sequence starts, it takes a "snapshot" of the current state of the target lights (On/Off state, current color, and active effect).

When the sequence stops (either manually or by completing its iterations), it **gracefully restores** the light to exactly how it found it.

* If the light was **OFF** before the sequence, it will transition back to **OFF**.
* If the light was running a different effect, it will transition back to that effect.
* The restoration uses a smooth 1-second transition to prevent jarring visual snaps.

---

## Active Sequence Control

When you define one or more sequences, a select entity named **"Active Sequence"** is automatically exposed to Home Assistant. Selecting a sequence from the dropdown starts it, and selecting **"None"** stops the active sequence and restores the previous light state.

---

## Actions

### `cfx_sequence.start`
Starts a specific sequence.

```yaml
on_...:
  then:
    - cfx_sequence.start: my_sequence_id
```

### `cfx_sequence.stop`
Stops the specified sequence (or all sequences if no ID is provided).

```yaml
on_...:
  then:
    - cfx_sequence.stop: my_sequence_id
```
