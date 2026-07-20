# Why build a custom ESPHome component when WLED already exists? 

That’s a great question! To answer it, let me take you back a few years.

### My Tasmota Roots
Years before I started this project, I had the privilege of serving as a **Core Developer for Tasmota**. 

That experience was truly my "masterclass" in IoT. Working deep in the Tasmota code taught me the importance of stability, efficiency, and how to squeeze every drop of performance out of a microcontroller. It shaped how I look at smart home reliability.

So, when I renovated my house, I put those lessons to work. I ran Ethernet cables to every single room—because as good as WiFi mesh is, nothing beats the stability of a physical wire.

### The DIY Phase & The Hard Way

Initially, I went the "hard mode" route. I developed my own custom firmware from scratch, running on WT32-ETH01 modules (hardwired via Ethernet) to control relays, switches, and I2C sensors.

It worked, and it was super minimal. But technology moves fast! I soon realized that maintaining a 100% custom firmware every time I wanted to add a new gadget was becoming a full-time job. I needed something easier to manage.

I decided to give ESPHome a second chance. Years ago, I found it a bit clunky, but I was curious to see how it had matured.

### The moment "Dad, can I have rainbow lights?"

Then came the real challenge: my youngest daughter asked for RGB lights in her room.

Now, we all know **WLED** is the king of RGB control. It’s simply fantastic. But I had a dilemma:

1.  **Redundancy:** I already had a powerful ESP32 node in her room for the sensors and relays. Adding a *second* chip just for cute lights felt like a waste of resources.
2.  **Connectivity:** I didn't have spare Ethernet ports, so I’d have to force the WLED chip onto WiFi, breaking my "wired-only" rule.

I looked at my existing Ethernet nodes and thought, *"I have the power right here. Why duplicate hardware?"*

### The "Spaghetti Code" Problem

I turned to ESPHome to handle the LEDs. While powerful, let's be honest: its native effect library is pretty basic compared to WLED.

I tried using `addressable_lambda` to recreate WLED's magic, but it got messy fast. Recreating just *one* of my daughter’s favorite effects ("Aurora") took **341 lines of lambda code!** 

