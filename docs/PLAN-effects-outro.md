# Project Plan: Auto-Registration & Outro Support

## Goal
Implement two new features in ChimeraLight:
1. **Effect Auto-Registration**: Offer an `all_effects: true` option in YAML to automatically register the 52 supported ChimeraFX effects, cleanly replacing the verbose `chimera_fx_effects.yaml` include.
2. **Outro Animation**: Intercept ESPHome's `turn_off` routine within `ChimeraLightOutput` to play an outro sequence before switching the LEDs off. The outro will be an inverted playback of the configured Intro animation (e.g., a "wipe-in" intro becomes a "wipe-out" outro).

## Phase 1: Context Check
We analyzed ESPHome's light state machine and our `register_addressable_effect` decorator.
- **Auto-registration** must happen at compile-time via our Python codegen (`light.py`). We will add an `all_effects` flag and use the curated 52-effect list (not the 114 engine list).
- **Outro intercept** must happen in `ChimeraLightOutput::write_state()`. We can detect when the target state changes to OFF and block the hardware update while signaling `CFXRunner` to play the outro.

## Phase 2: Socratic Gate (Completed)
- Q: "How to handle 114 effects?" -> A: We only use the 52 supported/tested effects from `chimera_fx_effects.yaml`.
- Q: "How should the outro look?" -> A: It will strictly be the *inverse* of the selected intro. If the intro draws from center out, the outro erases from outside in. If an effect cannot be inverted (e.g., glitter), we skip/fade.

## Phase 3: Technical Architecture

### 1. Effect Auto-Registration (Python Codegen)
**Component**: `components/chimera_light/light.py`
- Add `CONF_ALL_EFFECTS` to the config schema (default `false`).
- In `to_code()`, if `CONF_ALL_EFFECTS` is true:
  - Generate a Python dictionary mapping the 52 exact effect names to their `effect_id`s (matching `chimera_fx_effects.yaml`).
  - Iterate through this dictionary and call `cg.add(var.add_effect(cfx_effect_to_code(...)))` for each.
  - *Conflict resolution*: Merge with the `effects:` list. If a user explicitly defines an effect with presets in their YAML, it overrides the auto-registered one with the same ID.

### 2. Outro State Machine (C++)
**Component**: `components/chimera_light/chimera_light.cpp`
- **Detection**: In `write_state(LightState *state)`, compare `state->current_values.is_on()` with a new variable `this->was_on_`.
- If ON → OFF edge transitions:
  1. Trigger `runner->start_outro()`.
  2. Block the OFF state from reaching the RMT hardware by returning early.
- While Outro is running:
  1. `write_state` continues to receive calls (ESPHome runs the transition loop).
  2. We keep rendering the CFX buffer to RMT.
- When Outro finishes:
  1. Clear the block.
  2. Let ESPHome's actual 0-brightness OFF state write to the strip.

### 3. Outro Rendering Logic (CFXRunner)
**Component**: `src/CFXRunner.h` & `src/CFXRunner.cpp`
- Implement `start_outro()`: Setup state flags indicating an outro is active.
- Modify the Intro routing logic to support an `inverted` flag.
  - Example: `intro_wipe` normally fills `0` to `len`. Inverted, it empties from `len` down to `0`.
  - Example: `intro_fade` normally goes `0` to `255`. Inverted, it goes `255` down to `0`.
- The `is_running_intro` flag checks will need to be generalized to `is_transitioning()` to handle both intro and outro phases.

## Phase 4: Implementation Checklist
- [ ] Add `all_effects` boolean to `chimera_light` schema
- [ ] Implement `all_effects` loop in `light.py` using the 52-effect dictionary
- [ ] Test YAML compilation with `all_effects: true`
- [ ] Add `bool is_outro_{false};` and `bool was_on_{false};` to `ChimeraLightOutput`
- [ ] Implement `start_outro()` in `CFXRunner`
- [ ] Add `invert` parameter/logic to intro rendering functions (e.g., `intro_wipe`, `intro_fade`)
- [ ] Intercept ON→OFF edge in `ChimeraLightOutput::write_state`
- [ ] Delay RMT transmission of OFF state until `CFXRunner` signals outro complete
- [ ] Test ON/OFF cycles with various Intros (Fade, Wipe, Center) to verify Outro behavior

## Agents
- `project-planner` (Authored this plan)
- `backend-specialist` (Will implement C++ logic and transition state machine)
- `orchestrator` (Will coordinate codegen and C++ implementation)
