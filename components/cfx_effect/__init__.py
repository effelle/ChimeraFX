"""
ChimeraFX - WLED Effects for ESPHome
Copyright (c) 2026 Federico Leoni (effelle)
Based on WLED by Aircoookie (https://github.com/wled/WLED)

Licensed under the EUPL-1.2
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.light.types import AddressableLightEffect
from esphome.components.light.effects import register_addressable_effect
from esphome.components import number, select, switch
from esphome.const import CONF_ID, CONF_NAME, CONF_UPDATE_INTERVAL, CONF_EFFECTS
from esphome.core import CORE

DEPENDENCIES = ["light", "number", "select", "switch"]
CODEOWNERS = ["@effelle"]

# Define the namespace and class
chimera_fx_ns = cg.esphome_ns.namespace("chimera_fx")
CFXAddressableLightEffect = chimera_fx_ns.class_(
    "CFXAddressableLightEffect", AddressableLightEffect
)

from . import cfx_control

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional("cfx_control"): cv.ensure_list(cfx_control.CONFIG_SCHEMA),
    }
)

def to_code(config):
    if "cfx_control" in config:
        for conf in config["cfx_control"]:
            yield cfx_control.to_code(conf)

# Configuration keys
CONF_EFFECT_ID = "effect_id"
CONF_SPEED = "speed"
CONF_INTENSITY = "intensity"
CONF_PALETTE = "palette"
CONF_MIRROR = "mirror"

# Per-effect preset keys
CONF_SET_SPEED = "set_speed"
CONF_SET_INTENSITY = "set_intensity"
CONF_SET_PALETTE = "set_palette"
CONF_SET_MIRROR = "set_mirror"

# Intro Configuration
CONF_INTRO_EFFECT = "intro_effect"
CONF_INTRO_DURATION = "intro_duration"

# Map of Effect IDs to Names
CFX_EFFECT_NAMES = {
    0: "Solid", 1: "Blink", 2: "Breathe", 3: "Wipe", 4: "Wipe Random", 5: "Random Colors", 6: "Sweep", 7: "Dynamic", 8: "Colorloop", 9: "Rainbow",
    10: "Scan", 11: "Scan Dual", 12: "Fade", 13: "Theater", 14: "Theater Rainbow", 15: "Running", 16: "Saw", 17: "Twinkle", 18: "Dissolve", 19: "Dissolve Rnd",
    20: "Sparkle", 21: "Sparkle Dark", 22: "Sparkle+", 23: "Strobe", 24: "Strobe Rainbow", 25: "Strobe Mega", 26: "Blink Rainbow", 27: "Android", 28: "Chase", 29: "Chase Random",
    30: "Chase Rainbow", 31: "Chase Flash", 32: "Chase Flash Rnd", 33: "Chase Rainbow White", 34: "Colorful", 35: "Traffic Light", 36: "Sweep Random", 37: "Running 2", 38: "Aurora", 39: "Stream",
    40: "Scanner", 41: "Lighthouse", 42: "Fireworks", 43: "Rain", 44: "Tetris", 45: "Fire Flicker", 46: "Gradient", 47: "Loading", 48: "Rolling Balls", 49: "Fairy",
    50: "Two Dots", 51: "Fairy Twinkle", 52: "Running Dual", 54: "Tri Chase", 55: "Tri Wipe", 56: "Tri Fade", 57: "Lightning", 58: "ICU", 59: "Multi Comet",
    60: "Scanner Dual", 61: "Stream 2", 62: "Oscillate", 63: "Pride 2015", 64: "Juggle", 65: "Palette", 66: "Fire 2012", 67: "Colorwaves", 68: "BPM", 69: "Fill Noise",
    70: "Noise 1", 71: "Noise 2", 72: "Noise 3", 73: "Noise 4", 74: "Colortwinkle", 75: "Lake", 76: "Meteor", 77: "Meteor Smooth", 78: "Railway", 79: "Ripple",
    80: "Twinklefox", 81: "Twinklecat", 82: "Halloween Eyes", 83: "Solid Pattern", 84: "Solid Pattern Tri", 85: "Spots", 86: "Spots Fade", 87: "Glitter", 88: "Candle", 89: "Starburst",
    90: "Fireworks Starburst", 91: "Bouncing Balls", 92: "Sinelon", 93: "Sinelon Dual", 94: "Sinelon Rainbow", 95: "Popcorn", 96: "Drip", 97: "Plasma", 98: "Percent", 99: "Ripple Rainbow",
    100: "Heartbeat", 101: "Ocean", 102: "Candle Multi", 103: "Solid Glitter", 104: "Sunrise", 105: "Phased", 106: "Twinkleup", 107: "Noise Pal", 108: "Sine", 109: "Phased Noise",
    110: "Flow", 111: "Chunchun", 112: "Dancing Shadows", 113: "Washing Machine"
}

@register_addressable_effect(
    "addressable_cfx",
    CFXAddressableLightEffect,
    "CFX Effect",
    {
        cv.Required(CONF_EFFECT_ID): cv.int_range(0, 255),
        cv.Optional(CONF_SPEED): cv.use_id(number.Number),
        cv.Optional(CONF_INTENSITY): cv.use_id(number.Number),
        cv.Optional(CONF_PALETTE): cv.use_id(select.Select),
        cv.Optional(CONF_MIRROR): cv.use_id(switch.Switch),
        cv.Optional(CONF_UPDATE_INTERVAL, default="16ms"): cv.update_interval,
        cv.Optional(CONF_INTRO_EFFECT): cv.use_id(select.Select),
        cv.Optional(CONF_INTRO_DURATION): cv.use_id(number.Number),
        cv.Optional(CONF_SET_SPEED): cv.int_range(0, 255),
        cv.Optional(CONF_SET_INTENSITY): cv.int_range(0, 255),
        cv.Optional(CONF_SET_PALETTE): cv.int_range(0, 255),
        cv.Optional(CONF_SET_MIRROR): cv.boolean,
    },
)
async def cfx_effect_to_code(config, effect_id):
    """Generate code for addressable_cfx effect."""
    name = config.get(CONF_NAME) 
    eid = config[CONF_EFFECT_ID]
    if name == "CFX Effect":
        if eid in CFX_EFFECT_NAMES:
            name = CFX_EFFECT_NAMES[eid]
    
    effect = cg.new_Pvariable(effect_id, name)
    cg.add(effect.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    cg.add(effect.set_effect_id(config[CONF_EFFECT_ID]))
    
    if CONF_SPEED in config:
        speed_var = await cg.get_variable(config[CONF_SPEED])
        cg.add(effect.set_speed(speed_var))
    
    if CONF_INTENSITY in config:
        intensity_var = await cg.get_variable(config[CONF_INTENSITY])
        cg.add(effect.set_intensity(intensity_var))
    
    if CONF_PALETTE in config:
        palette_var = await cg.get_variable(config[CONF_PALETTE])
        cg.add(effect.set_palette(palette_var))
    
    if CONF_MIRROR in config:
        mirror_var = await cg.get_variable(config[CONF_MIRROR])
        cg.add(effect.set_mirror(mirror_var))
        
    if CONF_INTRO_EFFECT in config:
        intro_effect = await cg.get_variable(config[CONF_INTRO_EFFECT])
        cg.add(effect.set_intro_effect(intro_effect))
        
    if CONF_INTRO_DURATION in config:
        intro_duration = await cg.get_variable(config[CONF_INTRO_DURATION])
        cg.add(effect.set_intro_duration(intro_duration))
    
    if CONF_SET_SPEED in config:
        cg.add(effect.set_speed_preset(config[CONF_SET_SPEED]))
    if CONF_SET_INTENSITY in config:
        cg.add(effect.set_intensity_preset(config[CONF_SET_INTENSITY]))
    if CONF_SET_PALETTE in config:
        cg.add(effect.set_palette_preset(config[CONF_SET_PALETTE]))
    if CONF_SET_MIRROR in config:
        cg.add(effect.set_mirror_preset(config[CONF_SET_MIRROR]))

    return effect
