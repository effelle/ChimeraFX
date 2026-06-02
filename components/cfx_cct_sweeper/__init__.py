import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import light
from esphome.const import CONF_ID

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["light"]

cfx_cct_sweeper_ns = cg.esphome_ns.namespace("cfx_cct_sweeper")
CFXCCTSweeper = cfx_cct_sweeper_ns.class_("CFXCCTSweeper", cg.Component)
PressAction = cfx_cct_sweeper_ns.class_("PressAction", automation.Action)
ReleaseAction = cfx_cct_sweeper_ns.class_("ReleaseAction", automation.Action)

CONF_LIGHTS = "lights"
CONF_LONG_PRESS = "long_press"
CONF_SWEEP_TIME = "sweep_time"
CONF_FAVORITE_WHITE = "favorite_white"
CONF_WARM_WHITE = "warm_white"
CONF_COOL_WHITE = "cool_white"
CONF_RESTORE_DIRECTION = "restore_direction"


def _color4(value):
    value = cv.ensure_list(cv.percentage)(value)
    if len(value) != 4:
        raise cv.Invalid("Expected four percentage values: [red, green, blue, white]")
    return value


CONFIG_SCHEMA = cv.ensure_list(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CFXCCTSweeper),
            cv.Required(CONF_LIGHTS): cv.ensure_list(cv.use_id(light.LightState)),
            cv.Optional(
                CONF_LONG_PRESS, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_SWEEP_TIME, default="4s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_FAVORITE_WHITE, default=["100%", "100%", "100%", "100%"]): _color4,
            cv.Optional(CONF_WARM_WHITE, default=["100%", "55%", "18%", "100%"]): _color4,
            cv.Optional(CONF_COOL_WHITE, default=["70%", "85%", "100%", "100%"]): _color4,
            cv.Optional(CONF_RESTORE_DIRECTION, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID], conf[CONF_ID].id)
        await cg.register_component(var, conf)
        cg.add(var.set_long_press_ms(conf[CONF_LONG_PRESS].total_milliseconds))
        cg.add(var.set_sweep_time_ms(conf[CONF_SWEEP_TIME].total_milliseconds))
        cg.add(var.set_favorite_white(*conf[CONF_FAVORITE_WHITE]))
        cg.add(var.set_warm_white(*conf[CONF_WARM_WHITE]))
        cg.add(var.set_cool_white(*conf[CONF_COOL_WHITE]))
        cg.add(var.set_restore_direction(conf[CONF_RESTORE_DIRECTION]))

        for light_id in conf[CONF_LIGHTS]:
            light_state = await cg.get_variable(light_id)
            cg.add(var.add_light(light_state))


def _action_schema(value):
    if isinstance(value, str):
        value = {CONF_ID: value}
    return cv.Schema({cv.Required(CONF_ID): cv.use_id(CFXCCTSweeper)})(value)


@automation.register_action(
    "cfx_cct_sweeper.press",
    PressAction,
    _action_schema,
    synchronous=True,
)
async def cfx_cct_sweeper_press_to_code(config, action_id, template_arg, args):
    sweeper = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, sweeper)


@automation.register_action(
    "cfx_cct_sweeper.release",
    ReleaseAction,
    _action_schema,
    synchronous=True,
)
async def cfx_cct_sweeper_release_to_code(config, action_id, template_arg, args):
    sweeper = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, sweeper)
