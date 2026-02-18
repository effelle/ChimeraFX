## 🧠 Brainstorm: WLED Blur Implementation

### Context
You asked about the `blur` helper used in WLED (specifically `blur1d` and `SEGMENT.blur`).
- **Is it used often?** Yes, extensively (26+ distinct effects).
- **What is it for?** Smooth trails, motion blur, and temporal smoothing.
- **Does it offer gains?** Significant visual improvement over `fadeToBlackBy`.

---

### Analysis: Blur vs. Fade

#### Old Approach: `fadeToBlackBy`
- **Logic:** `pixel = pixel - fade_amount`.
- **Behavior:** Subtracts brightness every frame.
- **Issues:**
    - **Brightness Loss:** Pixels darken even if they should just move.
    - **Stepping:** Low brightness pixels drop to zero suddenly (The "Floor Bug" we fought).
    - **Dryness:** Moving pixels leave no trace unless explicitly coded to leave a trail.

#### New Approach: `blur1d` (WLED Method)
- **Logic:** `pixel = (pixel * (255-blur) + (left + right) * (blur/2)) >> 8`.
- **Behavior:** Spreads brightness to neighbors every frame.
- **Benefits:**
    - **Energy Preservation:** Light doesn't just vanish; it spreads. A moving pixel leaves a smooth "comet tail" naturally.
    - **Smoothness:** Averages out temporal jitter (anti-aliasing for time).
    - **No "Floor Bug":** Because it mixes neighbors, it rarely hits a hard zero wall instantly.

---

### Application to ChimeraFX

#### Option A: Implement `Segment::blur1d` Helper
Add the exact WLED math to your `Segment` class in `CFXRunner`.

✅ **Pros:**
- **Universal:** Any effect can call `SEGMENT.blur(40)` to get instant smooth trails.
- **Maintenance:** One function to fix/tune.
- **WLED Compat:** Makes porting future effects easier (copy-paste).

❌ **Cons:**
- Requires editing core `CFXRunner.h/cpp`.

#### Option B: Global "Blur" Slider (Auto-Apply)
Add a global "Motion Blur" slider that applies `blur1d` to the *entire strip* at the end of every frame, separate from effect logic.

✅ **Pros:**
- **Instant Upgrade:** Makes *every* effect smoother without rewriting them.
- **User Control:** User can tune "trail length" globally.

❌ **Cons:**
- Might blur effects that shouldn't be blurred (e.g., crisp strobe).
- Performance cost (running it every frame for every pixel).

---

## 💡 Recommendation

**Option A (Helper) + Selective Application.**

1.  **Refactor:** precise `blur1d` logic into `Segment::blur` in `CFXRunner.cpp`.
2.  **Upgrade Effects:** Systematically replace `fadeToBlackBy` in moving effects (`Juggle`, `Sinelon`, `BPM`, `Phased`) with `SEGMENT.blur()`.
3.  **Future:** Consider a "Global Blur" checkbox for the user.

**Immediate Action:**
I can verify which other effects use `fadeToBlackBy` and propose a "Smoothness Pass" to upgrade them all to `blur`.
