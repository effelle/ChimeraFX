import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome import automation
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_TRIGGER_ID,
)

DEPENDENCIES = ["light"]
AUTO_LOAD = ["cfx_effect"]

cfx_sequencer_ns = cg.esphome_ns.namespace("cfx_sequencer")
CFXSequencer = cfx_sequencer_ns.class_("CFXSequencer", cg.Component)

# Trigger classes specific to sequencer (to be mapped to sequencer C++ classes)
CfxSeqOnStartTrigger = cfx_sequencer_ns.class_("CfxSeqOnStartTrigger", automation.Trigger.template())
CfxSeqOnCompleteTrigger = cfx_sequencer_ns.class_("CfxSeqOnCompleteTrigger", automation.Trigger.template())
CfxSeqOnReachTrigger = cfx_sequencer_ns.class_("CfxSeqOnReachTrigger", automation.Trigger.template(cg.float_))
CfxSeqOnPixelNumTrigger = cfx_sequencer_ns.class_("CfxSeqOnPixelNumTrigger", automation.Trigger.template(cg.int32))

CONF_LIGHTS = "lights"
CONF_ACTIONS = "actions"

# Inherited constants originally from cfx_effect schema
CONF_ON_START = "on_start"
CONF_ON_COMPLETE = "on_complete"
CONF_ON_REACH = "on_reach"
CONF_POSITION = "position"
CONF_ON_PIXEL_NUM = "on_pixel_num"
CONF_PIXEL = "pixel"

# The sequence action schema (represents one step in the sequence)
SEQUENCE_ACTION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.string,
        # TODO: Add specific action parameters (effect_id, speed, iterations) when defining sequencer language
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CFXSequencer),
        cv.Required(CONF_LIGHTS): cv.ensure_list(cv.use_id(light.LightState)),
        # Trigger definitions inherited from old effect
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
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    cg.add_define("USE_CFX_SEQUENCER")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Register target lights
    for light_id in config.get(CONF_LIGHTS, []):
        light_state = await cg.get_variable(light_id)
        cg.add(var.add_light(light_state))

    # Setup Triggers
    for conf in config.get(CONF_ON_START, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_start_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_COMPLETE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_on_complete_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_REACH, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], conf[CONF_POSITION])
        cg.add(var.add_on_reach_trigger(trigger))
        await automation.build_automation(trigger, [(cg.float_, "position")], conf)

    for conf in config.get(CONF_ON_PIXEL_NUM, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], conf[CONF_PIXEL])
        cg.add(var.add_on_pixel_num_trigger(trigger))
        await automation.build_automation(trigger, [(cg.int32, "pixel")], conf)
