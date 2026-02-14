# Saw and Running Effects Implementation Plan

## Goal Description
Implement "Saw" (ID 16) and "Running Lights" (ID 15) effects in ChimeraFX, porting the logic from WLED's `mode_saw` and `mode_running_lights`. Both effects share a common base logic `running_base` and should support mirroring and use the Solid palette (255) by default. Speed and Intensity defaults are 128.

## User Review Required
> [!NOTE]
> These effects will rely on `running_base` helper function which attempts to replicate WLED's behavior exactly, including the specific sawtooth wave generation.

## Proposed Changes

### ChimeraFX Component

#### [MODIFY] [cfx_addressable_light_effect.cpp](file:///c:/Users/effel/OneDrive/Desktop/Antigravity_projects/chimera_fx/components/cfx_effect/cfx_addressable_light_effect.cpp)
- Update `get_default_palette_id_` to return `255` (Solid) for effect IDs 15 (`FX_MODE_RUNNING_LIGHTS`) and 16 (`FX_MODE_SAW`).
- Verify `get_default_speed_` and `get_default_intensity_` default to 128 (already handling this in default case).

#### [MODIFY] [CFXRunner.cpp](file:///c:/Users/effel/OneDrive/Desktop/Antigravity_projects/chimera_fx/components/cfx_effect/CFXRunner.cpp)
- Add `running_base(bool saw, bool dual)` helper function.
- Implement `mode_saw()` calling `running_base(true)`.
- Implement `mode_running_lights()` calling `running_base(false)`.
- Update `CFXRunner::service()` switch statement to handle cases 15 and 16.

## Verification Plan

### Automated Tests
- None available for visual effects.

### Manual Verification
1. **Compile**: Ensure the project compiles without errors.
2. **Deploy**: Flash to device.
3. **Test Saw Effect**:
   - Select effect "Saw".
   - Verify it uses primary color (Solid).
   - Verify Speed 128 and Intensity 128 defaults are applied.
   - Test Mirror toggle.
4. **Test Running Lights Effect**:
   - Select effect "Running".
   - Verify it uses primary color (Solid).
   - Verify defaults.
   - Test Mirror toggle.
