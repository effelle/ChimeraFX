# CFX Visualizer

A Pygame-based UDP Visualizer to test and view ChimeraFX rendering locally without physical LEDs.

## Installation

```bash
pip install -r requirements.txt
```

## Running the Visualizer

You can start the visualizer by running the included batch script or directly via Python:

```bash
# For 3-byte RGB strips like WS2812 (Default)
python visualizer.py

# For 4-byte RGBW strips like SK6812
python visualizer.py --rgbw
```

Options:
- `--ip`: IP address to bind to (Default: `0.0.0.0` - Listens on all interfaces)
- `--port`: UDP port to listen on (Default: `7777`)
- `--scale`: Visual size of each LED pixel box (Default: `15`)

## ESPHome Configuration

To connect your ESPHome device to this visualizer, ensure `cfx_light` is configured with the target IP address of the machine running this script.

Add the following to your ESPHome configuration:

```yaml
light:
  - platform: cfx_light
    id: led_strip
    name: "CFX Strip"
    pin: GPIO27
    num_leds: 60
    chipset: SK6812
    all_effects: true
    default_transition_length: 0ms
    visualizer_ip: "192.168.31.228" # Change to the IP of the PC running visualizer.py
    visualizer_port: 7777

switch:
  - platform: template
    name: "Visualizer Mode"
    optimistic: true
    turn_on_action:
      lambda: |-
        auto *out = (cfx_light::CFXLightOutput *) id(led_strip).get_output();
        out->set_visualizer_enabled(true);
    turn_off_action:
      lambda: |-
        auto *out = (cfx_light::CFXLightOutput *) id(led_strip).get_output();
        out->set_visualizer_enabled(false);
```
