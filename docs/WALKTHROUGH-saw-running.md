# Walkthrough: Saw and Running Effects Implementation

I have implemented the **Saw** (ID 16) and **Running Lights** (ID 15) effects in ChimeraFX, porting the logic from WLED.

## Changes

### 1. `cfx_utils.h`
- Added `map` helper function (standard Arduino/WLED logic) to ensure compatibility.

### 2. `cfx_addressable_light_effect.cpp`
- **Updated Default Configuration**:
  - `FX_MODE_RUNNING_LIGHTS` (15) and `FX_MODE_SAW` (16) now default to **Palette 255 (Solid)**.
  - This ensures they use the primary color by default, matching WLED behavior.

### 3. `CFXRunner.cpp`
- **Implemented `running_base`**: A helper function replicating WLED's sine/sawtooth logic.
  - *Note*: Dual mode support is partially stubbed but disabled as it was not requested.
- **Implemented Modes**:
  - `mode_running_lights()`: Calls `running_base(false)`.
  - `mode_saw()`: Calls `running_base(true)` (enables sawtooth calculation).
- **Registered Effects**:
  - Added cases `FX_MODE_RUNNING_LIGHTS` and `FX_MODE_SAW` to the main service switch.

## Verification

### Manual Testing Required
Since these are visual effects, please compile and flash the updated component to your device.

1.  **Select "Saw" Effect**:
    - Verify it uses the Primary Color (Solid).
    - Verify smooth sawtooth transitions.
    - Check Speed and Intensity controls (default 128).
2.  **Select "Running" Effect**:
    - Verify it uses the Primary Color (Solid).
    - Verify sine wave transitions.
3.  **Mirror Check**:
    - Toggle the "Mirror" switch and verify the effect reflects from the center/ends as expected.

## Next Steps
- Compile and upload to your ESP device.
- Enjoy the new effects!
