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
CONF_SET_MIRROR = "set_mirror"
CONF_SET_INTRO = "set_intro"
CONF_SET_OUTRO = "set_outro"
CONF_SET_INOUT_DURATION = "set_inout_dur"
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
        cv.Optional(CONF_SET_MIRROR): cv.boolean,
        cv.Optional(CONF_SET_INTRO): cv.int_range(min=0, max=24),
        cv.Optional(CONF_SET_OUTRO): cv.int_range(min=0, max=24),
        cv.Optional(CONF_SET_INOUT_DURATION): cv.float_range(min=0.0),
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

        # Event delivery uses the CFX Events entity (event.<node>_cfx_events).
        # Use 'state' trigger in HA automations — the event_type attribute
        # is pre-populated with all registered event strings for autocomplete.
    except cv.Invalid:
        raise
    except Exception:
        pass  # Non-fatal if api: block not present

    # One event entity per strip — see CFX-028 note below for why.
    import esphome.core as core

    # Build light yaml_id -> slugified_name map so strip tags match
    # get_object_id() at runtime. (CFX-024)
    import esphome.core as _core_lights
    light_name_map = {}
    for lconf in _core_lights.CORE.config.get("light", []):
        lid_obj = lconf.get("id")
        lname = lconf.get("name", "")
        if lid_obj is not None and lname:
            light_name_map[lid_obj.id] = _cfx_slugify(str(lname))
        # Also register segment lights — each segment is an independent LightState
        # with its own object_id and its own event tag. (CFX-026)
        for seg in lconf.get("segments", []):
            seg_id_obj = seg.get("id")
            seg_name = seg.get("name", "")
            if seg_id_obj is not None:
                slug = _cfx_slugify(str(seg_name)) if seg_name else str(seg_id_obj.id)
                light_name_map[seg_id_obj.id] = slug

    # Collect unique strip tags across all sequences. (CFX-024)
    seen_tags = []
    for seq_conf in config:
        for lid in seq_conf.get(CONF_LIGHTS, []):
            tag = light_name_map.get(lid.id, lid.id)
            if tag not in seen_tags:
                seen_tags.append(tag)

    _LOGGER.debug("CFX: light_name_map=%s seen_tags=%s", light_name_map, seen_tags)

    # Also collect tags for bare effects (no sequence) on CFX-capable lights
    # and their segments. (CFX-026)
    try:
        import esphome.core as _core3
        for lconf in _core3.CORE.config.get("light", []):
            # Parent light
            lid_obj = lconf.get("id")
            lname = lconf.get("name", "")
            if lid_obj is not None and lname:
                tag = _cfx_slugify(str(lname))
                if tag not in seen_tags:
                    # cfx_light stores effects as {"addressable_cfx": {...}}
                    for eff_conf in lconf.get("effects", []):
                        if "addressable_cfx" in eff_conf:
                            seen_tags.append(tag)
                            break
            # Segment lights — each is an independent LightState
            for seg in lconf.get("segments", []):
                seg_id_obj = seg.get("id")
                seg_name = seg.get("name", "")
                if seg_id_obj is None:
                    continue
                seg_tag = light_name_map.get(seg_id_obj.id, str(seg_id_obj.id))
                if seg_tag not in seen_tags:
                    seen_tags.append(seg_tag)
    except Exception as ex:
        _LOGGER.debug("CFX: CORE.config walk failed: %s", ex)

    # CFX-028: Per-strip event entities.
    #
    # The old design registered ONE global "CFX Events" entity whose
    # ListEntitiesEventResponse packet contained every event string for
    # every strip (4 lifecycle × N strips  +  20 reach milestones × N strips).
    # That packet exceeds the ESP32 API TCP send buffer for any non-trivial
    # setup, causing "Message too large to send: type=108" and the entity
    # never appearing in HA.
    #
    # Fix: create one event entity per strip.  Each registration packet only
    # carries strings for that single strip — 4 lifecycle + 20 reach = 24
    # strings — well within the buffer limit regardless of strip count.
    # HA sees entities named "CFX Events: <strip>" (e.g.
    # "CFX Events: led_strip1"), preserving full autocomplete for every
    # milestone value in the automation editor exactly as before.
    #
    # The C++ side is unchanged: CFXEventManager fires into whichever
    # event entity each CFXSequence instance was bound to at setup.
    # Sequences are bound to their strip's entity via CFXEventManager.strip_entities_ (CFX-028)
    # in the per-sequence codegen block further below.

    progress_step = 5
    milestones = list(range(progress_step, 101, progress_step))
    if 100 not in milestones:
        milestones.append(100)

    # tag -> event_var  (populated below, consumed in the per-sequence loop)
    strip_event_vars: dict = {}

    for tag in seen_tags:
        safe_id = re.sub(r'[^a-z0-9_]', '_', tag)
        eid = core.ID(
            f"cfx_events_{safe_id}",
            is_declaration=True,
            type=event.Event,
        )
        evar = cg.new_Pvariable(eid)
        core.CORE.component_ids.add(f"cfx_events_{safe_id}")
        strip_event_vars[tag] = evar

        event_types = (
            [f"cfx_start:{tag}", f"cfx_begin:{tag}",
             f"cfx_stop:{tag}",  f"cfx_complete:{tag}"]
            + [f"cfx_reach:{tag}:{m}" for m in milestones]
        )
        econf = {
            "id": eid,
            "name": f"CFX Events: {tag}",
            "icon": "mdi:animation-play",
            "disabled_by_default": False,
            "internal": False,
        }
        await event.register_event(evar, econf, event_types=event_types)
        # Register this strip's entity in CFXEventManager's dispatch table.
        cg.add(cg.RawExpression(
            f'cfx_sequence::CFXEventManager::get().register_strip_entity'
            f'("{tag}", {evar})'
        ))

    # Backward-compat alias: code below that references event_var falls back to
    # the first strip's entity when there is only one strip (common case).
    event_var = next(iter(strip_event_vars.values())) if strip_event_vars else None


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
            strip_tag = light_name_map.get(lights_list[0].id, lights_list[0].id)
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
        if CONF_SET_MIRROR in seq_conf:
            cg.add(var.set_mirror(seq_conf[CONF_SET_MIRROR]))
        if CONF_SET_INTRO in seq_conf:
            cg.add(var.set_intro(seq_conf[CONF_SET_INTRO]))
        if CONF_SET_OUTRO in seq_conf:
            cg.add(var.set_outro(seq_conf[CONF_SET_OUTRO]))
        if CONF_SET_INOUT_DURATION in seq_conf:
            cg.add(var.set_inout_duration(seq_conf[CONF_SET_INOUT_DURATION]))
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
            cv.Optional(CONF_SET_MIRROR):            cv.boolean,
            cv.Optional(CONF_SET_INTRO):             cv.int_range(min=0, max=24),
            cv.Optional(CONF_SET_OUTRO):             cv.int_range(min=0, max=24),
            cv.Optional(CONF_SET_INOUT_DURATION):    cv.float_range(min=0.0),
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
    if CONF_SET_BRIGHTNESS in config:
        cg.add(var.set_brightness(config[CONF_SET_BRIGHTNESS]))
    return var
