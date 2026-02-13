# Plan: Import Chase 2, BPM, Glitter Effects

## 📋 Context
**User Goal:** Import three specific WLED effects into ChimeraFX:
1. **Chase 2** (`FX_MODE_CHASE_COLOR`, ID 28) — Standard chase with intensity controlling width.
2. **BPM** (`FX_MODE_BPM`, ID 68) — Beat-synced pulsing stripes.
3. **Glitter** (`FX_MODE_GLITTER`, ID 87) — Standard Glitter effect over palette.

**Constraints:**
- Must be ported faithfully from WLED `FX.cpp`.
- Must support palettes (especially Solid/Rainbow defaults as requested).
- Must support Mirror/Reverse where applicable.
- Dependencies (`beatsin8_t`, helpers) must be ported from WLED.

---

## 🛠️ Technical Implementation Strategy

### 1. Prerequisites (Infrastructure)
The BPM effect relies on `beatsin8_t`, a WLED-specific timing function. This function recursively depends on `beat8` and `sin8`, which are currently missing in `FastLED_Stub.h`.

**Action:** Add the following to `cfx_utils.h` (sourced from WLED `util.cpp` and FastLED):
1. `sin8(uint8_t)` — Fast sine approximation (0-255).
2. `beat8(accum88)` — Beat generator.
3. `beatsin8_t(...)` — WLED's advanced beat generator.

### 2. Helper Functions
The requested effects rely on shared helper functions in `FX.cpp`.

**Action:** Add to `CFXRunner.cpp` (as static helpers):
- `chase(color1, color2, color3, do_palette)` — Core logic for all Chase effects.
- `glitter_base(intensity, color)` — Core logic for Glitter.

### 3. Effect Implementations

#### **Chase 2 (`mode_chase_color`)**
- **Logic:** Calls `chase()` with specific parameters.
- **WLED Source:** `FX.cpp` lines 993-995.
- **ChimeraFX:** Implement as `mode_chase_color`.
- **Controls:** Speed (chase speed), Intensity (width of chase).

#### **BPM (`mode_bpm`)**
- **Logic:** Uses `beatsin8_t` to modulate palette index.
- **WLED Source:** `FX.cpp` lines 2231-2239.
- **ChimeraFX:** Implement as `mode_bpm`.
- **Controls:** Speed (BPM) (Default: 64).

#### **Glitter (`mode_glitter`)**
- **Logic:** Calls `glitter_base` over a palette background.
- **WLED Source:** `FX.cpp` lines 3438-3450.
- **ChimeraFX:** Implement as `mode_glitter`.
- **Controls:** Speed (background speed), Intensity (glitter amount).

---

## 📅 Task Breakdown

### Phase 1: Infrastructure
- [ ] Port `sin8`, `beat8`, and `beatsin8_t` to `cfx_utils.h`.
- [ ] Verify compilation of new utilities.

### Phase 2: Implementation
- [ ] Add `chase` static helper to `CFXRunner.cpp`.
- [ ] Add `glitter_base` static helper to `CFXRunner.cpp`.
- [ ] Implement `mode_chase_color` (ID 28).
- [ ] Implement `mode_bpm` (ID 68).
- [ ] Implement `mode_glitter` (ID 87).

### Phase 3: Registration
- [ ] Register new effects in `CFXRunner` effect table.

---

## 🔍 Verification Checklist

- [ ] **Chase 2:**
    - [ ] Select ID 28.
    - [ ] Verify chasing pattern.
    - [ ] Verify Intensity slider changes width.
    - [ ] Verify Palette change works (Solid vs Rainbow).
- [ ] **BPM:**
    - [ ] Select ID 68.
    - [ ] Verify pulsing stripes.
    - [ ] Verify Speed slider changes BPM.
- [ ] **Glitter:**
    - [ ] Select ID 87.
    - [ ] Verify background palette animation.
    - [ ] Verify white sparkles appearing.
    - [ ] Verify Intensity slider changes sparkle density.