??? abstract "See for yourself!"
    ```yaml
      - addressable_lambda:
          name: "Aurora"
          update_interval: 17ms
          lambda: |-
            // --- Configuration ---
            static const int MAX_WAVES = 40;
            
            // --- Struct Definition ---
            struct AuroraWave {
              float center;
              float speed;
              float width;
              int age;
              int ttl;
              Color color;
              bool alive;

              void init(int num_leds, Color c) {
                // WLED uses W_WIDTH_FACTOR 6, so max width is roughly num_leds / 6
                // We ensure a minimum width of num_leds / 20 to prevent thin lines
                int max_width = num_leds / 6;
                int min_width = num_leds / 20;
                if (min_width < 2) min_width = 2;
                if (max_width < min_width) max_width = min_width;
                
                width = (float)random(min_width, max_width + 1); 
                
                // Random start position (can be outside strip)
                center = (float)random(0, num_leds); 
                
                // Random speed and direction
                float base_speed = (float)random(5, 30) / 100.0f;
                
                // Apply speed factor from number component
                // Assumes 'effect_speed' number component exists
                int speed_setting = (int)id(effect_speed).state;
                
                switch (speed_setting) {
                    case 1: base_speed *= 0.25f; break; // Very Slow
                    case 2: base_speed *= 0.5f;  break; // Slow
                    case 3: base_speed *= 1.0f;  break; // Normal
                    case 4: base_speed *= 1.5f;  break; // Fast
                    case 5: base_speed *= 2.0f;  break; // Very Fast
                    case 6: base_speed *= 3.0f;  break; // Turbo
                    default: base_speed *= 1.0f; break;
                }
                
                speed = base_speed;
                if (random(0, 2) == 0) speed = -speed;
                
                age = 0;
                ttl = random(1500, 3500); // Increased TTL for longer trails
                color = c;
                alive = true;
              }

              void update(int num_leds) {
                center += speed;
                age++;
                if (age >= ttl) alive = false;
                
                // Recycle if off-screen
                if (center - width > num_leds || center + width < 0) {
                  alive = false;
                }
              }

              Color get_color(int led_index) {
                if (!alive) return Color::BLACK;
                
                float dist = abs((float)led_index - center);
                float offset_factor = dist / width;
                
                if (offset_factor > 1.0f) return Color::BLACK;
                
                float brightness = 1.0f - offset_factor;
                
                // Triangle Fade: Fade IN for first half, Fade OUT for second half
                float age_factor = 0.0f;
                int half_ttl = ttl / 2;
                if (age < half_ttl) {
                  age_factor = (float)age / (float)half_ttl;
                } else {
                  age_factor = (float)(ttl - age) / (float)half_ttl;
                }
                
                // Clamp
                if (age_factor > 1.0f) age_factor = 1.0f;
                if (age_factor < 0.0f) age_factor = 0.0f;
                
                brightness *= age_factor;
                
                return Color(
                  (uint8_t)(color.r * brightness),
                  (uint8_t)(color.g * brightness),
                  (uint8_t)(color.b * brightness),
                  (uint8_t)(color.w * brightness)
                );
              }
            };

            static std::vector<AuroraWave> waves;
            static bool initialized = false;

            // --- Palette Helper ---
            // Inline definition to avoid scope issues
            struct PaletteEntry {
              uint8_t pos;
              uint8_t r;
              uint8_t g;
              uint8_t b;
            };

            auto interpolate_color = [](const PaletteEntry& c1, const PaletteEntry& c2, uint8_t pos) -> Color {
              if (pos <= c1.pos) return Color(c1.r, c1.g, c1.b);
              if (pos >= c2.pos) return Color(c2.r, c2.g, c2.b);

              float t = (float)(pos - c1.pos) / (float)(c2.pos - c1.pos);
              
              return Color(
                (uint8_t)(c1.r + (c2.r - c1.r) * t),
                (uint8_t)(c1.g + (c2.g - c1.g) * t),
                (uint8_t)(c1.b + (c2.b - c1.b) * t)
              );
            };

            auto ColorFromPalette = [&](const std::vector<PaletteEntry>& palette, uint8_t pos) -> Color {
              if (palette.empty()) return Color::BLACK;

              // Handle wrap-around for smooth looping if needed, but for now strict interpolation
              // Find the two entries surrounding 'pos'
              for (size_t i = 0; i < palette.size() - 1; i++) {
                if (pos >= palette[i].pos && pos <= palette[i+1].pos) {
                  return interpolate_color(palette[i], palette[i+1], pos);
                }
              }
              
              // If pos is before first entry (shouldn't happen if starts at 0)
              if (pos < palette[0].pos) return Color(palette[0].r, palette[0].g, palette[0].b);
              
              // If pos is after last entry (shouldn't happen if ends at 255)
              return Color(palette.back().r, palette.back().g, palette.back().b);
            };

            // Dynamic Palette based on current light color
            auto get_aurora_color = [&](uint8_t pos) -> Color {
              
              // 1. Get current target color from the light state
              // Use current_color passed to the lambda
              float current_r = current_color.r / 255.0f;
              float current_g = current_color.g / 255.0f;
              float current_b = current_color.b / 255.0f;

              // 2. Convert to HSV
              float h, s, v;
              float min_val = std::min(std::min(current_r, current_g), current_b);
              float max_val = std::max(std::max(current_r, current_g), current_b);
              float delta = max_val - min_val;

              v = max_val;

              if (delta < 0.00001f) {
                s = 0;
                h = 0; // Undefined, really
              } else {
                s = delta / max_val;
                if (current_r >= max_val) {
                  h = (current_g - current_b) / delta;
                } else if (current_g >= max_val) {
                  h = 2.0f + (current_b - current_r) / delta;
                } else {
                  h = 4.0f + (current_r - current_g) / delta;
                }
                h *= 60.0f;
                if (h < 0.0f) h += 360.0f;
              }

              // 3. Generate Palette based on Saturation
              std::vector<PaletteEntry> current_palette;
              
              if (s < 0.15f) {
                // Low Saturation (White/Grey) -> Subtle Aurora Tints
                // Mix White with Pale Green and Pale Blue to avoid "plain white"
                
                // Helper to create a pale color from Hue
                auto make_pale = [&](float hue) -> PaletteEntry {
                    float r, g, b;
                    float s_pale = 0.3f; // 30% saturation
                    float v_pale = 1.0f; // Full brightness
                    
                    int i = (int)(hue / 60.0f) % 6;
                    float f = (hue / 60.0f) - i;
                    float p = v_pale * (1.0f - s_pale);
                    float q = v_pale * (1.0f - f * s_pale);
                    float t = v_pale * (1.0f - (1.0f - f) * s_pale);
                    
                    switch (i) {
                        case 0: r = v_pale; g = t; b = p; break;
                        case 1: r = q; g = v_pale; b = p; break;
                        case 2: r = p; g = v_pale; b = t; break;
                        case 3: r = p; g = q; b = v_pale; break;
                        case 4: r = t; g = p; b = v_pale; break;
                        case 5: r = v_pale; g = p; b = q; break;
                        default: r = v_pale; g = p; b = q; break;
                    }
                    return {(uint8_t)0, (uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255)};
                };

                PaletteEntry white = {0, (uint8_t)(current_r*255), (uint8_t)(current_g*255), (uint8_t)(current_b*255)};
                PaletteEntry pale_green = make_pale(120.0f); // Green
                PaletteEntry pale_blue = make_pale(240.0f);  // Blue
                
                // Palette: White -> Pale Green -> White -> Pale Blue -> White
                current_palette = {
                    {0,   white.r, white.g, white.b},
                    {64,  pale_green.r, pale_green.g, pale_green.b},
                    {128, white.r, white.g, white.b},
                    {192, pale_blue.r, pale_blue.g, pale_blue.b},
                    {255, white.r, white.g, white.b}
                };

              } else {
                // High Saturation -> Analogous Colors
                // Base +/- 25 degrees
                
                auto make_hsv = [&](float hue, float sat, float val) -> PaletteEntry {
                    while (hue >= 360.0f) hue -= 360.0f;
                    while (hue < 0.0f) hue += 360.0f;
                    
                    float r, g, b;
                    int i = (int)(hue / 60.0f) % 6;
                    float f = (hue / 60.0f) - i;
                    float p = val * (1.0f - sat);
                    float q = val * (1.0f - f * sat);
                    float t = val * (1.0f - (1.0f - f) * sat);
                    
                    switch (i) {
                        case 0: r = val; g = t; b = p; break;
                        case 1: r = q; g = val; b = p; break;
                        case 2: r = p; g = val; b = t; break;
                        case 3: r = p; g = q; b = val; break;
                        case 4: r = t; g = p; b = val; break;
                        case 5: r = val; g = p; b = q; break;
                        default: r = val; g = p; b = q; break;
                    }
                    return {(uint8_t)0, (uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255)};
                };

                PaletteEntry base = make_hsv(h, s, v);
                PaletteEntry neighbor_1 = make_hsv(h + 25.0f, s, v);
                PaletteEntry neighbor_2 = make_hsv(h - 25.0f, s, v);
                
                // Palette: Base -> Neighbor 1 -> Base -> Neighbor 2 -> Base
                current_palette = {
                    {0,   base.r, base.g, base.b},
                    {64,  neighbor_1.r, neighbor_1.g, neighbor_1.b},
                    {128, base.r, base.g, base.b},
                    {192, neighbor_2.r, neighbor_2.g, neighbor_2.b},
                    {255, base.r, base.g, base.b}
                };
              }

              return ColorFromPalette(current_palette, pos);
            };

            // --- Initialization ---
            if (!initialized) {
              waves.resize(MAX_WAVES);
              for (int i = 0; i < MAX_WAVES; i++) {
                waves[i].alive = false;
              }
              initialized = true;
            }

            // --- Main Loop ---
            
            // 1. Update Waves & Spawn
            for (auto &wave : waves) {
              if (wave.alive) {
                wave.update(it.size());
              } else {
                // Immediate respawn for dense effect
                wave.init(it.size(), get_aurora_color(random(0, 255)));
              }
            }

            // 2. Render to LEDs
            for (int i = 0; i < it.size(); i++) {
              int r = 0, g = 0, b = 0, w = 0;
              
              // Background color (Black for better contrast)
              r = 0; g = 0; b = 0; 

              for (auto &wave : waves) {
                if (wave.alive) {
                  Color c = wave.get_color(i);
                  r += c.r;
                  g += c.g;
                  b += c.b;
                  w += c.w;
                }
              }
              
              // Clamp values
              if (r > 255) r = 255;
              if (g > 255) g = 255;
              if (b > 255) b = 255;
              if (w > 255) w = 255;

              it[i] = Color(r, g, b, w);
            }
    ```

