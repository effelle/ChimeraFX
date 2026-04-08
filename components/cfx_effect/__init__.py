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
from esphome.components import number, select, switch, light
from esphome.const import CONF_ID, CONF_NAME, CONF_UPDATE_INTERVAL, CONF_EFFECTS, CONF_TRIGGER_ID
from esphome.core import CORE
from esphome import automation

DEPENDENCIES = ["light"]
AUTO_LOAD = ["number", "select", "switch", "event"]
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
    cg.add_define("USE_CFX_EVENTS")
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
CONF_SET_INTRO = "set_intro"
CONF_SET_INOUT_DURATION = "set_inout_dur"
CONF_SET_OUTRO = "set_outro"
CONF_SET_FORCE_WHITE = "set_force_white"


# Intro Configuration
CONF_INTRO_EFFECT = "intro_effect"
CONF_INOUT_DURATION = "inout_duration"

# Outro Configuration
CONF_OUTRO_EFFECT = "outro_effect"

# Sequencer Triggers
CONF_ON_START = "on_start"
CONF_ON_BEGIN    = "on_begin"
CONF_ON_STOP     = "on_stop"
CONF_ON_COMPLETE = "on_complete"
CONF_ON_REACH = "on_reach"

CONF_POSITION = "position"

# Trigger Classes
CfxOnStartTrigger = chimera_fx_ns.class_("CfxOnStartTrigger", automation.Trigger.template())
CfxOnBeginTrigger    = chimera_fx_ns.class_("CfxOnBeginTrigger",    automation.Trigger.template())
CfxOnStopTrigger     = chimera_fx_ns.class_("CfxOnStopTrigger",     automation.Trigger.template())
CfxOnCompleteTrigger = chimera_fx_ns.class_("CfxOnCompleteTrigger", automation.Trigger.template())
CfxOnReachTrigger = chimera_fx_ns.class_("CfxOnReachTrigger", automation.Trigger.template(cg.float_))

