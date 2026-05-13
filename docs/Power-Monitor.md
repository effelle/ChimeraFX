# ChimeraFX Power Monitor

The ChimeraFX Power Monitor (`cfx_power`) provides real-time power consumption estimates for your LED strip installation. It helps you understand how much power your lights draw and gives you manual control to reduce brightness when needed.

## How It Works

The power monitor is **estimate-first**, not measurement-first:

1. **Frame-based sampling**: Each time ChimeraFX transmits a frame to the LEDs, it records an instantaneous power estimate based on the current pixel colors.
2. **Rolling average**: Over each `update_interval` window (default: 5 seconds), ChimeraFX averages all frame estimates.
3. **Published sensors**: The averaged values are published to Home Assistant as sensors.

This approach is efficient because it piggybacks on the existing frame transmission cycle without requiring extra ADC (Analog-to-Digital Converter) hardware or wiring to measure the electrical current.

> **Important**: The power monitor does **not** perform automatic brightness limiting unless you explicitly enable `limit.auto`. When monitoring is enabled, ChimeraFX automatically exposes a manual **Power Reduction** control as an emergency fallback or global day/night cap. Use the optional `limit` section to customize that control.

---

## Basic Configuration

Add the following to the **root level** of your ESPHome YAML configuration:

```yaml
cfx_power:
  monitor:
    mains_voltage: 230.0       # Required - your AC mains voltage
    psu_current_limit: 12A     # Optional but recommended. PSU max current output.
```

### Monitor Parameters

| Parameter | Type | Default | Description |
|:---|:---|:---|:---|
| **mains_voltage** | float | *(required)* | Your AC mains voltage (e.g., 120.0 for US, 230.0 for EU/UK). |
| **psu_current_limit** | string | `0A` | Your PSU's maximum current output (must include 'A', e.g., `12A` or `5.5A`). |
| update_interval | time | `5s` | How often to publish sensor updates. |
| supply_voltage | float | `5.0` | LED strip supply voltage (usually 5V). |
| psu_efficiency | float | `0.85` | PSU efficiency factor (0.01-1.0). |
| power_factor | float | `0.90` | Power factor for AC calculations (0.01-1.0). |
| controller_current_ma | float | `120.0` | ESP32 controller board consumption. |
| idle_current_ma | float | `1.0` | LED strip standby current per channel. |
| rgb_channel_current_ma | float | `20.0` | Typical current per RGB channel per LED. |
| white_channel_current_ma | float | `20.0` | Typical current for white channel per LED. |
| mains_voltage_sensor | sensor | *(none)* | Live AC voltage from Home Assistant. |
| power_factor_sensor | sensor | *(none)* | Live power factor from Home Assistant. |
| sensors | dict | *(see below)* | Configure which sensors are generated. |

> **[TIP]** `mains_voltage_sensor` and `power_factor_sensor` are optional, but can be used to calculate power readings more accurately. If you don't configure them—or if the sensors become unavailable—ChimeraFX will automatically fall back to the static values provided by the `mains_voltage` and `power_factor` parameters.

### Limit Parameters

The limit control is generated automatically when `monitor` is present. Add `limit:` only when you want to override the defaults.

| Parameter | Type | Default | Description |
|:---|:---|:---|:---|
| restore | boolean | `true` | Restore last power reduction on reboot. |
| name | string | `"Power Reduction"` | Display name for the dropdown entity. |
| icon | icon | `mdi:brightness-percent` | Icon for the dropdown entity. |
| auto.safe_hold_time | time | `30s` | Optional auto-release delay after demand returns to `SAFE`. |

---

## Understanding the Sensors

### Standard Sensors (Auto-Generated)

These are created automatically when `cfx_power` is configured:

| Sensor | Unit | Description |
|:---|:---|:---|
| **DC Current** | A | Estimated DC current draw from the 5V supply. |
| **DC Power** | W | Estimated DC power consumption (voltage × current). |

### Optional Sensors

Enable these under `cfx_power.monitor.sensors:`:

```yaml
cfx_power:
  monitor:
    mains_voltage: 230.0     # Replace with your mains voltage
    psu_current_limit: 12A   # Replace with your real PSU current limit
    sensors:
      ac_power:
      energy:
      psu_load:
      budget_status:
      apparent_power:
      ac_current:
```

Each optional entity can also be customized with `name:` and `id:` for Home Assistant organization and automations:

```yaml
cfx_power:
  monitor:
    mains_voltage: 230.0
    sensors:
      ac_power:
        id: cfx_ac_power
        name: "LED AC Power"
      budget_status:
        id: cfx_budget_status
        name: "LED Budget Status"
```

| Sensor | Unit | Description |
|:---|:---|:---|
| **AC Power** | W | Estimated AC power draw (DC Power ÷ PSU Efficiency). |
| **Energy** | kWh | Cumulative energy consumption over time. |
| **PSU Load** | % | Percentage of configured PSU capacity being used. |
| **Budget Status** | text | Overall status: `SAFE`, `WARNING`, `OVERBUDGET`, or `NO_LIMIT`. |
| **Apparent Power** | VA | AC apparent power (AC Power ÷ Power Factor). |
| **AC Current** | A | Estimated AC current draw. |

