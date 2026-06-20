import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, light
from esphome.const import CONF_ID
from esphome.final_validate import full_config

CODEOWNERS = ["@effelle"]
DEPENDENCIES = []
AUTO_LOAD = [
    "cfx_dimmer",
    "cfx_cct_sweeper",
    "cfx_hue_cycler",
    "cfx_effect_selector",
]

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
CONF_LIGHTS = "lights"
CONF_LONG_PRESS = "long_press"
CONF_RAMP_TIME = "ramp_time"
CONF_MIN_BRIGHTNESS = "min_brightness"
CONF_MAX_BRIGHTNESS = "max_brightness"
CONF_SWEEP_TIME = "sweep_time"
CONF_NATIVE_WHITE = "native_white"
CONF_PREFERRED_WHITE = "preferred_white"
CONF_FAVORITE_WHITE = "favorite_white"
CONF_RESTORE = "restore"
CONF_CYCLE_TIME = "cycle_time"
CONF_COLORS = "colors"
CONF_COLOR_INTERVAL = "color_interval"
CONF_FIXED_COLOR = "fixed_color"
CONF_SATURATION = "saturation"
CONF_EFFECTS = "effects"
CONF_EFFECT_INTERVAL = "effect_interval"
CONF_SEGMENTS = "segments"
MIN_BRIGHTNESS_FLOOR = 0.15

CONTROLLER_KEYS = (
    CONF_DIMMER,
    CONF_CCT_SWEEPER,
    CONF_HUE_CYCLER,
    CONF_EFFECT_SELECTOR,
)


def _color4(value):
    value = cv.ensure_list(cv.percentage)(value)
    if len(value) != 4:
        raise cv.Invalid(
            "Expected four percentage values: [red, green, blue, white]"
        )
    return value


def _effect_list(value):
    value = cv.ensure_list(cv.string)(value)
    if not value:
        raise cv.Invalid("effects must contain at least one effect name")
    return value


def _color_list(value):
    value = cv.ensure_list(_color4)(value)
    if not value:
        raise cv.Invalid("colors must contain at least one RGBW color")
    return value


def _validate_hue_mode(config):
    palette_mode = CONF_COLORS in config
    if palette_mode and CONF_CYCLE_TIME in config:
        raise cv.Invalid("cycle_time cannot be used with colors")
    if palette_mode and CONF_SATURATION in config:
        raise cv.Invalid("saturation cannot be used with colors")
    if not palette_mode and CONF_COLOR_INTERVAL in config:
        raise cv.Invalid("color_interval requires colors")
    if not palette_mode:
        config.setdefault(
            CONF_CYCLE_TIME, cv.positive_time_period_milliseconds("6s")
        )
        config.setdefault(CONF_SATURATION, 1.0)
    else:
        config.setdefault(
            CONF_COLOR_INTERVAL,
            cv.positive_time_period_milliseconds("900ms"),
        )
    return config


def _validate_brightness_bounds(config):
    if config[CONF_MIN_BRIGHTNESS] < MIN_BRIGHTNESS_FLOOR:
        raise cv.Invalid("min_brightness must be at least 15%")
    if config[CONF_MIN_BRIGHTNESS] >= config[CONF_MAX_BRIGHTNESS]:
        raise cv.Invalid("min_brightness must be lower than max_brightness")
    return config


def _resolve_preferred_white(config):
    if CONF_PREFERRED_WHITE in config and CONF_FAVORITE_WHITE in config:
        raise cv.Invalid(
            "preferred_white and legacy favorite_white cannot both be configured"
        )
    if CONF_PREFERRED_WHITE not in config:
        config[CONF_PREFERRED_WHITE] = config.pop(
            CONF_FAVORITE_WHITE, [1.0, 1.0, 1.0, 1.0]
        )
    return config


LIGHTS_SCHEMA = {
    cv.Required(CONF_LIGHTS): cv.ensure_list(cv.use_id(light.LightState))
}

DIMMER_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CFXDimmer),
            **LIGHTS_SCHEMA,
            cv.Optional(
                CONF_LONG_PRESS, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_RAMP_TIME, default="2s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MIN_BRIGHTNESS, default="15%"): cv.percentage,
            cv.Optional(CONF_MAX_BRIGHTNESS, default="100%"): cv.percentage,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_brightness_bounds,
)

CCT_SWEEPER_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CFXCCTSweeper),
            **LIGHTS_SCHEMA,
            cv.Optional(
                CONF_LONG_PRESS, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_SWEEP_TIME, default="4s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_NATIVE_WHITE,
                default=["0%", "0%", "0%", "100%"],
            ): _color4,
            cv.Optional(CONF_PREFERRED_WHITE): _color4,
            cv.Optional(CONF_FAVORITE_WHITE): _color4,
            cv.Optional(CONF_RESTORE, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _resolve_preferred_white,
)

HUE_CYCLER_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CFXHueCycler),
            **LIGHTS_SCHEMA,
            cv.Optional(
                CONF_LONG_PRESS, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_CYCLE_TIME): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_COLORS): _color_list,
            cv.Optional(
                CONF_COLOR_INTERVAL
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_FIXED_COLOR,
                default=["100%", "100%", "100%", "100%"],
            ): _color4,
            cv.Optional(CONF_SATURATION): cv.percentage,
            cv.Optional(CONF_RESTORE, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_hue_mode,
)

