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

INTRO_OPTIONS = ["None", "Wipe", "Fade", "Center", "Glitter", "Twin Pulse", "Morse Code"]

CONF_LIGHT = "light_id"
CONF_EXCLUDE = "exclude"

EXCLUDE_SPEED = 1
EXCLUDE_INTENSITY = 2
EXCLUDE_PALETTE = 3
EXCLUDE_MIRROR = 4
EXCLUDE_INTRO = 5
EXCLUDE_TIMER = 6
EXCLUDE_AUTOTUNE = 7
EXCLUDE_FORCE_WHITE = 8
EXCLUDE_DEBUG = 9
EXCLUDE_OUTRO = 10

CONF_DEFAULTS = "defaults" # Kept for backwards compatibility but ignored
CONF_DEFAULT_SPEED = "speed"
CONF_DEFAULT_INTENSITY = "intensity"
CONF_DEFAULT_PALETTE = "palette"
CONF_DEFAULT_MIRROR = "mirror"
CONF_DEFAULT_INTRO = "intro_effect"
CONF_DEFAULT_INTRO_DURATION = "intro_duration"
CONF_DEFAULT_INTRO_USE_PALETTE = "intro_use_palette"
CONF_DEFAULT_TIMER = "timer"
CONF_DEFAULT_AUTOTUNE = "autotune"

CONF_DEFAULT_OUTRO = "outro_effect"
CONF_DEFAULT_OUTRO_DURATION = "outro_duration"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CFXControl),
    cv.Required(CONF_NAME): cv.string,
    cv.Required(CONF_LIGHT): cv.ensure_list(cv.use_id(light.LightState)),
    cv.Optional(CONF_EXCLUDE, default=[]): cv.ensure_list(cv.int_range(min=1, max=10)),
    cv.Optional(CONF_DEFAULTS): cv.Any(dict, None), # Accept but ignore
})

