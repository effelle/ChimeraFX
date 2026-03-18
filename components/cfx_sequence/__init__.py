import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, select, sensor, number, button, text_sensor
from esphome import automation
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_TRIGGER_ID,
    CONF_DISABLED_BY_DEFAULT,
    CONF_INTERNAL,
    CONF_ICON,
)

DEPENDENCIES = ["light"]
AUTO_LOAD = ["cfx_effect", "select", "event", "sensor", "number", "button"]

cfx_sequence_ns = cg.esphome_ns.namespace("cfx_sequence")
CFXSequence = cfx_sequence_ns.class_("CFXSequence")
CFXSequenceSelect = cfx_sequence_ns.class_("CFXSequenceSelect", select.Select, cg.Component)
CFXProgressStepNumber = cfx_sequence_ns.class_("CFXProgressStepNumber", number.Number, cg.Component)
CFXStopAllButton = cfx_sequence_ns.class_(
    "CFXStopAllButton", button.Button, cg.Component
)
CFXSequenceServiceHandler = cfx_sequence_ns.class_(
    "CFXSequenceServiceHandler", cg.Component
)

# Actions
StartAction = cfx_sequence_ns.class_("StartAction", automation.Action)
StopAction = cfx_sequence_ns.class_("StopAction", automation.Action)

# Trigger classes
CfxSeqOnStartTrigger = cfx_sequence_ns.class_("CfxSeqOnStartTrigger", automation.Trigger.template())
CfxSeqOnCompleteTrigger = cfx_sequence_ns.class_("CfxSeqOnCompleteTrigger", automation.Trigger.template())
CfxSeqOnReachTrigger = cfx_sequence_ns.class_("CfxSeqOnReachTrigger", automation.Trigger.template(cg.float_))
CfxSeqOnPixelNumTrigger = cfx_sequence_ns.class_("CfxSeqOnPixelNumTrigger", automation.Trigger.template(cg.int32))

CONF_LIGHTS = "lights"
CONF_EFFECT = "effect"
CONF_SET_SPEED = "set_speed"
CONF_SET_INTENSITY = "set_intensity"
CONF_SET_PALETTE = "set_palette"
CONF_SET_BRIGHTNESS = "set_brightness"
CONF_ITERATIONS = "iterations"
CONF_RESTORE = "restore"
CONF_PIXEL_STEP = "pixel_step"
CONF_DURATION = "duration"

# Inherited constants
CONF_ON_START = "on_cfx_start"
CONF_ON_COMPLETE = "on_cfx_complete"
CONF_ON_REACH = "on_cfx_reach"
CONF_POSITION = "position"
CONF_ON_PIXEL_NUM = "on_cfx_pixel"
CONF_PIXEL = "pixel"


SEQUENCE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CFXSequence),
        cv.Required(CONF_NAME): cv.string,
        cv.Required(CONF_LIGHTS): cv.ensure_list(cv.use_id(light.LightState)),
        cv.Optional(CONF_EFFECT, default=""): cv.string,
        cv.Optional(CONF_SET_SPEED): cv.int_range(0, 255),
        cv.Optional(CONF_SET_INTENSITY): cv.int_range(0, 255),
        cv.Optional(CONF_SET_PALETTE): cv.int_range(0, 255),
        cv.Optional(CONF_SET_BRIGHTNESS): cv.percentage,
        cv.Optional(CONF_ITERATIONS, default=0): cv.int_range(min=0),
        cv.Optional(CONF_RESTORE, default=True): cv.boolean,
        cv.Optional(CONF_PIXEL_STEP, default=0): cv.int_range(min=0, max=255),
        cv.Optional(CONF_DURATION): cv.positive_time_period_milliseconds,
        
        # Triggers
        cv.Optional(CONF_ON_START): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnStartTrigger),
            }
        ),
        cv.Optional(CONF_ON_COMPLETE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnCompleteTrigger),
            }
        ),
        cv.Optional(CONF_ON_REACH): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnReachTrigger),
                cv.Required(CONF_POSITION): cv.percentage,
            }
        ),
        cv.Optional(CONF_ON_PIXEL_NUM): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnPixelNumTrigger),
                cv.Required(CONF_PIXEL): cv.int_,
            }
        ),
    }
)


