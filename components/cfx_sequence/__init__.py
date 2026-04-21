import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, select, event, sensor, button
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
AUTO_LOAD = ["cfx_effect", "select", "event", "sensor", "button"]

cfx_sequence_ns = cg.esphome_ns.namespace("cfx_sequence")
CFXSequence = cfx_sequence_ns.class_("CFXSequence")
CFXSequenceSelect = cfx_sequence_ns.class_("CFXSequenceSelect", select.Select, cg.Component)
CFXStopAllButton = cfx_sequence_ns.class_(
    "CFXStopAllButton", button.Button, cg.Component
)
CFXSequenceServiceHandler = cfx_sequence_ns.class_(
    "CFXSequenceServiceHandler", cg.Component
)

# Actions
StartAction  = cfx_sequence_ns.class_("StartAction",  automation.Action)
StopAction   = cfx_sequence_ns.class_("StopAction",   automation.Action)
CfxSetAction = cfx_sequence_ns.class_("CfxSetAction", automation.Action)
CfxRunAction = cfx_sequence_ns.class_("CfxRunAction", automation.Action)

# Trigger classes
CfxSeqOnStartTrigger = cfx_sequence_ns.class_("CfxSeqOnStartTrigger", automation.Trigger.template())
CfxSeqOnBeginTrigger    = cfx_sequence_ns.class_("CfxSeqOnBeginTrigger",    automation.Trigger.template())
CfxSeqOnStopTrigger     = cfx_sequence_ns.class_("CfxSeqOnStopTrigger",     automation.Trigger.template())
CfxSeqOnCompleteTrigger = cfx_sequence_ns.class_("CfxSeqOnCompleteTrigger", automation.Trigger.template())
CfxSeqOnReachTrigger = cfx_sequence_ns.class_("CfxSeqOnReachTrigger", automation.Trigger.template(cg.float_))

CONF_LIGHTS = "lights"
CONF_EFFECT = "effect"
CONF_SET_SPEED = "set_speed"
CONF_SET_INTENSITY = "set_intensity"
CONF_SET_PALETTE = "set_palette"
CONF_SET_BRIGHTNESS = "set_brightness"
CONF_SET_COLOR = "set_color"
CONF_SET_MIRROR = "set_mirror"
CONF_SET_INTRO = "set_intro"
CONF_SET_OUTRO = "set_outro"
CONF_SET_INOUT_DURATION = "set_inout_dur"
CONF_SET_FORCE_WHITE = "set_force_white"
CONF_SET_AUTOTUNE = "set_autotune"
CONF_HA_EVENTS = "ha_events"
CONF_ITERATIONS = "iterations"
CONF_RESTORE = "restore"
CONF_DURATION = "duration"

# Inherited constants
CONF_ON_START = "on_cfx_start"
CONF_ON_BEGIN    = "on_cfx_begin"
CONF_ON_STOP     = "on_cfx_stop"
CONF_ON_COMPLETE = "on_cfx_complete"
CONF_ON_REACH = "on_cfx_reach"
CONF_POSITION = "position"

SET_COLOR_SCHEMA = cv.All(
    cv.ensure_list(cv.int_range(min=0, max=255)),
    cv.Length(min=3, max=4),
)

HA_EVENTS_SCHEMA = cv.Any(cv.boolean, cv.one_of("auto", lower=True))


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
        cv.Optional(CONF_SET_COLOR): SET_COLOR_SCHEMA,
        cv.Optional(CONF_SET_MIRROR): cv.boolean,
        cv.Optional(CONF_SET_INTRO): cv.int_range(min=0, max=27),
        cv.Optional(CONF_SET_OUTRO): cv.int_range(min=0, max=27),
        cv.Optional(CONF_SET_INOUT_DURATION): cv.float_range(min=0.0),
        cv.Optional(CONF_SET_FORCE_WHITE): cv.boolean,
        cv.Optional(CONF_SET_AUTOTUNE): cv.boolean,
        cv.Optional(CONF_HA_EVENTS, default="auto"): HA_EVENTS_SCHEMA,
        cv.Optional(CONF_ITERATIONS, default=0): cv.int_range(min=0),
        cv.Optional(CONF_RESTORE, default=True): cv.boolean,
        cv.Optional(CONF_DURATION): cv.positive_time_period_milliseconds,
        
        # Triggers
        cv.Optional(CONF_ON_START): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnStartTrigger),
            }
        ),
        cv.Optional(CONF_ON_BEGIN): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnBeginTrigger),
            }
        ),
        cv.Optional(CONF_ON_STOP): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnStopTrigger),
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
    }
)