async def to_code(config):
    import esphome.core as core
    
    all_targets = []
    light_ids = [l.id for l in config[CONF_LIGHT]]
    
    # 1. Gather all Master lights and their Virtual Segments
    if "light" in core.CORE.config:
        for lconf in core.CORE.config["light"]:
            lconf_id = lconf.get(CONF_ID)
            if lconf_id and lconf_id.id in light_ids:
                has_white = lconf.get("is_rgbw", False) or lconf.get("is_wrgb", False) or lconf.get("chipset") == "SK6812"
                master_name = str(lconf.get(CONF_NAME, "Light"))
                master_state = await cg.get_variable(lconf_id)
                
                # Add master
                all_targets.append({
                    "state": master_state,
                    "name": master_name,
                    "id": lconf_id.id,
                    "has_white": has_white
                })
                
                # Add segments
                if hasattr(lconf, "get") and lconf.get("segments"):
                    for seg in lconf["segments"]:
                        seg_light_id = seg.get("light_id")
                        seg_name = str(seg.get(CONF_NAME, f"{master_name} Segment"))
                        if seg_light_id:
                            seg_state = await cg.get_variable(seg_light_id)
                            all_targets.append({
                                "state": seg_state,
                                "name": seg_name,
                                "id": seg_light_id.id,
                                "has_white": has_white
                            })
                            
    exclude = [int(x) for x in config[CONF_EXCLUDE]]
    def is_included(id):
        return id not in exclude

    for idx, target in enumerate(all_targets):
        if idx == 0:
            var_id = config[CONF_ID]
        else:
            var_id = core.ID(f"{config[CONF_ID].id}_{idx}", is_declaration=True, type=CFXControl)
            
        var = cg.new_Pvariable(var_id)
        # For dynamically generated sibling instances (segments, idx > 0) the ID
        # was never processed by ESPHome's YAML validator, so it is absent from
        # CORE.component_ids. register_component checks that set and raises if
        # the ID is missing.  Pre-populate it to replicate what the validator
        # does for statically declared Component-inheriting IDs.
        if idx > 0:
            import esphome.core as _core
            _core.CORE.component_ids.add(str(var_id))
        await cg.register_component(var, config if idx == 0 else {})
        cg.add(var.set_light(target["state"]))
        
        t_name = target["name"]
        t_id = f"{config[CONF_ID].id}_{idx}"
        has_white_channel = target["has_white"]

        # 1. Speed
        if is_included(EXCLUDE_SPEED):
            speed_init = 128
            conf = {
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_speed"),
                CONF_NAME: f"{t_name} Speed",
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
            intensity_init = 128
            conf = {
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_intensity"),
                CONF_NAME: f"{t_name} Intensity",
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
            palette_init = "Default"
            conf = {
                CONF_ID: cv.declare_id(CFXSelect)(f"{t_id}_palette"),
                CONF_NAME: f"{t_name} Palette",
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
            mirror_init = False
            conf = {
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_mirror"),
                CONF_NAME: f"{t_name} Mirror",
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
            intro_init = "None"
            conf = {
                CONF_ID: cv.declare_id(CFXSelect)(f"{t_id}_intro"),
                CONF_NAME: f"{t_name} Intro",
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
            intro_dur_init = 1.0
            conf = {
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_intro_dur"),
                CONF_NAME: f"{t_name} Intro Duration",
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
            intro_pal_init = False
            conf = {
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_intro_pal"),
                CONF_NAME: f"{t_name} Intro Use Palette",
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

        # 7.5. Outro Effect
        if is_included(EXCLUDE_OUTRO):
            outro_init = "None"
            conf = {
                CONF_ID: cv.declare_id(CFXSelect)(f"{t_id}_outro"),
                CONF_NAME: f"{t_name} Outro",
                CONF_ICON: "mdi:animation-play-outline",
                "optimistic": True,
                CONF_DISABLED_BY_DEFAULT: False,
                CONF_INTERNAL: False,
            }
            outro = cg.new_Pvariable(conf[CONF_ID])
            await select.register_select(outro, conf, options=INTRO_OPTIONS)
            cg.add(outro.publish_state(outro_init))
            cg.add(var.set_outro_effect(outro))

        # 7.6. Outro Duration
        if is_included(EXCLUDE_OUTRO):
            outro_dur_init = 1.0
            conf = {
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_outro_dur"),
                CONF_NAME: f"{t_name} Outro Duration",
                CONF_ICON: "mdi:timer-outline",
                "min_value": 0.5, "max_value": 10.0, "step": 0.1, "initial_value": outro_dur_init,
                "optimistic": True,
                CONF_DISABLED_BY_DEFAULT: False,
                CONF_INTERNAL: False,
                CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
            }
            outro_dur = cg.new_Pvariable(conf[CONF_ID])
            await number.register_number(outro_dur, conf, min_value=0.5, max_value=10.0, step=0.1)
            cg.add(outro_dur.publish_state(outro_dur_init))
            cg.add(var.set_outro_duration(outro_dur))

        # 8. Timer
        if is_included(EXCLUDE_TIMER):
            timer_init = 0
            conf = {
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_timer"),
                CONF_NAME: f"{t_name} Timer (min)",
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
            autotune_init = False
            conf = {
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_autotune"),
                CONF_NAME: f"{t_name} Autotune",
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

        # 9. Force White (SK6812 RGBW only — forces pure W channel)
        if is_included(EXCLUDE_FORCE_WHITE) and has_white_channel:
            force_white_init = False
            conf = {
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_force_white"),
                CONF_NAME: f"{t_name} Force White",
                CONF_ICON: "mdi:white-balance-sunny",
                "optimistic": True,
                CONF_DISABLED_BY_DEFAULT: False,
                CONF_INTERNAL: False,
                CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF"),
            }
            force_white = cg.new_Pvariable(conf[CONF_ID])
            await switch.register_switch(force_white, conf)
            cg.add(force_white.publish_state(force_white_init))
            cg.add(var.set_force_white(force_white))

        # 11. Debug
        if is_included(EXCLUDE_DEBUG) and idx == 0:
            conf = {
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_debug"),
                CONF_NAME: f"{t_name} Debug",
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
