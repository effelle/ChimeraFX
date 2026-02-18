# Plan: Kaleidos Effect (ID 155)

> **Status**: PROPOSED
> **Goal**: Implement a new "Kaleidos" effect with N-Way Symmetrical Mirroring.

## 🧠 Brainstorming Analysis

### 1. Symmetry Logic (Key Challenge)
The user proposed: `segments = map(intensity, 0, 255, 2, 8);`
*   **Issue**: Standard `map()` can output odd numbers (3, 5, 7) depending on implementation details and rounding, which breaks the "Forward/Backward" symmetry requirement. Use of integer math `map` is safer if we map to `1..4` then multiply by 2.
*   **Fix**: Force even numbers: `segments = map(intensity, 0, 255, 1, 4) * 2;` (Result: 2, 4, 6, 8).

### 2. Segment Length & Remainder Handling
`seg_len = len / segments;`
*   **Issue**: Integer division truncation. If `len=100`, `segments=8`, `seg_len=12`. Total covered = 96. Last 4 pixels are orphaned.
*   **Solution**: Since we iterate `i` from 0 to `len`, we must handle `seg_index >= segments`.
    *   **Clamp Strategy**: `if (seg_index >= segments) seg_index = segments - 1;`
    *   This forces the remainder pixels into the last segment. If the last segment is mirrored (odd index), the remainder pixels will continue the mirror pattern naturally, which is visually acceptable.

### 3. Density (Zoom Level)
User asked for "Fixed or derived".
*   **Option A (Fixed)**: Constant spatial frequency. As segments get smaller (higher intensity), we see *less* of the pattern.
*   **Option B (Dynamic)**: `density = 255 / seg_len`. We always see one full rainbow/cycle per segment.
*   **Recommendation**: Use a fixed base density (e.g., `32`) but scale it slightly with length to ensure it looks good on both 30 and 300 LEDs. A fixed density of `255 / 10` (approx 25) is a good starting point for detailed patterns.

### 4. Performance
*   **Optimization**: Move `millis() >> 4` out of the loop.
*   **Math**: Integer-based `ColorFromPalette` is efficient.
*   **Execution Time**: Expected to represent negligible load (O(N) complexity).

---

## 📋 Implementation Plan

### Phase 1: Core Implementation
Define the new effect function `mode_kaleidos` in `CFXRunner.cpp`.

#### 1. Define Constants
*   **ID**: 155
*   **Name**: "Kaleidos"
*   **Defaults**: Speed 128, Intensity 128, Palette Rainbow (4).

#### 2. Implement `mode_kaleidos` logic
```cpp
uint16_t mode_kaleidos(void) {
  // 1. Calculate Time Base
  uint32_t ms = millis();
  uint32_t cycle_time = ms >> 4; // Speed control via bitshift or variable

  // 2. Calculate Segments (Symmetry Engine)
  // Map Intensity 0-255 to 1-4, then double to get 2, 4, 6, 8
  uint8_t num_segments = map(instance->_segment.intensity, 0, 255, 1, 4) * 2;
  
  uint16_t len = instance->_segment.length();
  uint16_t seg_len = len / num_segments;
  if (seg_len == 0) seg_len = 1; // Safety

  // 3. Density
  // Fixed density or derived? Let's use a standard scaling factor
  // to ensure the pattern is detailed.
  uint8_t density = 20; 

  // 4. Render Loop
  for (int i = 0; i < len; i++) {
    // Determine Segment Index and Local Position
    uint16_t seg_index = i / seg_len;
    uint16_t local_pos = i % seg_len;

    // Handle Remainder Pixels (Clamp to last segment)
    if (seg_index >= num_segments) {
      seg_index = num_segments - 1;
      // Recalculate local_pos relative to the start of the last segment
      // local_pos = i - (seg_index * seg_len); 
      // Actually i % seg_len is still correct relative to the 'virtual' grid
      // but let's stick to simple clamp.
    }

    // Mirror Logic
    // Even = Forward, Odd = Backward
    if (seg_index & 0x01) {
      // Odd: Reverse
      local_pos = (seg_len - 1) - local_pos;
    }

    // Calculate Color Index
    // Pattern: Scrolling Palette
    uint8_t color_index = (local_pos * density) + cycle_time;

    // Map to Palette
    CRGBW color = ColorFromPalette(color_index, 255, 
                                   getPaletteByIndex(instance->_segment.palette));
    
    instance->_segment.setPixelColor(i, RGBW32(color.r, color.g, color.b, color.w));
  }

  return FRAMETIME;
}
```

### Phase 2: Registration
1.  **Register in `CFXRunner.cpp`**: Add `mode_kaleidos` to the `_mode_ptr` array at index 155.
2.  **Declare in `CFXRunner.cpp`**: Add forward declaration `uint16_t mode_kaleidos(void);`.
3.  **Update `CFXRunner.h`**: Add `#define FX_MODE_KALEIDOS 155`.

### Phase 3: Documentation
1.  Update `Effects-Library.md` (or `effects_preset.md` if applicable) to include "Kaleidos".

## ✅ Verification Checklist
- [ ] Effect compiles without errors.
- [ ] Intensity slider correctly changes segment count (2 -> 4 -> 6 -> 8).
- [ ] Odd segments mirror correctly (visual check: patterns meet at the boundaries).
- [ ] Pattern scrolls smoothly via Speed slider (if implemented, currrently fixed `millis() >> 4`).
    *   *Refinement*: Use `instance->_segment.speed` to scale the timebase!
    *   `uint32_t cycle_time = (ms * instance->_segment.speed) >> 8;`
- [ ] Remainder pixels at end of strip behave sanely.
