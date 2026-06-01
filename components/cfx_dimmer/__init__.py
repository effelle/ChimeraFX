import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import light
from esphome.const import CONF_ID

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["light"]

cfx_dimmer_ns = cg.esphome_ns.namespace("cfx_dimmer")
CFXDimmer = cfx_dimmer_ns.class_("CFXDimmer", cg.Component)
PressAction = cfx_dimmer_ns.class_("PressAction", automation.Action)
ReleaseAction = cfx_dimmer_ns.class_("ReleaseAction", automation.Action)

CONF_LIGHTS = "lights"
CONF_LONG_PRESS = "long_press"
CONF_RAMP_TIME = "ramp_time"
CONF_MIN_BRIGHTNESS = "min_brightness"
CONF_MAX_BRIGHTNESS = "max_brightness"
CONF_RESTORE_DIRECTION = "restore_direction"


def _validate_brightness_bounds(config):
    if config[CONF_MIN_BRIGHTNESS] >= config[CONF_MAX_BRIGHTNESS]:
        raise cv.Invalid("min_brightness must be lower than max_brightness")
    return config


CONFIG_SCHEMA = cv.All(
    cv.ensure_list(
        cv.All(
            cv.Schema(
                {
                    cv.GenerateID(): cv.declare_id(CFXDimmer),
                    cv.Required(CONF_LIGHTS): cv.ensure_list(
                        cv.use_id(light.LightState)
                    ),
                    cv.Optional(
                        CONF_LONG_PRESS, default="500ms"
                    ): cv.positive_time_period_milliseconds,
                    cv.Optional(
                        CONF_RAMP_TIME, default="2s"
                    ): cv.positive_time_period_milliseconds,
                    cv.Optional(CONF_MIN_BRIGHTNESS, default="1%"): cv.percentage,
                    cv.Optional(CONF_MAX_BRIGHTNESS, default="100%"): cv.percentage,
                    cv.Optional(CONF_RESTORE_DIRECTION, default=False): cv.boolean,
                }
            ).extend(cv.COMPONENT_SCHEMA),
            _validate_brightness_bounds,
        )
    )
)


async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID], conf[CONF_ID].id)
        await cg.register_component(var, conf)
        cg.add(var.set_long_press_ms(conf[CONF_LONG_PRESS].total_milliseconds))
        cg.add(var.set_ramp_time_ms(conf[CONF_RAMP_TIME].total_milliseconds))
        cg.add(var.set_min_brightness(conf[CONF_MIN_BRIGHTNESS]))
        cg.add(var.set_max_brightness(conf[CONF_MAX_BRIGHTNESS]))
        cg.add(var.set_restore_direction(conf[CONF_RESTORE_DIRECTION]))

        for light_id in conf[CONF_LIGHTS]:
            light_state = await cg.get_variable(light_id)
            cg.add(var.add_light(light_state))


def _dimmer_action_schema(value):
    if isinstance(value, str):
        value = {CONF_ID: value}
    return cv.Schema({cv.Required(CONF_ID): cv.use_id(CFXDimmer)})(value)


@automation.register_action(
    "cfx_dimmer.press",
    PressAction,
    _dimmer_action_schema,
    synchronous=True,
)
async def cfx_dimmer_press_to_code(config, action_id, template_arg, args):
    dimmer = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, dimmer)


@automation.register_action(
    "cfx_dimmer.release",
    ReleaseAction,
    _dimmer_action_schema,
    synchronous=True,
)
async def cfx_dimmer_release_to_code(config, action_id, template_arg, args):
    dimmer = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, dimmer)