### Budget Status Thresholds

The `budget_status` text sensor reports based on PSU load:

| Status | Condition |
|:---|:---|
| `SAFE` | PSU Load < 85%; operating within normal PSU thermal/efficiency range. |
| `WARNING` | 85% <= PSU Load <= 100%; increase reduction or simplify the effect. |
| `OVERBUDGET` | PSU Load > 100%; apply a larger reduction or upgrade the PSU. |
| `NO_LIMIT` | `psu_current_limit` not configured. |

---

## Power Reduction (Manual Limiting)

The **Power Reduction** dropdown is generated automatically when `monitor` is enabled. The `limit` section customizes it:

```yaml
cfx_power:
  monitor:
    mains_voltage: 230.0
  limit:       # Optional; generated automatically when monitor is enabled
    restore: true
    name: "Power Reduction"
    icon: mdi:brightness-percent
```

### Reduction Steps

| Selection | Brightness Reduction | Effect |
|:---|:---|:---|
| 0% | None | Full brightness. |
| 10% | 10% | Slight dim. |
| 20% | 20% | Moderate dim. |
| 30% | 30% | Significant dim. |
| 40% | 40% | Heavy reduction. |
| 50% | 50% | Emergency reduction. |
| 60% | 60% | Strong emergency reduction. |
| 70% | 70% | Very strong emergency reduction. |
| 80% | 80% | Near-minimum output. |
| 90% | 90% | Night cap / last-resort emergency output. |

> **[TIP]** You could use your lights without reduction during the day, and enable 50% reduction (or more) at night using a simple automation. This saves power without having to create completely new scenes or presets!

### How It Works

- **Smooth transitions**: When you change the reduction level, ChimeraFX smoothly ramps over a fixed 800ms safety window.
- **Effect preservation**: The brightness reduction is applied at the very last second before sending data to the LEDs. This means your active effects and colors continue running perfectly in the background without being distorted by the dimming.
- **Persistence**: By default, the selected reduction survives reboots. Set `restore: false` to reset to 0% on startup.

### Optional Auto Reduction

Auto reduction is disabled by default. Enable it only when you want ChimeraFX to temporarily raise the global reduction based on your `psu_current_limit`:

```yaml
cfx_power:
  monitor:
    mains_voltage: 230.0
    psu_current_limit: 12A
    sensors:
      budget_status:
      psu_load:
  limit:
    auto:
```

Auto mode behavior:

- Auto safety checks run every 2 seconds, independently from the slower sensor publish interval.
- `WARNING` requests at least 20% reduction, then keeps stepping up until the load reaches `SAFE`.
- `OVERBUDGET` requests at least 50% reduction.
- Repeated `OVERBUDGET` safety checks escalate by 10% per check, up to 90%.
- The auto reduction is released when the lights are off, or after demand stays `SAFE` for `safe_hold_time` (default 30s) and the pre-auto manual reduction would also be safe.
- Manual reduction is still respected; the effective reduction is the larger of the manual and auto values.
- While auto reduction is active, lower manual selections are ignored for safety; stronger manual selections act as temporary overrides.
- The pre-auto manual value is restored when auto releases.

> **[NOTE]** Auto reduction is a safety guard, not a show-sequencer. If a sequence turns on several strips with bright monochromatic effects, ChimeraFX may start reducing output during an intro or transition. That can look like the animation is bending downward, but it means the estimated load crossed the configured PSU budget. For polished show sequences, size the PSU for the sequence peak or apply a manual reduction before starting the sequence.

---

## Calibrating Current Estimates

> ⚠️ **WARNING: Electrical Safety First!** 
> Measuring live current requires working with powered electrical equipment. **Do not attempt to measure your power supply with a multimeter unless you are completely confident in what you are doing.** Mishandling a multimeter (such as using the wrong ports or bridging connections) can cause short circuits, destroyed equipment, severe injury, or fire. The default values in ChimeraFX are safe and accurate enough for most users—**calibration is entirely optional.**

The default values (20mA per RGB channel, 20mA per white channel) work reasonably well out-of-the-box for standard WS2812B strips. However, if you are experienced with a multimeter and want highly accurate estimates, you can calibrate the system using a multimeter. Current draw can vary based on:

- **LED brand and quality**: Higher-quality LEDs may draw less current.
- **Strip density**: 60 LEDs/m vs 144 LEDs/m.
- **Color content**: White and bright colors draw more than dark colors.
- **Supply voltage**: 5V vs 12V WS2811 configurations.

### Measuring Your Strip

1. Set your strip to solid **white at full brightness**.
2. Measure the actual DC current from your PSU using a multimeter.
3. Calculate per-LED current: `measured_current / num_leds`.
4. For RGBW strips: `(measured_current - controller_current) / num_leds`, then divide by 4 for per-channel values.