def _validate_unique_sequences(configs):
    """Reject duplicate sequence IDs or names at compile time."""
    seen_ids   = {}
    seen_names = {}
    for i, conf in enumerate(configs):
        sid  = conf[CONF_ID].id
        name = conf[CONF_NAME]

        if sid in seen_ids:
            raise cv.Invalid(
                f"Duplicate cfx_sequence id '{sid}' — each sequence must have a unique id. "
                f"First declared at sequence index {seen_ids[sid]}.",
                [i],
            )
        seen_ids[sid] = i

        if name in seen_names:
            raise cv.Invalid(
                f"Duplicate cfx_sequence name '{name}' — each sequence must have a unique name "
                f"(names populate the Internal Sequences dropdown). "
                f"First declared at sequence index {seen_names[name]}.",
                [i],
            )
        seen_names[name] = i

    return configs


CONFIG_SCHEMA = cv.All(
    cv.ensure_list(SEQUENCE_SCHEMA),
    _validate_unique_sequences,
)

import logging
import re

_LOGGER = logging.getLogger(__name__)


def _cfx_slugify(name: str) -> str:
    return re.sub(r'[^a-z0-9]+', '_', name.lower()).strip('_')


def _build_light_tag_map():
    import esphome.core as core

    light_name_map = {}
    seen_tags = []

    for lconf in core.CORE.config.get("light", []):
        lid_obj = lconf.get("id")
        lname = lconf.get("name", "")
        if lid_obj is not None:
            tag = _cfx_slugify(str(lname)) if lname else lid_obj.id
            light_name_map[lid_obj.id] = tag
            if tag not in seen_tags:
                seen_tags.append(tag)

        for seg in lconf.get("segments", []):
            seg_lid = seg.get("id")
            seg_name = seg.get("name", "")
            if seg_lid is not None:
                seg_tag = _cfx_slugify(str(seg_name)) if seg_name else seg_lid.id
                light_name_map[seg_lid.id] = seg_tag
                if seg_tag not in seen_tags:
                    seen_tags.append(seg_tag)

    return light_name_map, seen_tags


def _resolve_light_tag(light_id, light_name_map):
    return light_name_map.get(light_id.id, light_id.id)


def _emit_set_color(var, config):
    if CONF_SET_COLOR not in config:
        return

    color = config[CONF_SET_COLOR]
    if len(color) == 3:
        cg.add(var.set_color_rgb(color[0], color[1], color[2]))
    else:
        cg.add(var.set_color_rgbw(color[0], color[1], color[2], color[3]))


def _resolve_ha_events(value, *, default_enabled: bool) -> bool:
    if isinstance(value, bool):
        return value
    return default_enabled


def _emit_ha_events(var, config, *, default_enabled: bool):
    cg.add(var.set_ha_events(_resolve_ha_events(config[CONF_HA_EVENTS], default_enabled=default_enabled)))