# Map of Effect IDs to Names
CFX_EFFECT_NAMES = {
    0: "Solid", 1: "Blink", 2: "Breathe", 3: "Wipe", 4: "Wipe Random", 5: "Random Colors", 6: "Sweep", 7: "Dynamic", 8: "Colorloop", 9: "Rainbow",
    10: "Scan", 11: "Scan Dual", 12: "Fade", 13: "Theater", 14: "Theater Rainbow", 15: "Running", 16: "Saw", 17: "Twinkle", 18: "Dissolve", 19: "Dissolve Rnd",
    20: "Sparkle", 21: "Sparkle Dark", 22: "Sparkle+", 23: "Strobe", 24: "Strobe Rainbow", 25: "Strobe Mega", 26: "Blink Rainbow", 27: "Android", 28: "Chase", 29: "Chase Random",
    30: "Chase Rainbow", 31: "Chase Flash", 32: "Chase Flash Rnd", 33: "Chase Rainbow White", 34: "Colorful", 35: "Traffic Light", 36: "Sweep Random", 37: "Running 2", 38: "Aurora", 39: "Stream",
    40: "Scanner", 41: "Lighthouse", 42: "Fireworks", 43: "Rain", 44: "Tetris", 45: "Fire Flicker", 46: "Gradient", 47: "Loading", 48: "Rolling Balls", 49: "Fairy",
    150: "BPM", 151: "Dropping Time", 152: "Percent Center", 153: "Fire Dual", 154: "HeartBeat Center", 54: "Tri Chase", 55: "Tri Wipe", 56: "Tri Fade", 57: "Lightning", 58: "ICU", 59: "Multi Comet",
    60: "Scanner Dual", 61: "Stream 2", 62: "Oscillate", 63: "Colorwaves", 64: "Juggle", 65: "Palette", 66: "Fire 2012", 67: "Colorwaves Alt", 68: "BPM", 69: "Fill Noise",
    70: "Noise 1", 71: "Noise 2", 72: "Noise 3", 73: "Noise 4", 74: "Colortwinkles", 75: "Lake", 76: "Meteor", 77: "Meteor Smooth", 78: "Railway", 79: "Ripple",
    80: "Twinklefox", 81: "Twinklecat", 82: "Halloween Eyes", 83: "Solid Pattern", 84: "Solid Pattern Tri", 85: "Spots", 86: "Spots Fade", 87: "Glitter", 88: "Candle", 89: "Starburst",
    90: "Fireworks Starburst", 91: "Bouncing Balls", 92: "Sinelon", 93: "Sinelon Dual", 94: "Sinelon Rainbow", 95: "Popcorn", 96: "Drip", 97: "Plasma", 98: "Percent", 99: "Ripple Rainbow",
    100: "Heartbeat", 101: "Ocean", 102: "Candle Multi", 103: "Solid Glitter", 104: "Sunrise", 105: "Phased", 106: "Twinkleup", 107: "Noise Pal", 108: "Sine", 109: "Phased Noise",
    110: "Flow", 111: "Chunchun", 112: "Dancing Shadows", 113: "Washing Machine",
    155: "Kaleidos",
    156: "Follow Me",
    157: "Follow Us",
    158: "Energy",
    159: "Chaos Theory",
    160: "Fluid Rain",
    161: "Horizon Sweep",
    170: "Assembly",
    171: "Inertia Sweep",
    172: "Sonar Reveal",
    173: "Venetian",
    174: "Crystallize",
    175: "Deep Breathe",
    176: "Moiré Shift",
    177: "Resonance Fill",
    178: "Telemetry",
    179: "Stellar Dust",
    180: "Interference",
    181: "Eclipse",
    182: "Gas Discharge",
    183: "Harmonic Settle",
    184: "Lithograph",
    186: "Tidal Surge",
    187: "Impact Flare",
    185: "--- separator ---",
    255: "Ambient Roulette"
}
# ── CFX_EFFECTS — canonical ordered effect registry ───────────────────────────
# Source of truth for effect presentation order, categories, and names.
# Used by _inject_all_effects() in light.py to populate the effects list
# without depending on an external yaml file.
# Each tuple: (category, effect_id, display_name)
# category == "sep" → separator entry (effect_id 185 = clean off).
CFX_EFFECTS = [
    # ── Architectural ─────────────────────────────────────────────────────
    ("sep",            185, "--- Architectural ---"),
    ("architectural",  255, "Ambient Roulette"),
    ("architectural",  170, "Assembly"),
    ("architectural",  164, "Collider"),
    ("architectural",  174, "Crystallize"),
    ("architectural",  162, "Curtain Sweep"),
    ("architectural",  175, "Deep Breathe"),
    ("architectural",  169, "Dropping Fill"),
    ("architectural",  181, "Eclipse"),
    ("architectural",  167, "Four Times the Charm"),
    ("architectural",  182, "Gas Discharge"),
    ("architectural",  183, "Harmonic Settle"),
    ("architectural",  161, "Horizon Sweep"),
    ("architectural",  168, "Hydro-Pulse"),
    ("architectural",  171, "Inertia Sweep"),
    ("architectural",  180, "Interference"),
    ("architectural",  184, "Lithograph"),
    ("architectural",  176, "Moiré Shift"),
    ("architectural",  177, "Resonance Fill"),
    ("architectural",  172, "Sonar Reveal"),
    ("architectural",  163, "Stardust Sweep"),
    ("architectural",  179, "Stellar Dust"),
    ("architectural",  178, "Telemetry"),
    ("architectural",  186, "Tidal Surge"),
    ("architectural",  187, "Impact Flare"),
    ("architectural",  166, "Transmission"),
    ("architectural",  165, "Twin Pulse Sweep"),
    ("architectural",  173, "Venetian"),
    # ── ChimeraFX Originals ───────────────────────────────────────────────
    ("sep",            185, "--- ChimeraFX Originals ---"),
    ("originals",      152, "Center Gauge"),
    ("originals",      159, "Chaos Theory"),
    ("originals",      151, "Dropping Time"),
    ("originals",      158, "Energy"),
    ("originals",      160, "Fluid Rain"),
    ("originals",      156, "Follow Me"),
    ("originals",      157, "Follow Us"),
    ("originals",      155, "Kaleidos"),
    ("originals",      154, "Reactor Beat"),
    ("originals",      153, "Twin Flames"),
    # ── Classics ──────────────────────────────────────────────────────────
    ("sep",            185, "--- Classics ---"),
    ("classics",        38, "Aurora"),
    ("classics",         1, "Blink"),
    ("classics",        26, "Blink Rainbow"),
    ("classics",        91, "Bouncing Balls"),
    ("classics",         2, "Breathe"),
    ("classics",        28, "Chase"),
    ("classics",        54, "Chase multi"),
    ("classics",         8, "Colorloop"),
    ("classics",        74, "Colortwinkle"),
    ("classics",        63, "Colorwaves"),
    ("classics",        18, "Dissolve"),
    ("classics",        96, "Drip"),
    ("classics",        66, "Fire"),
    ("classics",        90, "Fireworks"),
    ("classics",       110, "Flow"),
    ("classics",        87, "Glitter"),
    ("classics",       100, "HeartBeat"),
    ("classics",        64, "Juggle"),
    ("classics",        76, "Meteor"),
    ("classics",        25, "Multi Strobe"),
    ("classics",       107, "Noise Pal"),
    ("classics",       101, "Ocean"),
    ("classics",        98, "Percent"),
    ("classics",        97, "Plasma"),
    ("classics",        95, "Popcorn"),
    ("classics",         9, "Rainbow"),
    ("classics",        79, "Ripple"),
    ("classics",        52, "Running Dual"),
    ("classics",        15, "Running lights"),
    ("classics",        16, "Saw"),
    ("classics",        40, "Scanner"),
    ("classics",        60, "Scanner Dual"),
    ("classics",        20, "Sparkle"),
    ("classics",        22, "Sparkle +"),
    ("classics",        21, "Sparkle Dark"),
    ("classics",         0, "Static"),
    ("classics",        23, "Strobe"),
    ("classics",        24, "Strobe Rainbow"),
    ("classics",       104, "Sunrise"),
    ("classics",         6, "Sweep"),
    ("classics",         3, "Wipe"),
    ("classics",         4, "Wipe Random"),
]



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
        cv.Optional(CONF_INOUT_DURATION): cv.use_id(number.Number),
        cv.Optional(CONF_OUTRO_EFFECT): cv.use_id(select.Select),
        cv.Optional(CONF_SET_SPEED): cv.int_range(0, 255),
        cv.Optional(CONF_SET_INTENSITY): cv.int_range(0, 255),
        cv.Optional(CONF_SET_PALETTE): cv.int_range(0, 255),
        cv.Optional(CONF_SET_MIRROR): cv.boolean,
        cv.Optional(CONF_SET_INTRO): cv.int_range(min=0, max=27),  # CFX-024: IntroMode enum now has 28 entries (0-27)
        cv.Optional(CONF_SET_INOUT_DURATION): cv.float_range(min=0.0),
        cv.Optional(CONF_SET_OUTRO): cv.int_range(min=0, max=27),  # CFX-024: IntroMode enum now has 28 entries (0-27)
        cv.Optional(CONF_SET_FORCE_WHITE): cv.boolean,
        cv.Optional(CONF_ON_START): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxOnStartTrigger),
            }
        ),
        cv.Optional(CONF_ON_BEGIN): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxOnBeginTrigger),
            }
        ),
        cv.Optional(CONF_ON_STOP): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxOnStopTrigger),
            }
        ),
        cv.Optional(CONF_ON_COMPLETE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxOnCompleteTrigger),
            }
        ),
        cv.Optional(CONF_ON_REACH): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CfxOnReachTrigger),
                cv.Required(CONF_POSITION): cv.percentage,
            }
        ),
    },
)
async def cfx_effect_to_code(config, effect_id, is_virtual_segment=False):
    """Generate code for addressable_cfx effect."""
    name = config.get(CONF_NAME) 
    eid = config[CONF_EFFECT_ID]
    if name == "CFX Effect":
        if eid in CFX_EFFECT_NAMES:
            name = CFX_EFFECT_NAMES[eid]
    
    effect = cg.new_Pvariable(effect_id, name)
    cg.add(effect.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    cg.add(effect.set_effect_id(config[CONF_EFFECT_ID]))
    
    if is_virtual_segment:
        cg.add(effect.set_virtual_segment(True))
        
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
        
    if CONF_INOUT_DURATION in config:
        inout_duration = await cg.get_variable(config[CONF_INOUT_DURATION])
        cg.add(effect.set_inout_duration(inout_duration))

    if CONF_OUTRO_EFFECT in config:
        outro_effect = await cg.get_variable(config[CONF_OUTRO_EFFECT])
        cg.add(effect.set_outro_effect(outro_effect))
        
    
    if CONF_SET_SPEED in config:
        cg.add(effect.set_speed_preset(config[CONF_SET_SPEED]))
    if CONF_SET_INTENSITY in config:
        cg.add(effect.set_intensity_preset(config[CONF_SET_INTENSITY]))
    if CONF_SET_PALETTE in config:
        cg.add(effect.set_palette_preset(config[CONF_SET_PALETTE]))
    if CONF_SET_MIRROR in config:
        cg.add(effect.set_mirror_preset(config[CONF_SET_MIRROR]))
    if CONF_SET_INTRO in config:
        cg.add(effect.set_intro_preset(config[CONF_SET_INTRO]))
    if CONF_SET_INOUT_DURATION in config:
        cg.add(effect.set_inout_duration_preset(config[CONF_SET_INOUT_DURATION]))
    if CONF_SET_OUTRO in config:
        cg.add(effect.set_outro_preset(config[CONF_SET_OUTRO]))
    if CONF_SET_FORCE_WHITE in config:
        cg.add(effect.set_force_white_preset(config[CONF_SET_FORCE_WHITE]))

    # Setup Triggers
    for conf in config.get(CONF_ON_START, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(effect.add_on_start_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_BEGIN, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(effect.add_on_begin_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_STOP, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(effect.add_on_stop_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_COMPLETE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(effect.add_on_complete_trigger(trigger))
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_REACH, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], conf[CONF_POSITION])
        cg.add(effect.add_on_reach_trigger(trigger))
        await automation.build_automation(trigger, [(cg.float_, "position")], conf)


    return effect

# Play Effect Action
PlayEffectAction = chimera_fx_ns.class_("PlayEffectAction", automation.Action)

@automation.register_action(
    "cfx.play_effect",
    PlayEffectAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(light.LightState),
            cv.Optional("effect"): cv.templatable(cv.string),
            cv.Optional("speed"): cv.templatable(cv.int_range(0, 255)),
            cv.Optional("intensity"): cv.templatable(cv.int_range(0, 255)),
            cv.Optional("palette"): cv.templatable(cv.int_range(0, 255)),
            cv.Optional("mirror"): cv.templatable(cv.boolean),
        }
    ),
    synchronous=True,
)
async def cfx_play_effect_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    
    if "effect" in config:
        cg.add(var.set_effect(await cg.templatable(config["effect"], args, cg.std_string)))
    if "speed" in config:
        cg.add(var.set_speed(await cg.templatable(config["speed"], args, cg.uint8)))
    if "intensity" in config:
        cg.add(var.set_intensity(await cg.templatable(config["intensity"], args, cg.uint8)))
    if "palette" in config:
        cg.add(var.set_palette(await cg.templatable(config["palette"], args, cg.uint8)))
    if "mirror" in config:
        cg.add(var.set_mirror(await cg.templatable(config["mirror"], args, cg.bool_)))
        
    return var
