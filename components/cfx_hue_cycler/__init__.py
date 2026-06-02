import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import light
from esphome.const import CONF_ID

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["light"]

cfx_hue_cycler_ns = cg.esphome_ns.namespace("cfx_hue_cycler")
CFXHueCycler = cfx_hue_cycler_ns.class_("CFXHueCycler", cg.Component)
PressAction = cfx_hue_cycler_ns.class_("PressAction", automation.Action)
ReleaseAction = cfx_hue_cycler_ns.class_("ReleaseAction", automation.Action)

CONF_LIGHTS = "lights"
CONF_LONG_PRESS = "long_press"
CONF_CYCLE_TIME = "cycle_time"
CONF_WHITE = "white"
CONF_SATURATION = "saturation"
CONF_RESTORE_HUE = "restore_hue"


def _color4(value):
    value = cv.ensure_list(cv.percentage)(value)
    if len(value) != 4:
        raise cv.Invalid("Expected four percentage values: [red, green, blue, white]")
    return value


CONFIG_SCHEMA = cv.ensure_list(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CFXHueCycler),
            cv.Required(CONF_LIGHTS): cv.ensure_list(cv.use_id(light.LightState)),
            cv.Optional(
                CONF_LONG_PRESS, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_CYCLE_TIME, default="6s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_WHITE, default=["100%", "100%", "100%", "100%"]): _color4,
            cv.Optional(CONF_SATURATION, default="100%"): cv.percentage,
            cv.Optional(CONF_RESTORE_HUE, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID], conf[CONF_ID].id)
        await cg.register_component(var, conf)
        cg.add(var.set_long_press_ms(conf[CONF_LONG_PRESS].total_milliseconds))
        cg.add(var.set_cycle_time_ms(conf[CONF_CYCLE_TIME].total_milliseconds))
        cg.add(var.set_white(*conf[CONF_WHITE]))
        cg.add(var.set_saturation(conf[CONF_SATURATION]))
        cg.add(var.set_restore_hue(conf[CONF_RESTORE_HUE]))

        for light_id in conf[CONF_LIGHTS]:
            light_state = await cg.get_variable(light_id)
            cg.add(var.add_light(light_state))


def _action_schema(value):
    if isinstance(value, str):
        value = {CONF_ID: value}
    return cv.Schema({cv.Required(CONF_ID): cv.use_id(CFXHueCycler)})(value)


@automation.register_action(
    "cfx_hue_cycler.press",
    PressAction,
    _action_schema,
    synchronous=True,
)
async def cfx_hue_cycler_press_to_code(config, action_id, template_arg, args):
    cycler = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, cycler)


@automation.register_action(
    "cfx_hue_cycler.release",
    ReleaseAction,
    _action_schema,
    synchronous=True,
)
async def cfx_hue_cycler_release_to_code(config, action_id, template_arg, args):
    cycler = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, cycler)