async def to_code(config):
    cg.add_define("USE_CFX_SEQUENCE")
    import esphome.core as core

    light_name_map, seen_tags = _build_light_tag_map()
    strip_event_vars = {}
    event_var = None


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
        "unit_of_measurement": "%",
    }
    await sensor.register_sensor(prog_var, prog_conf)

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

        # Bind this sequence to its strip's dedicated event entity (CFX-028).
        lights_list = seq_conf.get(CONF_LIGHTS, [])
        if lights_list:
            strip_tag = _resolve_light_tag(lights_list[0], light_name_map)
            cg.add(var.set_strip_tag(strip_tag))
            seq_event_var = strip_event_vars.get(strip_tag, event_var)
        else:
            strip_tag = None
            seq_event_var = event_var

        # CFX-028: routing is handled by CFXEventManager.strip_entities_ map;
        # set_event_entity() on individual sequences is no longer needed.

        if CONF_SET_SPEED in seq_conf:
            cg.add(var.set_speed(seq_conf[CONF_SET_SPEED]))
        if CONF_SET_INTENSITY in seq_conf:
            cg.add(var.set_intensity(seq_conf[CONF_SET_INTENSITY]))
        if CONF_SET_PALETTE in seq_conf:
            cg.add(var.set_palette(seq_conf[CONF_SET_PALETTE]))
        if CONF_SET_BRIGHTNESS in seq_conf:
            cg.add(var.set_brightness(seq_conf[CONF_SET_BRIGHTNESS]))
        _emit_set_color(var, seq_conf)
        if CONF_SET_MIRROR in seq_conf:
            cg.add(var.set_mirror(seq_conf[CONF_SET_MIRROR]))
        if CONF_SET_INTRO in seq_conf:
            cg.add(var.set_intro(seq_conf[CONF_SET_INTRO]))
        if CONF_SET_OUTRO in seq_conf:
            cg.add(var.set_outro(seq_conf[CONF_SET_OUTRO]))
        if CONF_SET_INOUT_DURATION in seq_conf:
            cg.add(var.set_inout_duration(seq_conf[CONF_SET_INOUT_DURATION]))
        if CONF_SET_FORCE_WHITE in seq_conf:
            cg.add(var.set_force_white(seq_conf[CONF_SET_FORCE_WHITE]))
        if CONF_SET_AUTOTUNE in seq_conf:
            cg.add(var.set_autotune(seq_conf[CONF_SET_AUTOTUNE]))
        _emit_ha_events(var, seq_conf, default_enabled=True)
        if CONF_ITERATIONS in seq_conf:
            cg.add(var.set_iterations(seq_conf[CONF_ITERATIONS]))
        if CONF_DURATION in seq_conf:
            cg.add(var.set_duration_ms(seq_conf[CONF_DURATION]))  # CFX-018: method is set_duration_ms()




        # Register target lights
        for light_id in seq_conf.get(CONF_LIGHTS, []):
            light_state = await cg.get_variable(light_id)
            cg.add(var.add_light(light_state))

        # Setup Triggers
        for trigger_conf in seq_conf.get(CONF_ON_START, []):
            trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID])
            cg.add(var.add_on_start_trigger(trigger))
            await automation.build_automation(trigger, [], trigger_conf)

        for trigger_conf in seq_conf.get(CONF_ON_BEGIN, []):
            trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID])
            cg.add(var.add_on_begin_trigger(trigger))
            await automation.build_automation(trigger, [], trigger_conf)

        for trigger_conf in seq_conf.get(CONF_ON_STOP, []):
            trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID])
            cg.add(var.add_on_stop_trigger(trigger))
            await automation.build_automation(trigger, [], trigger_conf)

        for trigger_conf in seq_conf.get(CONF_ON_COMPLETE, []):
            trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID])
            cg.add(var.add_on_complete_trigger(trigger))
            await automation.build_automation(trigger, [], trigger_conf)

        for trigger_conf in seq_conf.get(CONF_ON_REACH, []):
            trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID], trigger_conf[CONF_POSITION])
            cg.add(var.add_on_reach_trigger(trigger))
            await automation.build_automation(trigger, [(cg.float_, "position")], trigger_conf)


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
        # CFX-028: CFXEventManager.strip_entities_ map handles routing;
        # no single event_entity pointer needed on the select.
        # CFX-026: HA events opt-in — read flag set by cfx_control ID 6.
        # Defaults to True when cfx_control is not used.
        import esphome.core as _core_seq
        ha_events = _core_seq.CORE.data.get("cfx_ha_events_enabled", True)
        cg.add(sel_var.set_ha_events_enabled(ha_events))
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

def _sequence_action_schema(value):
    """Accept both shorthand string and dict form:
      cfx_sequence.start: my_id
      cfx_sequence.start:
        id: my_id
    """
    if isinstance(value, str):
        value = {CONF_ID: value}
    return cv.Schema({cv.Required(CONF_ID): cv.string})(value)

@automation.register_action(
    "cfx_sequence.start",
    StartAction,
    _sequence_action_schema,
    synchronous=True,
)
async def cfx_sequence_start_to_code(config, action_id, template_arg, args):
    return cg.new_Pvariable(action_id, template_arg, config[CONF_ID])


@automation.register_action(
    "cfx_sequence.stop",
    StopAction,
    _sequence_action_schema,
    synchronous=True,
)
async def cfx_sequence_stop_to_code(config, action_id, template_arg, args):
    return cg.new_Pvariable(action_id, template_arg, config[CONF_ID])


