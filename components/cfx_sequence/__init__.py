import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, select, event, sensor, number, text
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
AUTO_LOAD = ["cfx_effect", "select", "event", "sensor", "number", "text"]

cfx_sequence_ns = cg.esphome_ns.namespace("cfx_sequence")
CFXSequence = cfx_sequence_ns.class_("CFXSequence")
CFXSequenceSelect = cfx_sequence_ns.class_("CFXSequenceSelect", select.Select, cg.Component)
CFXProgressStepNumber = cfx_sequence_ns.class_("CFXProgressStepNumber", number.Number, cg.Component)
CFXPixelWatchText = cfx_sequence_ns.class_("CFXPixelWatchText", text.Text, cg.Component)

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

# Inherited constants
CONF_ON_START = "on_start"
CONF_ON_COMPLETE = "on_complete"
CONF_ON_REACH = "on_reach"
CONF_POSITION = "position"
CONF_ON_PIXEL_NUM = "on_pixel_num"
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
_LOGGER = logging.getLogger(__name__)

async def to_code(config):
    cg.add_define("USE_CFX_SEQUENCE")

    # Phase E: Check for api: batch_delay configuration
    # This is a heuristic — we check if the api component is loaded and advise the user.
    try:
        import esphome.core as _core
        api_conf = _core.CORE.config.get("api", {})
        batch_delay = api_conf.get("batch_delay", None)
        # batch_delay is typically a TimePeriod object in ESPHome config, so we cast to string to check
        if batch_delay is not None and str(batch_delay) != "0ms":
            _LOGGER.warning(
                "ChimeraFX events are enabled but 'api: batch_delay' is '%s'. "
                "Set 'api: batch_delay: 0ms' for sub-10ms cfx_start/cfx_complete "
                "event delivery to Home Assistant.",
                str(batch_delay),
            )
    except Exception:
        pass  # Non-fatal: warning is advisory only

    # Create global event entity for all sequences (one per device)
    import esphome.core as core
    event_id = core.ID("cfx_global_events", is_declaration=True, type=event.Event)
    event_var = cg.new_Pvariable(event_id)
    core.CORE.component_ids.add("cfx_global_events")

    event_types = ["cfx_start", "cfx_complete", "cfx_reach", "cfx_pixel"]
    event_conf = {
        "id": event_id,
        "name": "CFX Events",
        "icon": "mdi:animation-play",
        "disabled_by_default": False,
        "internal": False,
    }
    await event.register_event(event_var, event_conf, event_types=event_types)

    # 1. Progress Step Number
    step_id = core.ID("cfx_progress_step", is_declaration=True, type=CFXProgressStepNumber)
    step_var = cg.new_Pvariable(step_id)
    core.CORE.component_ids.add("cfx_progress_step")
    step_conf = {
        "id": step_id,
        "name": "CFX Progress Step",
        "icon": "mdi:percent",
        "mode": number.NUMBER_MODES["BOX"],
        "disabled_by_default": False,
        "internal": False,
    }
    await number.register_number(step_var, step_conf, min_value=0, max_value=50, step=1)
    await cg.register_component(step_var, step_conf)
    cg.add(step_var.set_unit_of_measurement("%"))

    # 2. Progress Sensor
    prog_id = core.ID("cfx_progress", is_declaration=True, type=sensor.Sensor)
    prog_var = cg.new_Pvariable(prog_id)
    core.CORE.component_ids.add("cfx_progress")
    prog_conf = {
        "id": prog_id,
        "name": "CFX Progress",
        "icon": "mdi:percent-circle",
        "state_class": sensor.STATE_CLASSES["measurement"],
        "accuracy_decimals": 0,
        "disabled_by_default": False,
        "internal": False,
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
        "disabled_by_default": False,
        "internal": False,
        "force_update": False,
        "entity_category": cv.ENTITY_CATEGORIES["diagnostic"],
    }
    await sensor.register_sensor(last_px_var, last_px_conf)

    # 4. Pixel Watch List Text
    watch_id = core.ID("cfx_pixel_watch_list", is_declaration=True, type=CFXPixelWatchText)
    watch_var = cg.new_Pvariable(watch_id)
    core.CORE.component_ids.add("cfx_pixel_watch_list")
    watch_conf = {
        "id": watch_id,
        "name": "CFX Pixel Watch List",
        "icon": "mdi:format-list-numbered",
        "mode": text.TEXT_MODES["TEXT"],
        "disabled_by_default": False,
        "internal": False,
    }
    await text.register_text(watch_var, watch_conf)
    await cg.register_component(watch_var, watch_conf)

    for seq_conf in config:
        # Pass ID string, Name string, Effect string, and Restore boolean
        var = cg.new_Pvariable(seq_conf[CONF_ID], seq_conf[CONF_ID].id, seq_conf[CONF_NAME], seq_conf[CONF_EFFECT], seq_conf[CONF_RESTORE])
        # Note: We do NOT await cg.register_component(var, seq_conf) to avoid Circular Dependency on IDs

        # Bind the global event entity to this sequence
        if event_var:
            cg.add(var.set_event_entity(event_var))
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