### Calibration Example

```yaml
cfx_power:
  monitor:
    mains_voltage: 230.0
    psu_current_limit: 12A
    supply_voltage: 5.1                    # Measured with multimeter
    rgb_channel_current_ma: 18.5           # Calibrated from measurement
    white_channel_current_ma: 22.0         # White typically draws more
    controller_current_ma: 150.0           # Measured ESP32 board consumption
```

### WS2811 Special Considerations

WS2811 strips at 12V use a different architecture with groupings of 3 LEDs per IC. Current varies significantly with:

- Input voltage (9V-12V range).
- LED grouping configuration.
- Built-in resistor values.

For WS2811 strips, it is highly recommended to measure your actual consumption and calibrate accordingly.

---

## Live AC Calibration

For more accurate AC power estimates, you can integrate live measurements from Home Assistant:

```yaml
sensor:
  - platform: homeassistant
    entity_id: sensor.grid_voltage
    id: live_voltage

  - platform: homeassistant
    entity_id: sensor.power_factor
    id: live_power_factor

cfx_power:
  monitor:
    mains_voltage: 230.0      # Static fallback
    power_factor: 0.90       # Static fallback
    mains_voltage_sensor: live_voltage
    power_factor_sensor: live_power_factor
```

### How It Works

- **With live sensors**: ChimeraFX uses the live readings for `apparent_power` and `ac_current` calculations.
- **Fallback values**: The static `mains_voltage` and `power_factor` remain as fallbacks if the Home Assistant sensors become unavailable.
- **Validation**: Invalid readings (out of range or unavailable) automatically fall back to static values.

---

## Renaming Sensors

All sensors can be renamed for better Home Assistant organization:

```yaml
cfx_power:
  monitor:
    mains_voltage: 230.0
    psu_current_limit: 12A
    sensors:
      dc_current:
        name: "TV LED Strip Current"
      dc_power:
        name: "TV LED Strip Power"
      ac_power:
        name: "TV LED Strip AC Power"
        icon: mdi:flash
      psu_load:
        name: "TV Power Supply Load"
      budget_status:
        name: "TV Power Budget"
```

---

## Full Configuration Example

```yaml
cfx_power:
  monitor:
    mains_voltage: 120.0
    psu_current_limit: 10A
    supply_voltage: 5.0
    psu_efficiency: 0.88
    power_factor: 0.92
    controller_current_ma: 130.0
    idle_current_ma: 0.5
    rgb_channel_current_ma: 18.0
    white_channel_current_ma: 22.0
    update_interval: 15s
    sensors:
      dc_current:
        name: "LED Current"
      dc_power:
        name: "LED Power"
      ac_power:
        name: "LED AC Power"
      apparent_power:
        name: "LED Apparent Power"
      ac_current:
        name: "LED AC Current"
      energy:
        name: "LED Energy"
        icon: mdi:counter
      psu_load:
        name: "PSU Load"
      budget_status:
        name: "Power Budget"
  limit:
    restore: true
    name: "Brightness Limit"
    icon: mdi:brightness-percent
```

---

## Troubleshooting

### PSU Load Shows 0% Even at Full Brightness

1. Verify `psu_current_limit` is set (without it, load percentage cannot be calculated).
2. Check that your strips have effects running (power is only sampled during frame transmission).
3. Try setting `update_interval: 1s` to see faster updates.

### Values Seem Too High or Too Low

- Review [Calibrating Current Estimates](#calibrating-current-estimates) above.
- For RGBW strips, ensure you're using `chipset: SK6812` so RGBW mode is auto-enabled.
- Check `supply_voltage` matches your actual PSU output (measure with a multimeter).

### Budget Status Stuck on "NO_LIMIT"

Set `psu_current_limit` in your monitor configuration:

```yaml
cfx_power:
  monitor:
    mains_voltage: 230.0
    psu_current_limit: 12A
```

### Power Reduction Doesn't Affect Effects

The power reduction scales the *transmit* brightness, not the internal effect buffers. This is intentional:

- Effect calculations remain accurate and unmodified.
- Only the final output is scaled, providing smooth transitions.
- Perfect for temporary "dim for movie time" scenarios without altering your carefully configured effect presets.

### Energy Sensor Resets on Reboot

Home Assistant records the energy sensor on each state change. ChimeraFX also keeps a sparse on-device backup so the estimated counter can recover after a reboot.

If the restored value looks wrong:

1. **Ensure flash is enabled**: Ensure your ESP32 has flash storage enabled in the configuration.
2. **Check the logs**: Look at the ESPHome logs for any flash-related errors.
3. **Understand the backup limitation**: The on-device backup is intentionally saved slowly (about once per hour) to prevent wearing out the ESP32's flash memory. Because Home Assistant already tracks total energy securely via the Energy Dashboard, the ESP32's internal value should be treated as a localized backup, not your primary utility meter.
