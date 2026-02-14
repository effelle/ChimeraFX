# Implementation Plan: Running Dual & sin_gap

## Goal
Implement "Running Dual" effect (ID 52) and properly port `sin_gap` to fix the gap visualization in Running-based effects.

## Proposed Changes

### 1. `cfx_utils.h`
- Added `sin_gap` implementation from WLED.
  ```cpp
  inline uint8_t sin_gap(uint16_t in) {
    if (in & 0x100) return 0;
    return sin8(in + 192);
  }
  ```

### 2. `CFXRunner.cpp`
- **Update `running_base`**:
  - Replace `cfx::sin8` with `cfx::sin_gap` where appropriate.
  - Enable the `dual` logic (currently commented out).
  - Implement the dual wave blending (sine/sawtooth mixing).
- **Implement `mode_running_dual`**:
  - `return running_base(false, true);`
- **Register Effect**:
  - Add `case FX_MODE_RUNNING_DUAL:` to `service()`.

### 3. `cfx_addressable_light_effect.cpp`
- **Default Settings**:
  - Set default palette for `FX_MODE_RUNNING_DUAL` (ID 52) to `PaletteSakura` (idx 49) or `PaletteRedBlue` (if available) as requested, to ensure distinct colors.
  - Default Speed/Intensity to 128.

## Verification
- **Manual Verification**:
  - Select "Running Dual".
  - Verify two waves are visible.
  - Verify "gap" appearance in standard "Running Lights" corresponds to WLED (cleaner gaps).
