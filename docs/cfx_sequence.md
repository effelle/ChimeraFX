# ChimeraFX Sequencer

The ChimeraFX Sequencer (`cfx_sequence`) is a powerful orchestration component that allows you to create complex, event-driven lighting workflows. It can control one or more ChimeraFX lights (or segments), applying specific effects with optional overrides for speed, intensity, palette, and brightness.

Sequences are the "Logic Layer" of your lighting—they allow segments to talk to each other, trigger handovers, and react to specific progress milestones.

---

## Configuration Overview

```yaml
cfx_sequence:
  - id: my_sequence
    name: "Standard Sweep"
    lights: [rgb_light]
    effect: "Wipe"
    set_speed: 150
    set_intensity: 200
    set_palette: 5
    set_brightness: 80%
    iterations: 1
    restore: true
    on_complete:
      - cfx_sequence.start: next_sequence
```

### Configuration Variables

| Variable | Type | Required | Default | Description |
|----------|------|----------|---------|-------------|
| **id** | ID | Yes | — | Unique identifier for referencing in actions |
| **name** | string | Yes | — | Display name in the "Active Sequence" selector |
| **lights** | list of IDs | Yes | — | Target `cfx_light` or segment light IDs |
| **effect** | string | Yes | — | Name of the effect to run (e.g., "Wipe", "Fire") |
| **set_speed** | int (0-255) | No | — | Force a specific speed for this sequence run |
| **set_intensity** | int (0-255) | No | — | Force a specific intensity for this sequence run |
| **set_palette** | int (0-255) | No | — | Force a specific palette ID |
| **set_brightness** | percentage | No | — | Set the light brightness (e.g., `80%`, `0.5`) |
| **iterations** | int | No | 0 | Cycles before `on_complete`. 0 = run indefinitely. |
| **restore** | boolean | No | true | Return to previous state when the sequence stops |

---

## Understanding Progressive Effects

Not all effects are created equal when it comes to the Sequencer. While any effect can be used in a sequence, only **Progressive Effects** support position-based triggers like `on_reach` and `on_pixel_num`.

### What makes an effect "Progressive"?
A progressive effect is one where the animation has a clear **leading edge** or **state progression** that moves across the strip over time. 
Examples include:
- **Wipes & Sweeps**: The light physically moves from one end to the other.
- **Filling Effects**: Like "Dropping Time," where the strip fills up like a bucket.
- **Single Cursors**: Like "Chase" or "Follow Me," where a distinct point moves.

### How to identify them
In the [Effects Library](Effects-Library.md), progressive effects are marked with the **Sequencer Ready** icon: :material-bullseye-arrow:.

When one of these effects is active, the engine tracks its "leading edge" pixel in real-time. This tracking is what allows the Sequencer to fire triggers at exactly 50% or pixel 45, even if the animation is moving very fast.

---

## Proximity & Progress Triggers

The sequencer can react to the physical progress of an effect.

### `on_reach` (Percentage)
Fires when a **Progressive Effect** reaches a specific threshold (percentage) of the strip. Ideal for cross-light handovers.

```yaml
on_reach:
  - position: 100%
    then:
      - light.turn_on: {id: next_strip, effect: "Breathe"}
```

### `on_pixel_num` (Discrete)
Fires at a specific absolute pixel index. Perfect for syncing effects with physical environmental features (e.g., "stair 5").

```yaml
on_pixel_num:
  - pixel: 45
    then:
      - logger.log: "Passed the hallway sensor point"
```

> [!TIP]
> **Crossing Logic**: Triggers are designed with "Crossing Detection." Even if your framerate is low, the system detects if the leading edge of an animation has passed your target, ensuring triggers are never skipped.

---

## Advanced Orchestration Patterns

### 1. The Relay (Serial Chain)
Sequences can be linked end-to-end to create a multi-stage animation.

```yaml
cfx_sequence:
  - id: stage_1
    name: "Relay Start"
    lights: [strip_1]
    iterations: 1
    on_complete:
      - cfx_sequence.start: stage_2

  - id: stage_2
    name: "Relay End"
    lights: [strip_1]
    # ... on_complete could loop back to stage_1
```

### 2. The Handover (Progress Chain)
One light "passes the baton" to the next as soon as the effect reaches its edge.

```yaml
cfx_sequence:
  - id: convergence
    name: "Push to Center"
    lights: [strip_1, strip_3]
    effect: "Curtain Sweep"
    on_reach:
      - position: 100%
        then:
          - light.turn_on: {id: strip_2, effect: "Breathe"}
```

### 3. The Master Override
A sequence can target a "Master" ID that represents multiple segments. The sequence takes ownership of the entire buffer, but you can still pulse individual segments within it.

```yaml
cfx_sequence:
  - id: master_sweep
    lights: [master_light]
    on_start:
      - delay: 2s
      - light.turn_on: {id: segment_1, red: 100%}
```

---

## State Resilience ("Chaos Mode")

The Sequencer is designed to handle manual user intervention gracefully. 

- **Manual Interruption**: If a sequence is running and you manually call `light.turn_on` for one of its target segments, the segment will obey your command (e.g., switching to "Solid Red").
- **Sequence Continuation**: The sequence remains "Active" in the background. If the sequence reaches a progress trigger (like `on_reach: 80%`), it can still fire even if the physical light is temporarily showing something else.
- **Auto-Recovery**: If `restore: true` is set, turning off the sequence via the "Active Sequence" selector will return the light correctly to its pre-sequence state, capturing any "chaos" changes made during the run.

---

## Summary of Logic
- **`iterations: 0`**: Runs forever. `on_complete` never fires.
- **`iterations: 1+`**: Once reached, `on_complete` fires. If `restore: true`, the lights return to their previous state automatically.
- **Nested Starts**: You can start a sequence from within another sequence's trigger. The new sequence will take over ownership of the lights it targets.

