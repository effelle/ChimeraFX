# Tidal Surge (ID 186)

**Tidal Surge** is a specialized monochromatic layout effect that presents a state-aware oscillation sequence mimicking the ebbs and flows of a wave. Rather than simply fading or wiping into the target color, the strip breathes its progress back and forth across specific lighting percentages before finally committing to a full 100% lit state.

This effect uses the "Intro/Outro" pattern internally. The core effect itself is a static hold of 100% lit color. The dynamic animation is purely provided by an organic Intro animation, mirrored symmetrically by its corresponding Outro animation.

## Animations

### Intro
The intro sequence animates a wipe traversing through a predefined set of waypoints:
`[30%, 20%, 50%, 20%, 100%]`

The overall intro duration set by the user is divided equally among these segments, creating a distinct "two steps forward, one step back" rhythm.

### Outro
The outro mirrors the personality of the intro during the shutdown phase, retracting the light backward through the following waypoints:
`[100%, 20%, 50%, 20%, 30%]`

After reaching the final 30% waypoint, the effect cleanly clears the strip to black.

## Milestone Suppression Architecture
Because of the oscillating nature of the intro sequence, the leading pixel naturally sweeps past the same percentage marks multiple times (e.g., reaching 20% on the way up, falling back past it, and crossing it again on the next surge).

ChimeraFX employs a **milestone suppression architecture** (specifically via the `last_fired_milestone` cache) to prevent duplicate sequence events. 

When configuring sequence milestones (e.g. `cfx_reach:20` or trigger conditions in HA), you can be assured that:
- **Events fire only once**: The `cfx_reach` milestone event for a specific percentage (like 20% or 50%) is fired on the *first* crossing of that threshold.
- **No duplicate triggers**: As the wave recedes and surges again across those same percentages, the events will remain suppressed.

This ensures sequences triggered by Tidal Surge milestones remain stable and predictable.
