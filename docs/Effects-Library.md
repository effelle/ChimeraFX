# Available Effects, Palettes and Intros

If you’re wondering why all WLED effects aren't available yet, here is the answer:

Bringing WLED effects to ChimeraFX is a meticulous process. Each effect is partially rewritten to 'squeeze' every bit of performance out of the hardware with minimal resource overhead. My goal is to preserve the original look while ensuring the code runs perfectly within ESPHome alongside your other components.
Please note that some WLED effects will not be ported. This includes animations designed specifically for 2D matrix, as well as effects that rely on WLED's ability to select three distinct colors (Secondary/Tertiary), as these features exceed the current capabilities of ESPHome and ChimeraFX.

### Effect List

#### ChimeraFX Original Effects:

| ID | Name | Description | Controls | Palette support | Author |
|:---|:---|:---|:---|:---:|:---|
| 151 | **Dropping Time**| Falling drops filling a bucket. | **Speed**: Time (1-60 minutes) | Yes | ChimeraFX |
| 153 | **Fire Dual** | Two flames meeting in the center. | **Intensity**: Sparking rate - **Speed**: Cooling rate | No | ChimeraFX - Mark Kriegsman (Adapt.) |
| 156 | **Follow Me** | Single cursor running from one side to another. | **Intensity**: Fade rate - **Speed**: Cursor speed | No | ChimeraFX |
| 157 | **Follow Us** | Three cursors running from one side to another. | **Intensity**: Cursors distance - **Speed**: Cursors speed | No | ChimeraFX |
| 155 | **Kaleidos** | Symmetrical mirroring of animations. | **Intensity**: Segment count - **Speed**: Scroll speed | Yes | ChimeraFX |
| 152 | **Percent Center**| Percent-based fill from the center. | **Intensity**: Percent fill - **Speed**: Smoothness | Yes | ChimeraFX |
| 154 | **Heartbeat Center**| Heartbeat pulsing from the center. | **Intensity**: Pulse decay - **Speed**: BPM | Yes | ChimeraFX |

#### WLED-Style Effects:

| ID | Name | Description | Controls | Palette support | Author |
|:---|:---|:---|:---|:---:|:---|
| 38 | **Aurora** | Northern lights animation with drifting waves. | **Intensity**: Wave width - **Speed**: Drift speed | Yes | Aircoookie |
| 1 | **Blink** | Simple on/off blinking. | **Speed**: Blink rate | Yes | Aircoookie |
| 26 | **Blink Rainbow** | Blinking with color cycling. | **Speed**: Blink rate - **Intensity**: Color speed | Yes | Aircoookie |
| 91 | **Bouncing Balls** | Real gravity physics with multiple balls. | **Intensity**: Count - **Speed**: Gravity | Yes | Tweaking4All |
| 2 | **Breathe** | Apple-style standby breathing LED. | **Speed**: Breathe rate | Yes | Aircoookie |
| 28 | **Chase** | Moving dot with fading trail. | **Intensity**: Trail length - **Speed**: Chase speed | Yes | Aircoookie |
| 54 | **Chase tri** | Three-color chase animation. | **Intensity**: Spacing - **Speed**: Chase speed | Yes | Aircoookie |
| 8 | **Colorloop** | Solid color cycling through palette. | **Intensity**: Saturation - **Speed**: Cycle speed | Yes | Harm Aldick |
| 74 | **Colortwinkle** | Magical fairy-dust twinkles. | **Intensity**: Spawn rate - **Speed**: Fade speed | Yes | Mark Kriegsman |
| 63 | **Colorwaves** | Rainbow flag with breathing motion. | **Intensity**: Saturation - **Speed**: Wave speed | Yes | WLED (Pride 2015) |
| 18 | **Dissolve** | Sequential random pixel fill and clear. | **Intensity**: Change rate - **Speed**: Hold time | Yes | Aircoookie |
| 96 | **Drip** | Falling water drop physics. | **Intensity**: Drop size - **Speed**: Gravity | Yes | Aircoookie |
| 66 | **Fire** | Realistic 1D fire simulation. | **Intensity**: Sparking rate - **Speed**: Cooling rate | No | Mark Kriegsman |
| 90 | **Fireworks** | Exploding rocket simulation. | **Intensity**: Explosion size - **Speed**: Launch frequency | Yes | Aircoookie |
| 110 | **Flow** | Smooth moving color zones. | **Intensity**: Zone count - **Speed**: Flow speed | Yes | Aircoookie |
| 87 | **Glitter** | Moving rainbow with random sparkles. | **Intensity**: Sparkle density - **Speed**: Flow speed | Yes | Mark Kriegsman |
| 100 | **HeartBeat** | Anatomical heartbeat simulation. | **Intensity**: Pulse decay - **Speed**: BPM | Yes | Aircoookie |
| 64 | **Juggle** | Eight bouncing dots with trails. | **Intensity**: Trail length - **Speed**: Movement speed | Yes | Mark Kriegsman |
| 76 | **Meteor** | Meteor with random decay trail. | **Intensity**: Trail length - **Speed**: Fall speed | Yes | Aircoookie |
| 25 | **Multi Strobe** | Multiple strobe pulses in sequence. | **Intensity**: Burst count - **Speed**: Strobe rate | Yes | Aircoookie |
| 107 | **Noise Pal** | Perlin noise color movement. | **Intensity**: Zoom - **Speed**: Drift speed | Yes | WLED |
| 101 | **Ocean** | Gentle ocean waves (Pacifica). | **Intensity**: Zoom/Scale - **Speed**: Wave speed | No | Mark Kriegsman |
| 98 | **Percent** | Percent-based fill (Progress Bar). | **Intensity**: Percent fill - **Speed**: Smoothness | Yes | Aircoookie |
| 97 | **Plasma** | Multi-layer Perlin noise plasma. | **Intensity**: Frequency - **Speed**: Morph speed | Yes | Jeremy Williams |
| 95 | **Popcorn** | Bouncing particles simulation. | **Intensity**: Particle count - **Speed**: Bounciness | Yes | Aircoookie |
| 9 | **Rainbow** | Moving per-pixel rainbow. | **Intensity**: Zoom/Density - **Speed**: Flow speed | Yes | Harm Aldick |
| 79 | **Ripple** | Expanding ripple waves. | **Intensity**: Wave strength - **Speed**: Propagation | Yes | Aircoookie |
| 52 | **Running Dual** | Two running light trails from center. | **Intensity**: Spacing - **Speed**: Run speed | Yes | Aircoookie |
| 15 | **Running lights**| Moving light trails with subtle decay. | **Intensity**: Trail spacing - **Speed**: Run speed | Yes | Aircoookie |
| 16 | **Saw** | Sawtooth wave color movement. | **Intensity**: Wave count - **Speed**: Speed | Yes | Aircoookie |
| 40 | **Scanner** | Larson Scanner (Cylon eye). | **Intensity**: Trail length - **Speed**: Scan speed | Yes | Aircoookie |
| 60 | **Scanner Dual** | Two scanners meeting in the center. | **Intensity**: Trail length - **Speed**: Scan speed | Yes | Aircoookie |
| 20 | **Sparkle** | Randomly flashing pixels. | **Intensity**: Sparkle density - **Speed**: Cycle speed | Yes | Aircoookie |
| 22 | **Sparkle +** | Intense high-frequency sparkles. | **Intensity**: Density - **Speed**: Flash rate | Yes | Aircoookie |
| 21 | **Sparkle Dark** | Random pixels turning off briefly. | **Intensity**: Density - **Speed**: Blink rate | Yes | Aircoookie |
| 0 | **Static** | Solid color or stationary palette. | **Palette**: Gradient/Solid | Yes | Aircoookie |
| 23 | **Strobe** | Rapid full-strip flashing. | **Speed**: Strobe rate | Yes | Aircoookie |
| 24 | **Strobe Rainbow**| Strobe flashing with color cycle. | **Speed**: Strobe rate - **Intensity**: Color speed | Yes | Aircoookie |
| 104 | **Sunrise** | Gradual brightness and color increase. | **Speed**: Duration | Yes | Aircoookie |
| 6 | **Sweep** | Ping-pong wipe animation. | **Speed**: Sweep speed | Yes | Aircoookie |
| 3 | **Wipe** | Linear color wipe from start to end. | **Speed**: Wipe speed | Yes | Aircoookie |
| 4 | **Wipe Random** | Linear wipe with random color changes. | **Speed**: Wipe speed | Yes | Aircoookie |

### A few notes on the credits:
*   **ChimeraFX:** Custom effects developed specifically for this component by Federico Leoni.
*   **Aircoookie:** Lead developer of WLED. Most core effect logic is derived from his work.
*   **Mark Kriegsman:** Godfather of high-quality LED math. Responsible for Fire, Juggle, and many FastLED classics.
*   **Tweaking4All:** Creator of the iconic Bouncing Balls physics logic.

---

## Palettes

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
| 9 | **Lava** | Black, red, and orange deep heatmap. |
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

## Intro Animations

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