CONFIG_SCHEMA = cv.All(
    cv.ensure_list(SEQUENCE_SCHEMA),
)

import logging
import re

_LOGGER = logging.getLogger(__name__)

def _cfx_slugify(name: str) -> str:
    """Replicate ESPHome's object_id derivation from a friendly name.
    Matches what LightState::get_object_id() returns at runtime so that
    codegen-registered event_types always match the C++ fired strings."""
    return re.sub(r'[^a-z0-9]+', '_', name.lower()).strip('_')

async def to_code(config):
    cg.add_define("USE_CFX_SEQUENCE")

    # Phase E: Check for api: configuration
    try:
        import esphome.core as _core
        api_conf = _core.CORE.config.get("api", {})

        batch_delay = api_conf.get("batch_delay", None)
        if batch_delay is not None and str(batch_delay) != "0ms":
            _LOGGER.warning(
                "ChimeraFX events are enabled but 'api: batch_delay' is '%s'. "
                "Set 'api: batch_delay: 0ms' for sub-10ms cfx_start/cfx_complete "
                "event delivery to Home Assistant.",
                str(batch_delay),
            )

        # CFX-025: auto-enable homeassistant_services if available for
        # fire_homeassistant_event() direct event bus delivery.
        # Optional — falls back to event entity if not set.
        ha_services = api_conf.get("homeassistant_services", False)
        if ha_services:
            cg.add_define("USE_CFX_HA_SERVICES")
            _LOGGER.info("CFX: homeassistant_services enabled — using direct event bus delivery")
        else:
            _LOGGER.info("CFX: add 'homeassistant_services: true' under 'api:' for "
                         "most reliable event delivery to Home Assistant.")
    except cv.Invalid:
        raise
    except Exception:
        pass  # Non-fatal if api: block not present

    # CFX Events State text_sensor (internal/disabled — event entity is primary). (CFX-025)
    import esphome.core as core
    evt_ts_id = core.ID("cfx_event_state", is_declaration=True, type=text_sensor.TextSensor)
    evt_ts_var = cg.new_Pvariable(evt_ts_id)
    core.CORE.component_ids.add("cfx_event_state")
    evt_ts_conf = {
        "id": evt_ts_id,
        "name": "CFX Event State",
        "icon": "mdi:bell-ring",
        "disabled_by_default": True,
        "internal": True,
        "entity_category": cv.ENTITY_CATEGORIES["diagnostic"],
    }
    await text_sensor.register_text_sensor(evt_ts_var, evt_ts_conf)

    # 1. Progress Step Number
    step_id = core.ID("cfx_progress_step", is_declaration=True, type=CFXProgressStepNumber)
    step_var = cg.new_Pvariable(step_id)
    core.CORE.component_ids.add("cfx_progress_step")
    step_conf = {
        "id": step_id,
        "name": "Sequence Step",
        "icon": "mdi:percent",
        "mode": number.NUMBER_MODES["BOX"],
        "unit_of_measurement": "%",
        "disabled_by_default": False,
        "internal": False,
        "entity_category": cv.ENTITY_CATEGORIES["config"],
    }
    await number.register_number(step_var, step_conf, min_value=0, max_value=50, step=1)
    await cg.register_component(step_var, step_conf)

    # 2. Progress Sensor
    prog_id = core.ID("cfx_progress", is_declaration=True, type=sensor.Sensor)
    prog_var = cg.new_Pvariable(prog_id)
    core.CORE.component_ids.add("cfx_progress")
    prog_conf = {
        "id": prog_id,
        "name": "Sequence Progress",
        "icon": "mdi:percent-circle",
        "state_class": sensor.STATE_CLASSES["measurement"],
        "accuracy_decimals": 0,
        "disabled_by_default": False,
        "internal": True,
        "force_update": False,
        "entity_category": cv.ENTITY_CATEGORIES["diagnostic"],
    }
    await sensor.register_sensor(prog_var, prog_conf)
    cg.add(prog_var.set_unit_of_measurement("%"))

    # 3. Last Pixel Sensor
    last_px_id = core.ID("cfx_last_pixel", is_declaration=True, type=sensor.Sensor)
    last_px_var = cg.new_Pvariable(last_px_id)
    core.CORE.component_ids.add("cfx_last_pixel")
    last_px_conf = {
        "id": last_px_id,
        "name": "CFX Last Pixel",
        "icon": "mdi:led-on",
        "state_class": sensor.STATE_CLASSES["measurement"],
        "accuracy_decimals": 0,
        "disabled_by_default": True,
        "internal": True,
        "force_update": False,
        "entity_category": cv.ENTITY_CATEGORIES["diagnostic"],
    }
    await sensor.register_sensor(last_px_var, last_px_conf)



    for seq_conf in config:
        # Pass ID string, Name string, Effect string, and Restore boolean
        var = cg.new_Pvariable(seq_conf[CONF_ID], seq_conf[CONF_ID].id, seq_conf[CONF_NAME], seq_conf[CONF_EFFECT], seq_conf[CONF_RESTORE])
        # Note: We do NOT await cg.register_component(var, seq_conf) to avoid Circular Dependency on IDs

        # Bind the global event entity to this sequence
        if evt_ts_var:
            cg.add(var.set_event_text_sensor(evt_ts_var))
        if prog_var:
            cg.add(var.set_progress_sensor(prog_var))
        if last_px_var:
            cg.add(var.set_last_pixel_sensor(last_px_var))

        if CONF_SET_SPEED in seq_conf:
            cg.add(var.set_speed(seq_conf[CONF_SET_SPEED]))
        if CONF_SET_INTENSITY in seq_conf:
            cg.add(var.set_intensity(seq_conf[CONF_SET_INTENSITY]))
        if CONF_SET_PALETTE in seq_conf:
            cg.add(var.set_palette(seq_conf[CONF_SET_PALETTE]))
        if CONF_SET_BRIGHTNESS in seq_conf:
            cg.add(var.set_brightness(seq_conf[CONF_SET_BRIGHTNESS]))
        if CONF_ITERATIONS in seq_conf:
            cg.add(var.set_iterations(seq_conf[CONF_ITERATIONS]))
        if CONF_PIXEL_STEP in seq_conf and seq_conf[CONF_PIXEL_STEP] > 0:
            cg.add(var.set_pixel_step(seq_conf[CONF_PIXEL_STEP]))
        if CONF_DURATION in seq_conf:
            cg.add(var.set_duration_ms(seq_conf[CONF_DURATION]))  # CFX-018: method is set_duration_ms()

        # CFX-024: strip identity tag = slugify(light.name), matching get_object_id() at runtime.
        lights_list = seq_conf.get(CONF_LIGHTS, [])
        if lights_list:
            strip_tag = light_name_map.get(lights_list[0].id, lights_list[0].id)
            cg.add(var.set_strip_tag(strip_tag))


        # Register target lights
        for light_id in seq_conf.get(CONF_LIGHTS, []):
            light_state = await cg.get_variable(light_id)
            cg.add(var.add_light(light_state))

        # Setup Triggers
        for trigger_conf in seq_conf.get(CONF_ON_START, []):
            trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID])
            cg.add(var.add_on_start_trigger(trigger))
            await automation.build_automation(trigger, [], trigger_conf)

        for trigger_conf in seq_conf.get(CONF_ON_COMPLETE, []):
            trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID])
            cg.add(var.add_on_complete_trigger(trigger))
            await automation.build_automation(trigger, [], trigger_conf)

        for trigger_conf in seq_conf.get(CONF_ON_REACH, []):
            trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID], trigger_conf[CONF_POSITION])
            cg.add(var.add_on_reach_trigger(trigger))
            await automation.build_automation(trigger, [(cg.float_, "position")], trigger_conf)

        for trigger_conf in seq_conf.get(CONF_ON_PIXEL_NUM, []):
            trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID], trigger_conf[CONF_PIXEL])
            cg.add(var.add_on_pixel_num_trigger(trigger))
            await automation.build_automation(trigger, [(cg.int32, "pixel")], trigger_conf)

    # ----------------------------------------------------
    # Generate the global Sequence Select Dropdown
    # ----------------------------------------------------
    if len(config) > 0:
        seq_options = ["None"]
        for seq_conf in config:
            seq_options.append(seq_conf[CONF_NAME])
            
        var_id = core.ID("cfx_global_sequence_select", is_declaration=True, type=CFXSequenceSelect)
        sel_var = cg.new_Pvariable(var_id)
        core.CORE.component_ids.add("cfx_global_sequence_select")

        sel_conf = {
            CONF_ID: var_id,
            CONF_NAME: "Internal Sequences",
            CONF_ICON: "mdi:movie-open-play",
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
        }
            
        await select.register_select(sel_var, sel_conf, options=seq_options)
        await cg.register_component(sel_var, sel_conf)
        cg.add(sel_var.publish_state("None"))
        # Wire global entities into CFXSequenceSelect (which drives CFXEventManager)
        if evt_ts_var:
            cg.add(sel_var.set_event_text_sensor(evt_ts_var))
        # Register all known strip tags so discovery fires all milestones at boot
        for tag in seen_tags:
            cg.add(sel_var.add_known_tag(tag))

        # Stop All button
        stop_id = core.ID(
            "cfx_stop_all", is_declaration=True, type=CFXStopAllButton
        )
        stop_var = cg.new_Pvariable(stop_id)
        core.CORE.component_ids.add("cfx_stop_all")
        stop_conf = {
            CONF_ID: stop_id,
            CONF_NAME: "Stop All",
            CONF_ICON: "mdi:stop-circle-outline",
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
        }
        await button.register_button(stop_var, stop_conf)
        await cg.register_component(stop_var, stop_conf)

        # HA Service handler (only when API component is loaded)
        try:
            import esphome.core as _core
            if "api" in _core.CORE.config:
                svc_id = core.ID(
                    "cfx_sequence_service_handler",
                    is_declaration=True,
                    type=CFXSequenceServiceHandler,
                )
                if "api" in _core.CORE.config:
                    # Unlock CustomAPIDevice::register_service()
                    # We define the exact internal macros required by custom_api_device.h
                    cg.add_define("USE_API_USER_DEFINED_ACTIONS")
                    cg.add_define("USE_API_CUSTOM_SERVICES")
                    
                    # If the user hasn't explicitly enabled custom_services in YAML,
                    # the 'api' component won't provide the necessary template symbols
                    # for std::string arguments. We detect this and tell the C++ side
                    # to provide fallback symbols to avoid linker errors.
                    if not _core.CORE.config["api"].get("custom_services", False):
                        cg.add_define("CHIMERAFX_NEED_API_SYMBOLS")
                svc_var = cg.new_Pvariable(svc_id)
                core.CORE.component_ids.add("cfx_sequence_service_handler")
                await cg.register_component(svc_var, {})
        except Exception:
            pass  # Non-fatal: service handler is advisory only

@automation.register_action(
    "cfx_sequence.start",
    StartAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.string,
        }
    ),
)
async def cfx_sequence_start_to_code(config, action_id, template_arg, args):
    # Pass the raw target ID string directly to break the codegen dependency graph
    return cg.new_Pvariable(action_id, template_arg, config[CONF_ID])


@automation.register_action(
    "cfx_sequence.stop",
    StopAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.string,
        }
    ),
)
async def cfx_sequence_stop_to_code(config, action_id, template_arg, args):
    # Pass the raw target ID string directly to break the codegen dependency graph
    return cg.new_Pvariable(action_id, template_arg, config[CONF_ID])