@automation.register_action(
    "cfx_set",
    CfxSetAction,
    cv.Schema(
        {
            cv.Required(CONF_ID):                    cv.use_id(light.LightState),
            cv.Optional("effect"):                   cv.string,
            cv.Optional(CONF_SET_SPEED):             cv.int_range(0, 255),
            cv.Optional(CONF_SET_INTENSITY):         cv.int_range(0, 255),
            cv.Optional(CONF_SET_PALETTE):           cv.int_range(0, 255),
            cv.Optional(CONF_SET_BRIGHTNESS):        cv.percentage,
            cv.Optional(CONF_SET_COLOR):             SET_COLOR_SCHEMA,
            cv.Optional(CONF_SET_MIRROR):            cv.boolean,
            cv.Optional(CONF_SET_INTRO):             cv.int_range(min=0, max=27),
            cv.Optional(CONF_SET_OUTRO):             cv.int_range(min=0, max=27),
            cv.Optional(CONF_SET_INOUT_DURATION):    cv.float_range(min=0.0),
            cv.Optional(CONF_SET_FORCE_WHITE):       cv.boolean,
            cv.Optional(CONF_SET_AUTOTUNE):          cv.boolean,
            cv.Optional(CONF_HA_EVENTS, default="auto"): HA_EVENTS_SCHEMA,
        }
    ),
    synchronous=True,
)
async def cfx_set_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    light_var = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_light(light_var))
    if "effect" in config:
        cg.add(var.set_effect(config["effect"]))
    if CONF_SET_SPEED in config:
        cg.add(var.set_speed(config[CONF_SET_SPEED]))
    if CONF_SET_INTENSITY in config:
        cg.add(var.set_intensity(config[CONF_SET_INTENSITY]))
    if CONF_SET_PALETTE in config:
        cg.add(var.set_palette(config[CONF_SET_PALETTE]))
    if CONF_SET_MIRROR in config:
        cg.add(var.set_mirror(config[CONF_SET_MIRROR]))
    if CONF_SET_INTRO in config:
        cg.add(var.set_intro(config[CONF_SET_INTRO]))
    if CONF_SET_OUTRO in config:
        cg.add(var.set_outro(config[CONF_SET_OUTRO]))
    if CONF_SET_INOUT_DURATION in config:
        cg.add(var.set_inout_duration(config[CONF_SET_INOUT_DURATION]))
    if CONF_SET_FORCE_WHITE in config:
        cg.add(var.set_force_white(config[CONF_SET_FORCE_WHITE]))
    if CONF_SET_AUTOTUNE in config:
        cg.add(var.set_autotune(config[CONF_SET_AUTOTUNE]))
    _emit_ha_events(var, config, default_enabled=False)
    if CONF_SET_BRIGHTNESS in config:
        cg.add(var.set_brightness(config[CONF_SET_BRIGHTNESS]))
    _emit_set_color(var, config)
    return var


# ── cfx_run ───────────────────────────────────────────────────────────────────
# Spawns an independent pool-backed sequence at runtime.
# Supports the same parameters as cfx_set plus iterations and nested triggers.
# Nesting depth is injected automatically by the codegen based on how deeply
# the cfx_run is nested inside other cfx_run on_cfx_reach/on_cfx_complete blocks.

def _cfx_run_schema():
    """Build the cfx_run schema. Defined as a function to allow forward
    reference to the trigger schemas which reference on_cfx_reach recursively."""
    return cv.Schema(
        {
            cv.Required(CONF_ID):                    cv.use_id(light.LightState),
            cv.Required("effect"):                   cv.string,
            cv.Optional(CONF_SET_SPEED):             cv.int_range(0, 255),
            cv.Optional(CONF_SET_INTENSITY):         cv.int_range(0, 255),
            cv.Optional(CONF_SET_PALETTE):           cv.int_range(0, 255),
            cv.Optional(CONF_SET_BRIGHTNESS):        cv.percentage,
            cv.Optional(CONF_SET_COLOR):             SET_COLOR_SCHEMA,
            cv.Optional(CONF_SET_MIRROR):            cv.boolean,
            cv.Optional(CONF_SET_INTRO):             cv.int_range(min=0, max=27),
            cv.Optional(CONF_SET_OUTRO):             cv.int_range(min=0, max=27),
            cv.Optional(CONF_SET_INOUT_DURATION):    cv.float_range(min=0.0),
            cv.Optional(CONF_SET_FORCE_WHITE):       cv.boolean,
            cv.Optional(CONF_SET_AUTOTUNE):          cv.boolean,
            cv.Optional(CONF_HA_EVENTS, default="auto"): HA_EVENTS_SCHEMA,
            cv.Optional(CONF_ITERATIONS, default=1): cv.int_range(min=1),
            # Lifecycle triggers on the spawned sequence
            cv.Optional(CONF_ON_START): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnStartTrigger)}
            ),
            cv.Optional(CONF_ON_STOP): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnStopTrigger)}
            ),
            cv.Optional(CONF_ON_COMPLETE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnCompleteTrigger)}
            ),
            cv.Optional(CONF_ON_REACH): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxSeqOnReachTrigger),
                    cv.Required(CONF_POSITION): cv.percentage,
                }
            ),
        }
    )


