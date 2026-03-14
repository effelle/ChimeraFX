# ChimeraFX Orchestrator

The ChimeraFX Orchestrator (`cfx_sequence`) is the **Logic Layer** of your lighting. It allows your LED strips to move beyond simple repeating loops and become an organic, responsive part of your environment.

### 🛡️ Built for Reliability: Two Ways to Sequence

ChimeraFX gives you two ways to orchestrate your lights, an internal sequence engine and Home Assistant automations. Choosing the right one is key to a professional lighting setup.

| Feature | On-Device Sequence (`cfx_sequence`) | Home Assistant Automation |
| :--- | :--- | :--- |
| **Logic Location** | Runs directly on the ESP32 hardware. | Runs on your Home Assistant Server. |
| **Reliability** | **Immune** to network lag or Wi-Fi drops. | Dependent on network stability. |
| **Precision** | **Microsecond timing**. Perfect for "handovers". | **Millisecond latency** (~10-200ms). |
| **Ecosystem** | Limited to ChimeraFX components. | Connects to any smart device (TTS, Plugs). |
| **Best For** | Timing-critical organic animations. | High-level triggers and notifications. |

---

## ⚡ Examples: Internal vs. External

The following examples show how to react to the same sequence logic in both environments.

??? example "1. Start & Complete Triggers"
    *React immediately when a sequence begins or finishes its cycle.*

    === "On-Device (YAML)"
        ```yaml
        cfx_sequence:
          - id: intro_seq
            name: "Intro Sweep"
            lights: [strip_1]
            on_cfx_start:
              then:
                - logger.log: "Animation started on hardware"
            on_cfx_complete:
              then:
                - cfx_sequence.start: main_loop_seq
        ```

    === "Home Assistant"
        ```yaml
        # Use the 'state' trigger on the CFX Events entity.
        # Filter by the 'event_type' attribute.
        automation:
          triggers:
            - trigger: state
              entity_id: event.YOUR_DEVICE_ID_cfx_events
              attribute: event_type
              to: cfx_complete
          action:
            - service: notify.mobile_app
              data:
                message: "The light show is complete!"
        ```

??? example "2. Proximity Triggers (`on_cfx_reach`)"
    *The "Edge" of ChimeraFX: react as the animation physically moves across the device.*

    === "On-Device (YAML)"
        ```yaml
        # Seamless handover: Start strip_2 exactly as strip_1 hits 100%.
        cfx_sequence:
          - id: strip_1_wipe
            on_cfx_reach:
              - position: 50%
                then:
                  - cfx_sequence.start: strip_2_wipe
        ```

    === "Home Assistant"
        ```yaml
        # Sync a secondary lamp to match the current sequence progress.
        automation:
          triggers:
            - trigger: state
              entity_id: event.YOUR_DEVICE_ID_cfx_events
              attribute: event_type
              to: cfx_reach
          conditions:
            - condition: numeric_state
              entity_id: sensor.YOUR_DEVICE_ID_sequence_progress
              above: 49
              below: 51
          action:
            - service: light.turn_on
              target: { entity_id: light.mood_lamp }
              data: {}
        ```

??? example "3. Precision Pixel Watch (`on_cfx_pixel`)"
    *Trigger actions exactly when the light passes a specific physical point.*

    === "On-Device (YAML)"
        ```yaml
        # Fast reaction: Pulse a specific segment when the master wipe passes it.
        cfx_sequence:
          - id: master_wipe
            on_cfx_pixel:
              - pixel: 45
                then:
                  - light.turn_on: { id: segment_at_pixel_45, brightness: 100% }
        ```

    === "Home Assistant"
        ```yaml
        # Security: Snapshot when the light "passes" the door (Pixel 124).
        automation:
          triggers:
            - trigger: state
              entity_id: event.YOUR_DEVICE_ID_cfx_events
              attribute: event_type
              to: cfx_pixel
          condition:
            # cfx_pixel fires unconditionally for every pixel update.
            # Use a numeric_state condition to gate your automation.
            - condition: numeric_state
              entity_id: sensor.YOUR_DEVICE_ID_last_pixel
              above: 44
              below: 46
          action:
            - service: camera.snapshot
              target: { entity_id: camera.front_door }
        ```

---

## ⚡ Performance Optimization: Event Latency

By default, ESPHome batches network updates to prevent Wi-Fi congestion. While this is great for standard sensors, it can introduce a ~200ms delay in Home Assistant receiving your lighting events.

For the most responsive "Logic Layer," we recommend reducing the API batch delay:

```yaml
api:
  batch_delay: 0ms
```

> [!NOTE]
> **Why is there a delay?**
> ESPHome defaults to `200ms` to group multiple sensor updates into a single network packet, saving energy and reducing network noise. Setting it to `0ms` tells the device to fire events **instantly** the microsecond they occur—critical for tightly synced automations.

---

## 🏠 Home Assistant Dashboard Setup

ChimeraFX exposes several entities to help you monitor and configure your logic from the dashboard.

??? info "Entity List & Usage (Click to expand)"
    | Entity | Usage | Location |
    | :--- | :--- | :--- |
    | **Internal Sequences** | The main dropdown to trigger sequences stored on-device. | **Controls** |
    | **CFX Events** | The event hub used for triggers (Start/Complete/Reach/Pixel). | Diagnostic |
    | **Sequence Progress** | Sensor showing the current position (0-100%). | Diagnostic |
    | **Sequence Step** | Configuration: How often to fire `cfx_reach` (e.g., every 5%). | Configuration |
    | **Last Pixel** | Diagnostic: Shows the index of the leading pixel for filtering `cfx_pixel` events. | Diagnostic |

---

## Configuration Variables (YAML)

| Variable | Type | Required | Description |
|----------|------|----------|-------------|
| **id** | ID | Yes | Unique identifier for referencing in YAML actions. |
| **name** | string | Yes | The name displayed in the Home Assistant dropdown. |
| **lights** | list | Yes | IDs of simple lights or segments to control. |
| **effect** | string | Yes | The CFX effect to apply (e.g., "Wipe", "Fire"). |
| **on_cfx_start** | trigger | No | Action to fire when the sequence begins. |
| **on_cfx_complete** | trigger | No | Action to fire when iterations are complete. |
| **on_cfx_reach** | trigger | No | Fires at a specific % position (requires position). |
| **on_cfx_pixel** | trigger | No | Fires at a specific pixel index (requires pixel). |
| **iterations** | int | No | Cycles before finishing. 0 = indefinitely. |
| **restore** | bool | No | Return strips to their pre-sequence state on stop. |

---

## 🛠️ Native ESPHome Integration (Actions)

ChimeraFX isn't a standalone system—it is built to live inside the **ESPHome Automation Engine**. Every sequence is a first-class citizen that can be triggered, stopped, or chained directly from hardware events (buttons) or software logic (MQTT, sensor values).

### `cfx_sequence.start`
Starts a sequence manually by its YAML ID. This allows you to create high-fidelity hardware interactions that bypass the network entirely.

```yaml
# Example: Physical Button Trigger
binary_sensor:
  - platform: gpio
    pin: GPIO4
    on_press:
      - cfx_sequence.start: my_sequence_id
```

### `cfx_sequence.stop`
Immediately stops a running sequence. If `restore: true` was set, the device returns to its pre-sequence state.

```yaml
# Example: Emergency Stop on sensor trigger
binary_sensor:
  - platform: gpio
    pin: GPIO5
    on_press:
      - cfx_sequence.stop: my_sequence_id
```

---

- **`cfx_reach`**: Fired when progress reaches a multiple of the **Progress Step**.
- **`cfx_complete`**: Fired when a sequence finishes its requested **Iterations**.
- **`cfx_pixel`**: Fired unconditionally every time the leading pixel advances. Filter in HA using the `cfx_last_pixel` sensor.

---

## 🚀 Home Assistant Automations

ChimeraFX events integrate natively with Home Assistant. You can trigger automations using the built-in event selectors.

### Example: Triggering on Reach
Since `cfx_reach` fires at multiple milestones (e.g., 10%, 20%), you can filter by the **Progress** sensor if you need a specific point, or simply trigger on the event itself for general progress actions.

```yaml
trigger:
  - platform: state
    entity_id: event.esp32_test_cfx_events
    attribute: event_type
    to: "cfx_reach"
```

### Tips for Reliable Triggers:
- **Progress Step**: Set this to a value that matches your automation needs (e.g., `10` for every 10%).
- **State Transformer**: For immediate reaction, ensure your ESPHome `api` configuration has a low `batch_delay`.

---

## Technical Edge: Why ChimeraFX is Different
ChimeraFX isn't built to replace all-in-one effects hubs like WLED. It is built for **Industrial-grade lighting logic**. While other platforms focus on visual presets, ChimeraFX focuses on the **Logic Layer**—ensuring that if your light "passes a point," your system knows about it instantly and timing remains absolute, even without a network connection.
