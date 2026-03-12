# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ChimeraFX** is an advanced LED effects engine for ESPHome, implemented as a native C++ component for ESP32. It provides high-performance WLED-style effects optimized for both ESP-IDF and Arduino frameworks, with a focus on architectural transitions, fluid simulations, and event-driven sequencing.

- **Repository**: https://github.com/effelle/ChimeraFX
- **License**: EUPL-1.2
- **Framework**: ESPHome component (C++ core + Python integration)
- **Platform**: ESP32 (Classic, S2, S3, C3, C6); ESP8266 NOT supported
- **Protocol**: 1-wire NRZ (WS2812X, SK6812, WS2811) only

## High-Level Architecture

```
components/
├── cfx_effect/          # Core rendering engine
│   ├── CFXRunner.cpp/h  # Main effect runner, effect dispatcher (256 effect modes)
│   ├── cfx_addressable_light_effect.cpp/h  # ESPHome effect integration
│   ├── cfx_control.h/py # Effect controls, triggers, automations
│   ├── cfx_utils.h      # Utility functions (color, math, palettes)
│   └── FastLED_Stub.cpp/h  # Compatibility layer (FastLED-like API)
├── cfx_light/           # LED output driver
│   ├── cfx_light.cpp/h  # DMA RMT driver, segment infrastructure
│   └── cfx_virtual_segment_light.h  # Segment light entities
└── cfx_sequence/        # Event-driven sequencing system
    ├── cfx_sequence.cpp/h  # Sequence interpreter, event triggers

tools/
└── visualizer/          # Node.js/Express effect preview tool

docs/                    # MkDocs documentation
chimera_fx_effects.yaml  # Effect definitions package (for users)
```

### Key Design Patterns

- **Dual-layer integration**: C++ core compiled into ESPHome binary; Python `__init__.py` registers components and generates config schema
- **Effect dispatch**: Single `CFXRunner::service()` method with large switch-case on `_mode` (0-255). Each effect mode is a function.
- **Segment model**: Up to 6 configurable segments per strip; segments can have independent effects or share runners
- **DMA RMT output**: Non-blocking, fire-and-forget LED transmission using ESP32 RMT peripheral
- **Intro/Outro transitions**: Effects can have entrance/exit animations; callbacks allow post-shutdown sequences
- **Event-driven sequencing**: Triggers (on_start, on_complete, on_reach, on_pixel_num) enable complex timed automation

### Component Responsibilities

- **CFXRunner**: Pure effect rendering; writes to `Segment::data` buffer (RGB24). Knows nothing about DMA/ESPHome.
- **CFXLightOutput**: ESPHome `AddressableLight` subclass; owns LED buffer, SPI/RMT driver, segment state sync; bridges between Home Assistant and CFXRunner instances.
- **CFXSequence**: Parses YAML sequences and drives CFXRunners with timed transitions and triggers.
- **VirtualSegmentLight**: Light entities for individual segments in multi-segment setups.

## Common Development Tasks

### Build & Compile

The codebase is an ESPHome component library and does **not** have a standalone build system. To compile and test:

1. **Add to ESPHome YAML**:
   ```yaml
   external_components:
     - source:
         type: local
         path: ./components
       components: [cfx_light, cfx_effect, cfx_sequence]
   ```

2. **Build with ESPHome** (in your ESPHome project):
   ```bash
   esphome config.yaml compile
   esphome config.yaml upload
   ```

3. **Compilation requirements**:
   - ESPHome 2024.x or newer
   - Platform: `espressif32`
   - Framework: `esp-idf` (recommended) or `arduino`
   - Dependencies: `light`, `number`, `select`, `switch`

### Run Tests

No unit test framework exists. Testing workflow:

1. **Compile-time validation**: Ensure ESPHome compilation succeeds with various config permutations
2. **Hardware testing**: Upload to ESP32 and verify:
   - LED output (use visualizer to preview effects: `python tools/visualizer/visualizer.py`)
   - HA entity states and controls
   - Segment runner synchronization
   - Intro/outro transitions
   - Sequencer triggers
3. **Visualizer tool**: Preview effects without hardware
   ```bash
   cd tools/visualizer
   npm install  # if needed
   python visualizer.py
   ```
   Then open http://localhost:3000 and connect from ChimeraFX light via `visualizer_ip`/`visualizer_port` config.

### Lint & Format

No formal linter configured. Follow these conventions:

- C++: Use the same style as existing code (4-space indents, braces on same line, `#include` order: stdlib → ESPHome → project)
- Python: PEP 8, 4-space indents (follow ESPHome conventions)
- YAML: 2-space indents
- Commit messages: Use Conventional Commits: `feat(component): ...`, `fix: ...`, `docs: ...`

### Documentation

- **Source**: `docs/` (Markdown)
- **Build**: `mkdocs serve` (requires `mkdocs` and `mkdocs-material`)
- **Deploy**: GitHub Pages from `site/` (generated)
- **Effect list**: `chimera_fx_effects.yaml` and `docs/Effects-Library.md` (auto-generated from CFXRunner effect names)

### Debugging

- **ESPHome logs**: Enable `logger` level `debug` to see `chimera_fx` logs
- **CFXRunner diagnostics**: Set `diagnostics.enabled: true` in effect config to log frame time per effect
- **Visualizer**: Send metadata with `light.send_visualizer_metadata(name, palette)` to show active effect/palette in tool
- **Common issues**: Check RMT channel allocation (only channels 0-7); ensure `num_leds` reasonable for RAM (RGB24 buffer = num_leds * 3 bytes)

## Recent Focus & Current Work

Based on recent git history (Phase 2):

