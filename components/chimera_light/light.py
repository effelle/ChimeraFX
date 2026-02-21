"""
ChimeraLight - Async DMA LED Output for ESPHome
Copyright (c) 2026 Federico Leoni (effelle)

Drop-in replacement for esp32_rmt_led_strip with:
- DMA always enabled (fire-and-forget)
- Chipset-aware timing (WS2812B, SK6812 strict, WS2811, WS2813)
- Auto-detected mem_block_symbols per ESP32 variant
- Auto RGBW from chipset (SK6812 = 4-byte, WS2812B = 3-byte)
"""

import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome import pins
from esphome.const import (
    CONF_CHIPSET,
    CONF_MAX_REFRESH_RATE,
    CONF_NUM_LEDS,
    CONF_NUMBER,
    CONF_OUTPUT_ID,
    CONF_PIN,
    CONF_RGB_ORDER,
    CONF_RMT_SYMBOLS,
)

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["esp32"]

chimera_light_ns = cg.esphome_ns.namespace("chimera_light")
ChimeraLightOutput = chimera_light_ns.class_(
    "ChimeraLightOutput", light.AddressableLight
)

ChimeraChipset = chimera_light_ns.enum("ChimeraChipset")
RGBOrder = chimera_light_ns.enum("RGBOrder")

# Chipset enum mapping
CHIPSETS = {
    "WS2812B": ChimeraChipset.CHIPSET_WS2812B,
    "SK6812": ChimeraChipset.CHIPSET_SK6812,
    "WS2811": ChimeraChipset.CHIPSET_WS2811,
    "WS2813": ChimeraChipset.CHIPSET_WS2813,
}

# Chipsets that use 4-byte RGBW protocol
RGBW_CHIPSETS = {"SK6812"}

# RGB byte order mapping
RGB_ORDERS = {
    "RGB": RGBOrder.ORDER_RGB,
    "RBG": RGBOrder.ORDER_RBG,
    "GRB": RGBOrder.ORDER_GRB,
    "GBR": RGBOrder.ORDER_GBR,
    "BGR": RGBOrder.ORDER_BGR,
    "BRG": RGBOrder.ORDER_BRG,
}

# Default byte order per chipset (can be overridden in YAML)
DEFAULT_ORDER = {
    "WS2812B": "GRB",
    "SK6812": "GRB",
    "WS2811": "RGB",
    "WS2813": "GRB",
}


def _default_rgb_order(config):
    """Set default RGB order from chipset if not explicitly specified."""
    if CONF_RGB_ORDER not in config:
        chipset = config[CONF_CHIPSET]
        config[CONF_RGB_ORDER] = DEFAULT_ORDER.get(chipset, "GRB")
    return config


CONFIG_SCHEMA = cv.All(
    light.ADDRESSABLE_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(ChimeraLightOutput),
            cv.Required(CONF_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_NUM_LEDS): cv.positive_not_null_int,
            cv.Required(CONF_CHIPSET): cv.one_of(*CHIPSETS, upper=True),
            cv.Optional(CONF_RGB_ORDER): cv.enum(RGB_ORDERS, upper=True),
            cv.Optional(CONF_MAX_REFRESH_RATE): cv.positive_time_period_microseconds,
            cv.Optional(CONF_RMT_SYMBOLS, default=0): cv.uint32_t,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _default_rgb_order,
)


async def to_code(config):
    # Re-enable ESP-IDF's RMT driver (newer ESPHome excludes it by default)
    try:
        from esphome.components.esp32 import include_builtin_idf_component
        include_builtin_idf_component("esp_driver_rmt")
    except ImportError:
        pass  # Older ESPHome: RMT driver included by default

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    await cg.register_component(var, config)

    cg.add(var.set_pin(config[CONF_PIN][CONF_NUMBER]))
    cg.add(var.set_num_leds(config[CONF_NUM_LEDS]))

    chipset_name = config[CONF_CHIPSET]
    cg.add(var.set_chipset(CHIPSETS[chipset_name]))

    # Auto-detect RGBW from chipset
    is_rgbw = chipset_name in RGBW_CHIPSETS
    cg.add(var.set_is_rgbw(is_rgbw))

    cg.add(var.set_rgb_order(config[CONF_RGB_ORDER]))

    # 0 = auto-detect from chip variant in C++
    cg.add(var.set_rmt_symbols(config[CONF_RMT_SYMBOLS]))

    if CONF_MAX_REFRESH_RATE in config:
        cg.add(var.set_max_refresh_rate(config[CONF_MAX_REFRESH_RATE]))
