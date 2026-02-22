"""
ChimeraFX - WLED Effects for ESPHome
Copyright (c) 2026 Federico Leoni (effelle)
Based on WLED by Aircoookie (https://github.com/wled/WLED)

Licensed under the EUPL-1.2
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number, select, switch, light
from esphome.const import (
    CONF_ID, CONF_NAME, CONF_ICON, CONF_MODE,
    CONF_DISABLED_BY_DEFAULT, CONF_INTERNAL,
    CONF_RESTORE_MODE, CONF_ENTITY_CATEGORY,
    ENTITY_CATEGORY_DIAGNOSTIC
)
from . import chimera_fx_ns

DEPENDENCIES = ["light", "number", "select", "switch"]

CFXControl = chimera_fx_ns.class_("CFXControl", cg.Component)
CFXNumber = chimera_fx_ns.class_("CFXNumber", number.Number)
CFXSelect = chimera_fx_ns.class_("CFXSelect", select.Select)
CFXSwitch = chimera_fx_ns.class_("CFXSwitch", switch.Switch)

PALETTE_OPTIONS = [
    "Default", "Aurora", "Forest", "Halloween", "Rainbow", "Fire", "Sunset", "Ice", "Party", 
    "Pastel", "Ocean", "HeatColors", "Sakura", "Rivendell", "Cyberpunk", 
    "OrangeTeal", "Christmas", "RedBlue", "Matrix", "SunnyGold", "Solid", "Fairy", "Twilight", "Smart Random"
]

INTRO_OPTIONS = ["None", "Wipe", "Fade", "Center", "Glitter"]

CONF_LIGHT = "light_id"
CONF_EXCLUDE = "exclude"

EXCLUDE_SPEED = 1
EXCLUDE_INTENSITY = 2
EXCLUDE_PALETTE = 3
EXCLUDE_MIRROR = 4
EXCLUDE_INTRO = 5
EXCLUDE_TIMER = 6
EXCLUDE_AUTOTUNE = 7
EXCLUDE_DEBUG = 9

CONF_DEFAULTS = "defaults"
CONF_DEFAULT_SPEED = "speed"
CONF_DEFAULT_INTENSITY = "intensity"
CONF_DEFAULT_PALETTE = "palette"
CONF_DEFAULT_MIRROR = "mirror"
CONF_DEFAULT_INTRO = "intro_effect"
CONF_DEFAULT_INTRO_DURATION = "intro_duration"
CONF_DEFAULT_INTRO_USE_PALETTE = "intro_use_palette"
CONF_DEFAULT_TIMER = "timer"
CONF_DEFAULT_AUTOTUNE = "autotune"

DEFAULTS_SCHEMA = cv.Schema({
    cv.Optional(CONF_DEFAULT_SPEED): cv.int_range(min=0, max=255),
    cv.Optional(CONF_DEFAULT_INTENSITY): cv.int_range(min=0, max=255),
    cv.Optional(CONF_DEFAULT_PALETTE): cv.string,
    cv.Optional(CONF_DEFAULT_MIRROR): cv.boolean,
    cv.Optional(CONF_DEFAULT_INTRO): cv.string,
    cv.Optional(CONF_DEFAULT_INTRO_DURATION): cv.float_range(min=0.5, max=10.0),
    cv.Optional(CONF_DEFAULT_INTRO_USE_PALETTE): cv.boolean,
    cv.Optional(CONF_DEFAULT_TIMER): cv.int_range(min=0, max=360),
    cv.Optional(CONF_DEFAULT_AUTOTUNE): cv.boolean,
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CFXControl),
    cv.Required(CONF_NAME): cv.string,
    cv.Required(CONF_LIGHT): cv.ensure_list(cv.use_id(light.LightState)),
    cv.Optional(CONF_EXCLUDE, default=[]): cv.ensure_list(cv.int_range(min=1, max=9)),
    cv.Optional(CONF_DEFAULTS): DEFAULTS_SCHEMA,
})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    for light_id in config[CONF_LIGHT]:
        light_state = await cg.get_variable(light_id)
        cg.add(var.add_light(light_state))
    
    name = config[CONF_NAME]
    exclude = [int(x) for x in config[CONF_EXCLUDE]]
    
    def is_included(id):
        return id not in exclude

    defaults = config.get(CONF_DEFAULTS, {})

    # 1. Speed
    if is_included(EXCLUDE_SPEED):
        speed_init = defaults.get(CONF_DEFAULT_SPEED, 128)
        conf = {
            CONF_ID: cv.declare_id(CFXNumber)(f"{config[CONF_ID]}_speed"),
            CONF_NAME: f"{name} Speed",
            CONF_ICON: "mdi:speedometer",
            "min_value": 0, "max_value": 255, "step": 1, "initial_value": speed_init,
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
            CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
        }
        speed = cg.new_Pvariable(conf[CONF_ID])
        await number.register_number(speed, conf, min_value=0, max_value=255, step=1)
        cg.add(speed.publish_state(speed_init))
        cg.add(var.set_speed(speed))

    # 2. Intensity
    if is_included(EXCLUDE_INTENSITY):
        intensity_init = defaults.get(CONF_DEFAULT_INTENSITY, 128)
        conf = {
            CONF_ID: cv.declare_id(CFXNumber)(f"{config[CONF_ID]}_intensity"),
            CONF_NAME: f"{name} Intensity",
            CONF_ICON: "mdi:brightness-6",
            "min_value": 0, "max_value": 255, "step": 1, "initial_value": intensity_init,
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
            CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
        }
        intensity = cg.new_Pvariable(conf[CONF_ID])
        await number.register_number(intensity, conf, min_value=0, max_value=255, step=1)
        cg.add(intensity.publish_state(intensity_init))
        cg.add(var.set_intensity(intensity))

    # 3. Palette
    if is_included(EXCLUDE_PALETTE):
        palette_init = defaults.get(CONF_DEFAULT_PALETTE, "Default")
        conf = {
            CONF_ID: cv.declare_id(CFXSelect)(f"{config[CONF_ID]}_palette"),
            CONF_NAME: f"{name} Palette",
            CONF_ICON: "mdi:palette",
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
        }
        palette = cg.new_Pvariable(conf[CONF_ID])
        await select.register_select(palette, conf, options=PALETTE_OPTIONS)
        cg.add(palette.publish_state(palette_init))
        cg.add(var.set_palette(palette))

    # 4. Mirror
    if is_included(EXCLUDE_MIRROR):
        mirror_init = defaults.get(CONF_DEFAULT_MIRROR, False)
        conf = {
            CONF_ID: cv.declare_id(CFXSwitch)(f"{config[CONF_ID]}_mirror"),
            CONF_NAME: f"{name} Mirror",
            CONF_ICON: "mdi:swap-horizontal",
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
            CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF" if not mirror_init else "switch_::SWITCH_RESTORE_DEFAULT_ON"),
        }
        mirror = cg.new_Pvariable(conf[CONF_ID])
        await switch.register_switch(mirror, conf)
        cg.add(var.set_mirror(mirror))
        cg.add(mirror.publish_state(mirror_init))

    # 5. Intro Effect
    if is_included(EXCLUDE_INTRO):
        intro_init = defaults.get(CONF_DEFAULT_INTRO, "None")
        conf = {
            CONF_ID: cv.declare_id(CFXSelect)(f"{config[CONF_ID]}_intro"),
            CONF_NAME: f"{name} Intro",
            CONF_ICON: "mdi:animation-play",
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
        }
        intro = cg.new_Pvariable(conf[CONF_ID])
        await select.register_select(intro, conf, options=INTRO_OPTIONS)
        cg.add(intro.publish_state(intro_init))
        cg.add(var.set_intro_effect(intro))

    # 6. Intro Duration
    if is_included(EXCLUDE_INTRO):
        intro_dur_init = defaults.get(CONF_DEFAULT_INTRO_DURATION, 1.0)
        conf = {
            CONF_ID: cv.declare_id(CFXNumber)(f"{config[CONF_ID]}_intro_dur"),
            CONF_NAME: f"{name} Intro Duration",
            CONF_ICON: "mdi:timer-outline",
            "min_value": 0.5, "max_value": 10.0, "step": 0.1, "initial_value": intro_dur_init,
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
            CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
        }
        intro_dur = cg.new_Pvariable(conf[CONF_ID])
        await number.register_number(intro_dur, conf, min_value=0.5, max_value=10.0, step=0.1)
        cg.add(intro_dur.publish_state(intro_dur_init))
        cg.add(var.set_intro_duration(intro_dur))

    # 7. Intro Use Palette
    if is_included(EXCLUDE_INTRO):
        intro_pal_init = defaults.get(CONF_DEFAULT_INTRO_USE_PALETTE, False)
        conf = {
            CONF_ID: cv.declare_id(CFXSwitch)(f"{config[CONF_ID]}_intro_pal"),
            CONF_NAME: f"{name} Intro Use Palette",
            CONF_ICON: "mdi:palette-swatch-variant",
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
            CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF" if not intro_pal_init else "switch_::SWITCH_RESTORE_DEFAULT_ON"),
        }
        intro_pal = cg.new_Pvariable(conf[CONF_ID])
        await switch.register_switch(intro_pal, conf)
        cg.add(intro_pal.publish_state(intro_pal_init))
        cg.add(var.set_intro_use_palette(intro_pal))

    # 8. Timer
    if is_included(EXCLUDE_TIMER):
        timer_init = defaults.get(CONF_DEFAULT_TIMER, 0)
        conf = {
            CONF_ID: cv.declare_id(CFXNumber)(f"{config[CONF_ID]}_timer"),
            CONF_NAME: f"{name} Timer (min)",
            CONF_ICON: "mdi:timer-sand",
            "min_value": 0, "max_value": 360, "step": 1, "initial_value": timer_init,
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
            CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
        }
        timer = cg.new_Pvariable(conf[CONF_ID])
        await number.register_number(timer, conf, min_value=0, max_value=360, step=1)
        cg.add(timer.publish_state(timer_init))
        cg.add(var.set_timer(timer))

    # 10. Autotune
    if is_included(EXCLUDE_AUTOTUNE):
        autotune_init = defaults.get(CONF_DEFAULT_AUTOTUNE, True)
        conf = {
            CONF_ID: cv.declare_id(CFXSwitch)(f"{config[CONF_ID]}_autotune"),
            CONF_NAME: f"{name} Autotune",
            CONF_ICON: "mdi:auto-fix",
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
            CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF" if not autotune_init else "switch_::SWITCH_RESTORE_DEFAULT_ON"),
        }
        autotune = cg.new_Pvariable(conf[CONF_ID])
        await switch.register_switch(autotune, conf)
        cg.add(autotune.write_state(autotune_init))
        cg.add(var.set_autotune(autotune))

    # 11. Debug
    if is_included(EXCLUDE_DEBUG):
        conf = {
            CONF_ID: cv.declare_id(CFXSwitch)(f"{config[CONF_ID]}_debug"),
            CONF_NAME: f"{name} Debug",
            CONF_ICON: "mdi:bug",
            "optimistic": True,
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
            CONF_ENTITY_CATEGORY: cg.RawExpression("esphome::ENTITY_CATEGORY_DIAGNOSTIC"),
            CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF"),
        }
        debug = cg.new_Pvariable(conf[CONF_ID])
        await switch.register_switch(debug, conf)
        cg.add(debug.publish_state(False))
        cg.add(var.set_debug(debug))