Imagine adding three or four effects: you’re looking at a config file with over 1,000 lines just for pretty lights. As a developer who values clean code, I couldn't live with that maintenance nightmare.

### Enter: ChimeraFX

I wanted the structure and integration of ESPHome, but with the amazing visual library of WLED.

So, I built **ChimeraFX**. But how about the component name? 

I love ancient history, and I love the story of the Chimera. It's a symbol of strength, power, and the ability to overcome challenges. It's also a symbol of unity, bringing together different parts to create something new and beautiful (well, not THAT beautiful, but you get the idea).

But what exactly is a Chimera? In Greek mythology, a Chimera is a hybrid creature made up of different animals: the body and head of a lion, a goat’s head rising from its back, and a serpent for a tail. Three parts, three animals, three sources of power.

It fills all the slots:

- **The Lion (Power):** Raw WLED logic and effects — the proven algorithms that make lights come alive.
- **The Goat (Structure):** The reliable ESPHome framework — robust, maintainable, and Home Assistant native.
- **The Serpent (Connection):** My custom abstraction layer — the binding force that seamlessly connects them.

It's hard to find another analogy that fits so well.

`ChimeraFX` wasn't easy to build. It was born from a lot of trial, error, and dad-duties but also from a lot of coffee (trust me on this one), and a lot of "why isn't this working?" moments.

