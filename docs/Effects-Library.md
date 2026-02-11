# Available Effects, Palettes and Intros

## 🔥 Effect Library

If you’re wondering why all WLED effects aren't available yet, here is the answer:

Bringing WLED effects to ChimeraFX is a meticulous process. Each effect is partially rewritten to "squeeze" every bit of performance out of the hardware with minimal resource overhead. My goal is to preserve the original look while ensuring the code runs perfectly within ESPHome alongside your other components. Thank you for your patience as I port them over incrementally!

### Effect List

| ID | Name | Description | Controls | Palette support | Author |
|:---|:---|:---|:---|:---:|:---|
| 0 | **Static** | Solid color with palette support. | **Palette**: Gradient/Solid | Yes | Aircoookie |
| 2 | **Breathe** | Apple-style standby breathing LED. | **Speed**: Breathe rate | Yes | Aircoookie |
| 3 | **Wipe** | Primary/Secondary color wipe. | **Speed**: Wipe speed | Yes | Aircoookie |
| 6 | **Sweep** | Ping-pong wipe animation. | **Speed**: Sweep speed | Yes | Aircoookie |
| 8 | **Colorloop** | Solid color cycling through palette. | **Intensity**: Saturation - **Speed**: Cycle speed | Yes | Harm Aldick |
| 9 | **Rainbow** | Per-pixel rainbow with density control. | **Intensity**: Zoom/Density - **Speed**: Flow speed | Yes | Harm Aldick |
| 18 | **Dissolve** | Random pixel color transitions. | **Speed**: Dissolve rate | Yes | Aircoookie |
| 38 | **Aurora** | Northern lights animation. | **Intensity**: Wave width - **Speed**: Drift speed | Yes | Aircoookie |
| 53 | **Fire Dual** | Two flames meeting in the center. | **Intensity**: Sparking rate - **Speed**: Cooling rate | No | Mark Kriegsman (Adapt.) |
| 63 | **Colorwaves** | Rainbow flag with breathing motion. | **Intensity**: Saturation - **Speed**: Wave speed | Yes | WLED (Pride 2015) |
| 64 | **Juggle** | Eight bouncing dots with trails. | **Intensity**: Trail - **Speed**: Speed | Yes | Mark Kriegsman (FastLED) |
| 66 | **Fire** | Realistic fire simulation. | **Intensity**: Sparking rate - **Speed**: Cooling rate | No | Mark Kriegsman |
| 74 | **Colortwinkles** | Magical fairy-dust twinkles. | **Intensity**: Spawn rate - **Speed**: Fade speed | Yes | Mark Kriegsman |
| 76 | **Meteor** | Meteor with random decay trail. | **Intensity**: Trail length - **Speed**: Fall speed | Yes | Aircoookie |
| 91 | **Bouncing Balls**| Real gravity physics. | **Intensity**: Count - **Speed**: Gravity | Yes | Tweaking4All |
| 97 | **Plasma** | Smooth plasma animation. | **Intensity**: Frequency - **Speed**: Morph speed | Yes | Jeremy Williams |
| 101 | **Ocean** | Gentle ocean waves (based on Pacifica). | **Intensity**: Zoom/Scale - **Speed**: Ripple speed | No | Mark Kriegsman |
| 104 | **Sunrise** | Gradual sunrise/sunset simulation. | **Speed**: Duration | Yes | Aircoookie |
| 105 | **Phased** | Sine wave interference pattern. | **Intensity**: Count - **Speed**: Speed | Yes | Aircoookie |
| 110 | **Flow** | Smooth color zones animation. | **Intensity**: Zone count - **Speed**: Flow speed | Yes | Aircoookie |

### A few notes on the credits:
*   **Aircoookie:** He is the lead developer of WLED. Any effect that doesn't have a clear historical "parent" in the FastLED community is usually his work.
*   **Tweaking4All:** The "Bouncing Balls" algorithm with gravity is a very famous bit of code from a specific tutorial on the Tweaking4All website that has been used in almost every LED project for the last decade.
*   **Mark Kriegsman:** Most of the high-quality, math-heavy effects (Fire, Juggle, Pacifica) come from his work on the **FastLED** library. He is widely considered the "godfather" of modern LED animations.

---

## 🎨 Palettes

You can assign any of these palettes to compatible effects using the `palette` selector.

| ID | Palette Name | Description |
|:---|:---|:---|
| 0 | **Default** | Uses the effect's hardcoded colors. |
| 1 | **Aurora** | Northern lights colors. |
| 2 | **Forest** | Earth greens, browns, and mossy tones. |
| 3 | **Halloween** | Orange and purple mix. |
| 4 | **Rainbow** | Full HSV spectrum cycle. |
| 5 | **Fire** | Intense reds, oranges, and yellows. |
| 6 | **Sunset** | Purple, red, orange, and yellow gradients. |
| 7 | **Ice** | Cool whites, blues, and cyans. |
| 8 | **Party** | Vibrant high-contrast mixed colors. |
| 10 | **Pastel** | Soft, desaturated soothing colors. |
| 11 | **Ocean** | Deep blues, cyans, and white crests. |
| 12 | **HeatColors** | Traditional heatmap colors. |
| 13 | **Sakura** | Pink and white cherry blossom tones. |
| 14 | **Rivendell** | Fantasy forest greens and blues. |
| 15 | **Cyberpunk** | Neon pink, blue, and purple. |
| 16 | **OrangeTeal** | Modern cinematic contrast. |
| 17 | **Christmas** | Red and green holiday mix. |
| 18 | **RedBlue** | Simple red and blue gradient. |
| 19 | **Matrix** | Digital rain greens. |
| 20 | **SunnyGold** | Bright gold and yellow. |
| 21 | **Solid** | Solid color (uses primary color). |
| 22 | **Fairy** | Magical pinks and purples. |
| 23 | **Twilight** | Dusk blues and purples. |

---

## 🚀 Intro Animations

These short animations play once when the light is turned ON.

| ID | Animation Name | Description |
|:---|:---|:---|
| 0 | **None** | Standard behavior (Main effect starts immediately). |
| 1 | **Wipe** | Linear wipe from start to end (respects Mirror). |
| 2 | **Fade** | Smooth brightness fade-in. |
| 3 | **Center** | Wipe from center outwards (or inwards if reversed). |
| 4 | **Glitter** | Random pixels sparkle as brightness increases. |

### Transition Behavior
When the Intro Duration ends, the Intro Effect will **Dissolve** (Soft Fairy Dust) into the Main Effect over 1.5 seconds. This creates a seamless, premium startup experience.
**Note:** The intro animation runs only when the light is switched from Off to On. If you change the main effect while the light is already on, the intro will not play again. It will only re-trigger the next time the light is toggled on.