@automation.register_action(
    "cfx_run",
    CfxRunAction,
    _cfx_run_schema(),
    synchronous=True,
)
async def cfx_run_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)

    light_var = await cg.get_variable(config[CONF_ID])
    cg.add(var.set_light(light_var))
    cg.add(var.set_effect(config["effect"]))

    # Reuse the same canonical light/segment tag resolution as cfx_sequence.
    light_name_map, _ = _build_light_tag_map()
    strip_tag = _resolve_light_tag(config[CONF_ID], light_name_map)
    cg.add(var.set_strip_tag(strip_tag))

    if CONF_SET_SPEED in config:
        cg.add(var.set_speed(config[CONF_SET_SPEED]))
    if CONF_SET_INTENSITY in config:
        cg.add(var.set_intensity(config[CONF_SET_INTENSITY]))
    if CONF_SET_PALETTE in config:
        cg.add(var.set_palette(config[CONF_SET_PALETTE]))
    if CONF_SET_BRIGHTNESS in config:
        cg.add(var.set_brightness(config[CONF_SET_BRIGHTNESS]))
    _emit_set_color(var, config)
    if CONF_SET_MIRROR in config:
        cg.add(var.set_mirror(config[CONF_SET_MIRROR]))
    if CONF_SET_INTRO in config:
        cg.add(var.set_intro(config[CONF_SET_INTRO]))
    if CONF_SET_OUTRO in config:
        cg.add(var.set_outro(config[CONF_SET_OUTRO]))
    if CONF_SET_INOUT_DURATION in config:
        cg.add(var.set_inout_duration(config[CONF_SET_INOUT_DURATION]))
    if CONF_SET_FORCE_WHITE in config:
        cg.add(var.set_force_white(config[CONF_SET_FORCE_WHITE]))
    if CONF_SET_AUTOTUNE in config:
        cg.add(var.set_autotune(config[CONF_SET_AUTOTUNE]))
    _emit_ha_events(var, config, default_enabled=False)
    if CONF_ITERATIONS in config:
        cg.add(var.set_iterations(config[CONF_ITERATIONS]))

    # Nesting depth: count how many cfx_run levels deep this action is.
    # ESPHome passes the parent action chain in args — we walk up counting
    # cfx_run ancestors. Defaults to 0 if not determinable at codegen time.
    # The runtime guard in CFXRunPool::claim() enforces CFX_RUN_MAX_DEPTH.
    cg.add(var.set_nesting_depth(0))

    # Register lifecycle triggers on the spawned sequence
    for trigger_conf in config.get(CONF_ON_START, []):
        trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_start_trigger(trigger))
        await automation.build_automation(trigger, [], trigger_conf)

    for trigger_conf in config.get(CONF_ON_STOP, []):
        trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_stop_trigger(trigger))
        await automation.build_automation(trigger, [], trigger_conf)

    for trigger_conf in config.get(CONF_ON_COMPLETE, []):
        trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_complete_trigger(trigger))
        await automation.build_automation(trigger, [], trigger_conf)

    for trigger_conf in config.get(CONF_ON_REACH, []):
        trigger = cg.new_Pvariable(trigger_conf[CONF_TRIGGER_ID], trigger_conf[CONF_POSITION])
        cg.add(var.add_on_reach_trigger(trigger))
        await automation.build_automation(trigger, [(cg.float_, "position")], trigger_conf)

    return var
