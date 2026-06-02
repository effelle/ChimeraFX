import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import light
from esphome.const import CONF_ID

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["light"]

cfx_effect_selector_ns = cg.esphome_ns.namespace("cfx_effect_selector")
CFXEffectSelector = cfx_effect_selector_ns.class_("CFXEffectSelector", cg.Component)
PressAction = cfx_effect_selector_ns.class_("PressAction", automation.Action)
ReleaseAction = cfx_effect_selector_ns.class_("ReleaseAction", automation.Action)

CONF_LIGHTS = "lights"
CONF_EFFECTS = "effects"
CONF_LONG_PRESS = "long_press"
CONF_EFFECT_INTERVAL = "effect_interval"


def _effect_list(value):
    value = cv.ensure_list(cv.string)(value)
    if not value:
        raise cv.Invalid("effects must contain at least one effect name")
    return value


CONFIG_SCHEMA = cv.ensure_list(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CFXEffectSelector),
            cv.Required(CONF_LIGHTS): cv.ensure_list(cv.use_id(light.LightState)),
            cv.Required(CONF_EFFECTS): _effect_list,
            cv.Optional(
                CONF_LONG_PRESS, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_EFFECT_INTERVAL, default="900ms"
            ): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID], conf[CONF_ID].id)
        await cg.register_component(var, conf)
        cg.add(var.set_long_press_ms(conf[CONF_LONG_PRESS].total_milliseconds))
        cg.add(
            var.set_effect_interval_ms(
                conf[CONF_EFFECT_INTERVAL].total_milliseconds
            )
        )

        for light_id in conf[CONF_LIGHTS]:
            light_state = await cg.get_variable(light_id)
            cg.add(var.add_light(light_state))

        for effect in conf[CONF_EFFECTS]:
            cg.add(var.add_effect(effect))


def _action_schema(value):
    if isinstance(value, str):
        value = {CONF_ID: value}
    return cv.Schema({cv.Required(CONF_ID): cv.use_id(CFXEffectSelector)})(value)


@automation.register_action(
    "cfx_effect_selector.press",
    PressAction,
    _action_schema,
    synchronous=True,
)
async def cfx_effect_selector_press_to_code(config, action_id, template_arg, args):
    selector = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, selector)


@automation.register_action(
    "cfx_effect_selector.release",
    ReleaseAction,
    _action_schema,
    synchronous=True,
)
async def cfx_effect_selector_release_to_code(config, action_id, template_arg, args):
    selector = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, selector)