EFFECT_SELECTOR_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CFXEffectSelector),
        **LIGHTS_SCHEMA,
        cv.Required(CONF_EFFECTS): _effect_list,
        cv.Optional(
            CONF_LONG_PRESS, default="500ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_EFFECT_INTERVAL, default="900ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_RESTORE, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)

CONFIG_SCHEMA = cv.ensure_list(
    cv.All(
        cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(CFXButton),
                cv.Optional(CONF_BUTTON): cv.use_id(binary_sensor.BinarySensor),
                cv.Optional(CONF_DIMMER): DIMMER_SCHEMA,
                cv.Optional(CONF_CCT_SWEEPER): CCT_SWEEPER_SCHEMA,
                cv.Optional(CONF_HUE_CYCLER): HUE_CYCLER_SCHEMA,
                cv.Optional(CONF_EFFECT_SELECTOR): EFFECT_SELECTOR_SCHEMA,
            }
        ).extend(cv.COMPONENT_SCHEMA),
        cv.has_exactly_one_key(*CONTROLLER_KEYS),
    )
)


def _id_name(value):
    return getattr(value, "id", value)


def _final_validate(config):
    all_lights = full_config.get().get_config_for_path(["light"])
    segmented_lights = {
        _id_name(light_config[CONF_ID]): [
            _id_name(segment[CONF_ID])
            for segment in light_config.get(CONF_SEGMENTS, [])
        ]
        for light_config in all_lights
        if light_config.get("platform") == "cfx_light"
        and light_config.get(CONF_SEGMENTS)
    }

    for button_config in config:
        controller_key = next(
            key for key in CONTROLLER_KEYS if key in button_config
        )
        targets = {
            _id_name(light_id)
            for light_id in button_config[controller_key][CONF_LIGHTS]
        }
        for master_id, segment_ids in segmented_lights.items():
            if master_id not in targets:
                continue
            for segment_id in segment_ids:
                if segment_id in targets:
                    raise cv.Invalid(
                        f"{controller_key} controller cannot target segmented "
                        f"master '{master_id}' and child segment "
                        f"'{segment_id}' together"
                    )

    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def _add_lights(controller, config):
    for light_id in config[CONF_LIGHTS]:
        light_state = await cg.get_variable(light_id)
        cg.add(controller.add_light(light_state))


async def _build_dimmer(config):
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_ID].id)
    await cg.register_component(var, config)
    cg.add(var.set_long_press_ms(config[CONF_LONG_PRESS].total_milliseconds))
    cg.add(var.set_ramp_time_ms(config[CONF_RAMP_TIME].total_milliseconds))
    cg.add(var.set_min_brightness(config[CONF_MIN_BRIGHTNESS]))
    cg.add(var.set_max_brightness(config[CONF_MAX_BRIGHTNESS]))
    await _add_lights(var, config)
    return var


async def _build_cct_sweeper(config):
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_ID].id)
    await cg.register_component(var, config)
    cg.add(var.set_long_press_ms(config[CONF_LONG_PRESS].total_milliseconds))
    cg.add(var.set_sweep_time_ms(config[CONF_SWEEP_TIME].total_milliseconds))
    cg.add(var.set_native_white(*config[CONF_NATIVE_WHITE]))
    cg.add(var.set_preferred_white(*config[CONF_PREFERRED_WHITE]))
    cg.add(var.set_restore(config[CONF_RESTORE]))
    await _add_lights(var, config)
    return var


async def _build_hue_cycler(config):
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_ID].id)
    await cg.register_component(var, config)
    cg.add(var.set_long_press_ms(config[CONF_LONG_PRESS].total_milliseconds))
    if CONF_COLORS in config:
        cg.add(
            var.set_color_interval_ms(
                config[CONF_COLOR_INTERVAL].total_milliseconds
            )
        )
        for color in config[CONF_COLORS]:
            cg.add(var.add_color(*color))
    else:
        cg.add(
            var.set_cycle_time_ms(config[CONF_CYCLE_TIME].total_milliseconds)
        )
        cg.add(var.set_saturation(config[CONF_SATURATION]))
    cg.add(var.set_fixed_color(*config[CONF_FIXED_COLOR]))
    cg.add(var.set_restore(config[CONF_RESTORE]))
    await _add_lights(var, config)
    return var


async def _build_effect_selector(config):
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_ID].id)
    await cg.register_component(var, config)
    cg.add(var.set_long_press_ms(config[CONF_LONG_PRESS].total_milliseconds))
    cg.add(
        var.set_effect_interval_ms(
            config[CONF_EFFECT_INTERVAL].total_milliseconds
        )
    )
    cg.add(var.set_restore(config[CONF_RESTORE]))
    await _add_lights(var, config)
    for effect in config[CONF_EFFECTS]:
        cg.add(var.add_effect(effect))
    return var


CONTROLLER_BUILDERS = {
    CONF_DIMMER: _build_dimmer,
    CONF_CCT_SWEEPER: _build_cct_sweeper,
    CONF_HUE_CYCLER: _build_hue_cycler,
    CONF_EFFECT_SELECTOR: _build_effect_selector,
}


async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)

        if CONF_BUTTON in conf:
            button = await cg.get_variable(conf[CONF_BUTTON])
            cg.add(var.set_button(button))

        controller_key = next(key for key in CONTROLLER_KEYS if key in conf)
        controller = await CONTROLLER_BUILDERS[controller_key](
            conf[controller_key]
        )
        cg.add(var.set_controller(controller))
