import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["binary_sensor"]

cfx_button_ns = cg.esphome_ns.namespace("cfx_button")
CFXButton = cfx_button_ns.class_("CFXButton", cg.Component)

cfx_dimmer_ns = cg.esphome_ns.namespace("cfx_dimmer")
CFXDimmer = cfx_dimmer_ns.class_("CFXDimmer", cg.Component)
cfx_cct_sweeper_ns = cg.esphome_ns.namespace("cfx_cct_sweeper")
CFXCCTSweeper = cfx_cct_sweeper_ns.class_("CFXCCTSweeper", cg.Component)
cfx_hue_cycler_ns = cg.esphome_ns.namespace("cfx_hue_cycler")
CFXHueCycler = cfx_hue_cycler_ns.class_("CFXHueCycler", cg.Component)
cfx_effect_selector_ns = cg.esphome_ns.namespace("cfx_effect_selector")
CFXEffectSelector = cfx_effect_selector_ns.class_(
    "CFXEffectSelector", cg.Component
)

CONF_BUTTON = "button"
CONF_DIMMER = "dimmer"
CONF_CCT_SWEEPER = "cct_sweeper"
CONF_HUE_CYCLER = "hue_cycler"
CONF_EFFECT_SELECTOR = "effect_selector"

CONTROLLER_KEYS = (
    CONF_DIMMER,
    CONF_CCT_SWEEPER,
    CONF_HUE_CYCLER,
    CONF_EFFECT_SELECTOR,
)

CONFIG_SCHEMA = cv.ensure_list(
    cv.All(
        cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(CFXButton),
                cv.Required(CONF_BUTTON): cv.use_id(binary_sensor.BinarySensor),
                cv.Optional(CONF_DIMMER): cv.use_id(CFXDimmer),
                cv.Optional(CONF_CCT_SWEEPER): cv.use_id(CFXCCTSweeper),
                cv.Optional(CONF_HUE_CYCLER): cv.use_id(CFXHueCycler),
                cv.Optional(CONF_EFFECT_SELECTOR): cv.use_id(
                    CFXEffectSelector
                ),
            }
        ).extend(cv.COMPONENT_SCHEMA),
        cv.has_exactly_one_key(*CONTROLLER_KEYS),
    )
)


async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)

        button = await cg.get_variable(conf[CONF_BUTTON])
        cg.add(var.set_button(button))

        controller_key = next(key for key in CONTROLLER_KEYS if key in conf)
        controller = await cg.get_variable(conf[controller_key])
        cg.add(var.set_controller(controller))
