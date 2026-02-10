# Available Effects & Palettes

This library lists the high-fidelity effects currently optimized for ChimeraFX.

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

## 🔥 Effect Library

### Effect List

| ID | Name | Description | Controls | Palette? | Author |
|:---|:---|:---|:---|:---:|:---|
| 0 | **Static** | Solid color with palette support. | **Palette**: Gradient/Solid | Yes | - |
| 2 | **Breathe** | Apple-style standby breathing LED. | **Speed**: Breathe rate | Yes | - |
| 3 | **Wipe** | Primary/Secondary color wipe. | **Speed**: Wipe speed | Yes | - |
| 6 | **Sweep** | Ping-pong wipe animation. | **Speed**: Sweep speed | Yes | - |
| 8 | **Colorloop** | Solid color cycling through palette. | **Intensity**: Saturation<br>**Speed**: Cycle speed | No | Harm Aldick |
| 9 | **Rainbow** | Per-pixel rainbow with density control. | **Intensity**: Zoom/Density<br>**Speed**: Flow speed | No | Harm Aldick |
| 18 | **Dissolve** | Random pixel color transitions. | **Speed**: Dissolve rate | No | - |
| 38 | **Aurora** | Northern lights animation. | **Intensity**: Wave width<br>**Speed**: Drift speed | Yes | Aircoookie |
| 53 | **Fire Dual** | Two flames meeting in the center. | **Intensity**: Sparking rate<br>**Speed**: Cooling rate | Yes | Mark Kriegsman (Adapt.) |
| 63 | **Colorwaves** | Rainbow flag with breathing motion. | **Intensity**: Saturation<br>**Speed**: Wave speed | No | WLED (Pride 2015) |
| 64 | **Juggle** | Eight bouncing dots with trails. | **Intensity**: Trail<br>**Speed**: Speed | No | Mark Kriegsman (FastLED) |
| 66 | **Fire** | Realistic fire simulation. | **Intensity**: Sparking rate<br>**Speed**: Cooling rate | Yes | Mark Kriegsman |
| 74 | **Colortwinkles** | Magical fairy-dust twinkles. | **Intensity**: Spawn rate<br>**Speed**: Fade speed | Yes | Mark Kriegsman |
| 76 | **Meteor** | Meteor with random decay trail. | **Intensity**: Trail length<br>**Speed**: Fall speed | Yes | - |
| 91 | **Bouncing Balls**| Real gravity physics. | **Intensity**: Count<br>**Speed**: Gravity | Yes | - |
| 97 | **Plasma** | Smooth plasma animation. | **Intensity**: Frequency<br>**Speed**: Morph speed | Yes | Jeremy Williams |
| 101 | **Ocean** | Gentle ocean waves (Pacifica). | **Intensity**: Zoom/Scale<br>**Speed**: Ripple speed | No | Mark Kriegsman |
| 104 | **Sunrise** | Gradual sunrise/sunset simulation. | **Speed**: Duration | No | - |
| 105 | **Phased** | Sine wave interference pattern. | **Intensity**: Count<br>**Speed**: Speed | Yes | - |
| 110 | **Flow** | Smooth color zones animation. | **Intensity**: Zone count<br>**Speed**: Flow speed | Yes | - |

*(Note: More effects from the standard WLED library are being ported incrementally.)*

---

## 🚀 Intro Animations

These short animations play once when the light is turned ON.

| ID | Animation Name | Description |
|:---|:---|:---|
| 0 | **None** | No intro animation. |
| 1 | **Wipe** | Linear wipe from start to end (respects Mirror). |
| 2 | **Fade** | Smooth brightness fade-in. |
| 3 | **Center** | Wipe from center outwards (or inwards if reversed). |
| 4 | **Glitter** | Random pixels sparkle as brightness increases. |