- **Event-driven HA integration**: Triggers and automations responding to effect progress (on_start, on_complete, on_reach percentage/pixel)
- **Monochromatic transitions**: Architectural effects for single-color setups (Moiré Shift, Resonance Fill, Telemetry, Stellar Dust, Interference)
- **Intro/Outro refinement**: Segment inheritance of root defaults, flexible transition selection
- **CFXRunner refactor**: RAII guard for global instance, removed mode pointer out-of-bounds risks

## Effect Development

To add a new effect:

1. **Assign an unused mode ID** (check `chimera_fx_effects.yaml` and `CFXRunner.h` for gaps). Range: 0-255.
2. **Implement rendering** in `CFXRunner::service()` (switch case) or a helper function. Write to `_segment.data[]` as RGB24.
3. **Use `Segment` helpers**: `setPixelColor()`, `fill()`, `fadeToBlackBy()`, `blur()`, `color_from_palette()`.
4. **Track progress**: Set `current_leading_pixel` for percentage-based triggers (wipe/sweep/sunrise/etc).
5. **Add to effect name map** in `components/cfx_effect/__init__.py:CFX_EFFECT_NAMES`.
6. **Add config entry** in `chimera_fx_effects.yaml` and document in `docs/Effects-Library.md`.
7. **Test** with visualizer and hardware.

See existing effects (e.g., `FX_MODE_MOIRE_SHIFT`, `FX_MODE_RESONANCE_FILL`) for transition patterns; see `FX_MODE_WIPE` for progress tracking.

## Branch & Workflow

- **Main**: `main`
- **Active development**: `stage` (current branch)
- **Commit style**: Descriptive; reference effect names or component areas

## Tools & Scripts

- `tools/visualizer/visualizer.py`: Flask/Express bridge for effect visualization (Node.js frontend). Accepts raw pixel stream from ESP32 via WebSocket.
- `all_blobs.txt`: GitHub blob SHA listing (possibly for analytics/debug).

## Configuration Reference (YAML)

```yaml
light:
  - platform: esp32_rmt_led_strip  # or custom cfx_light
    cfx_light:
      pin: GPIO3
      num_leds: 120
      chipset: WS2812X
      rgb_order: GRB
      # Intro/outro transitions (root defaults)
      default_intro: Wipe
      default_intro_duration: 1.5
      default_outro: Wipe
      default_outro_duration: 1.5
      # Segments
      segments:
        - id: segment1
          start: 0
          stop: 60
          mirror: false
      # Visualizer
      visualizer_ip: 192.168.1.100
      visualizer_port: 7777

    effects:
      - addressable_cfx:
          effect_id: 3  # Wipe
          speed: 128
          intensity: 128
          mirror: !find light.xxx_mirror
          # Sequencer/presets
          set_speed: 200
          set_intro: 1  # Wipe
          set_intro_duration: 2.0
          # Triggers
          on_start:
            - then:
                - logger.log: "Effect started"
          on_reach:
            position: 50%
            then:
              - light.turn_on: ...  # halfway trigger
```

## Important Files to Know

- `components/cfx_effect/CFXRunner.h`: Effect mode constants (0-255), `Segment` struct, CFXRunner class interface
- `components/cfx_effect/cfx_addressable_light_effect.h/cpp`: ESPHome effect wrapper, Python codegen
- `components/cfx_light/cfx_light.h`: DMA RMT driver, segment definitions, visualizer socket
- `components/cfx_sequence/cfx_sequence.h/cpp`: Sequence interpreter
- `components/cfx_effect/__init__.py`: Python schema, effect ID → name mapping, automation triggers
- `docs/Effects-Library.md`: Auto-generated effect documentation with parameters

## Platform-Specific Notes

- **ESP-IDF ≥ v5.3**: Different RMT buffer type (`uint8_t*` vs `rmt_symbol_word_t*`) — handled in cfx_light.h with `#if ESP_IDF_VERSION`
- **Arduino**: Uses same RMT API but through Arduino core abstractions
- **RAM constraints**: Each runner + segment data ≈ `num_leds * 3` bytes. Enable `mirror` halves virtual length but doesn't reduce memory. Max 6 segments × (max seg length) may exceed RAM.
- **CPU**: Dual-core ESP32 strongly recommended; complex effects + heavy ESPHome components → instability.

## Troubleshooting

- **Compilation errors**: Ensure `external_components` points to local path and all 3 components are included. Check ESPHome version compatibility.
- **RMT channel conflicts**: Only 8 RMT channels on ESP32; other peripherals (IR, NeoPixelBus) may also use them.
- **Visualizer not receiving**: Verify `visualizer_ip`/`port` reachable from ESP32; check firewall. UDP not used; TCP socket stream.
- **Effect not in list**: Add to `CFX_EFFECT_NAMES` in `__init__.py` and rebuild.

## Style & Contribution Guidelines

- Keep effect code localized to `CFXRunner::service()` or helper functions in the same file; avoid polluting other components.
- Use existing `Segment` methods for common operations; don't bypass to manipulate data buffer directly.
- Document new effects in `docs/Effects-Library.md` with a clear description, recommended palette, and example video (if available).
- Do not break existing effect IDs or YAML keys; maintain backward compatibility.
- Prefer `uint8_t` for speed, but `float` for time/virtual_now per CFX-009. Keep math efficient.

## Claude Code Context Management

- Always try to calculate the necessary tokens before proceeding with any task to avoid context_length_exceeded error.
- The maximum context length is 262144 tokens.
- When working with large files or multiple operations, break tasks into smaller chunks and use memory files or worktrees to manage context.
- Use the Agent tool with subagents for parallel exploration to conserve main context window space.
- Use the auto memory directory (`C:\Users\effel\.claude\projects\C--Users-effel-OneDrive-Desktop-Antigravity-projects\memory\`) for persisting information across conversations.