### Falling Down the Rabbit Hole

Once the "Aurora" effect was running perfectly in my daughter’s room, I thought my job was done. Spoiler: it was just the beginning. 

As I lived with the system, I started falling down the rabbit hole. I began writing code to satisfy needs I didn't even know I had. I wanted buttery-smooth transitions. I wanted complex color palettes. Most importantly, I needed absolute memory efficiency so that rendering complex light shows wouldn’t cause my ESP32 to drop a single packet from the critical I2C sensors and relays running on the exact same chip. 

The project evolved organically, step by step. A late-night debugging session here, a weekend optimizing an algorithm there. It wasn't driven by a grand roadmap, but by a tinkerer's obsession with making things run *flawlessly*. I was stripping away the bloat and keeping only the absolute best parts of the code.

### A New Species in Open Source

Eventually, I stepped back and looked at what ChimeraFX had become. I realized that it had outgrown its original premise. 

When I look at the open-source panorama today, I realize there is nothing else quite like this out there. As I said, you usually have to make a choice: you either dedicate a whole microcontroller to an amazing, standalone LED operating system (like WLED), or you accept basic, unoptimized light effects baked into a broader automation firmware (like ESPHome). 

ChimeraFX became the missing link. It evolved into a high-performance, embedded visual engine that lives completely side-by-side with complex home automation tasks, without breaking a sweat or hogging resources.

### Beyond Blinking: The Event-Driven Revolution

If you thought I was done falling down the rabbit hole, grab a shovel. 

As I kept tinkering, I realized a fundamental flaw in most smart lighting: effects are almost always strictly *time-based*. They just run dumb loops. But what if the lights actually knew what they were doing? 

That's why I introduced a **complex, event-driven sequencer**. Instead of just firing off an animation and hoping for the best, the system tracks what is happening in real-time. It knows exactly when a sequence starts. It knows the microsecond a light pulse hits exactly 20% or 50% down the LED strip. It knows when the main effect finishes rendering, when it’s time to trigger an "outro," and when the complete sequence is fully done.

This elevated the level of control to something I honestly haven’t seen before on consumer-level devices. 

The beauty of this sequencer is its dual nature:

1. **On-Device Perfection:** It can run entirely locally on the ESP32 chip. Because it doesn't need to wait for a network command to change states, there is **zero lag**. The best for **Mission Critical** applications.

2. **Home Assistant Magic:** Every single step of an effect—every milestone the light hits—becomes an event that Home Assistant can listen to. You can trigger external home automations based on exactly where an animation is on your LED strip. Imagine use a sensor as a trigger to start an effect, or use the position of a light to trigger an action. Endless possibilities.

When I combined this event-driven sequencer with a new suite of highly specialized, monochromatic effects, it resulted in a system that is incredibly complex under the hood, yet shockingly easy to manage from the outside. 

### The Result

While ChimeraFX began as a simple "dad project" to bring some WLED classics to ESPHome, it has evolved into something entirely unique. The focus has shifted from simple replication to pure **innovation**.

Today, ChimeraFX is a precision-engineered lighting engine offering a curated suite of effects designed specifically for ESPHome, prioritizing **visual fidelity and resource efficiency** over raw quantity, with a focus on **architectural lighting control**.

I hope you enjoy using it as much as I enjoyed building it. If you find **ChimeraFX** useful and would like to support the time and effort put into it, [donations](https://www.buymeacoffee.com/effelle) are never expected but always greatly appreciated!
