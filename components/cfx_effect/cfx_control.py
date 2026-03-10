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
    CONF_RESTORE_MODE, CONF_ENTITY_CATEGORY
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

INTRO_OPTIONS = ["None", "Wipe", "Fade", "Center", "Glitter", "Twin Pulse", "Morse Code", "Quadrant", "Pressurize", "Drain"]

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

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CFXControl),
    cv.Required(CONF_NAME): cv.string,
    cv.Required(CONF_LIGHT): cv.ensure_list(cv.use_id(light.LightState)),
    cv.Optional(CONF_EXCLUDE, default=[]): cv.ensure_list(cv.int_range(min=1, max=10)),
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
                if "segments" in lconf:
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

    has_segments = len(all_targets) > 1

    for idx, target in enumerate(all_targets):
        is_effect_target = not has_segments or idx > 0

        if idx == 0:
            var_id = config[CONF_ID]
        else:
            var_id = core.ID(f"{config[CONF_ID].id}_{idx}", is_declaration=True, type=CFXControl)
            
        var = cg.new_Pvariable(var_id)
        if idx > 0:
            import esphome.core as _core
            _core.CORE.component_ids.add(str(var_id))
        await cg.register_component(var, config if idx == 0 else {})
        cg.add(var.set_light(target["state"]))
        
        t_name = target["name"]
        t_id = f"{config[CONF_ID].id}_{idx}"
        has_white_channel = target["has_white"]

        # Base conf for all entities to avoid KeyErrors in setup_entity
        base_entity_conf = {
            CONF_DISABLED_BY_DEFAULT: False,
            CONF_INTERNAL: False,
        }

        # 1. Autotune
        if is_effect_target and is_included(EXCLUDE_AUTOTUNE):
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_autotune"),
                CONF_NAME: f"{t_name} Autotune",
                CONF_ICON: "mdi:auto-fix",
                "optimistic": True,
                CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF"),
            }
            autotune = cg.new_Pvariable(conf[CONF_ID])
            await switch.register_switch(autotune, conf)
            cg.add(autotune.write_state(False))
            cg.add(var.set_autotune(autotune))

        # 2. Force White
        if is_effect_target and is_included(EXCLUDE_FORCE_WHITE) and has_white_channel:
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_force_white"),
                CONF_NAME: f"{t_name} Force White",
                CONF_ICON: "mdi:white-balance-sunny",
                "optimistic": True,
                CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF"),
            }
            force_white = cg.new_Pvariable(conf[CONF_ID])
            await switch.register_switch(force_white, conf)
            cg.add(force_white.publish_state(False))
            cg.add(var.set_force_white(force_white))

        # 3. Mirror
        if is_effect_target and is_included(EXCLUDE_MIRROR):
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_mirror"),
                CONF_NAME: f"{t_name} Mirror",
                CONF_ICON: "mdi:swap-horizontal",
                "optimistic": True,
                CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF"),
            }
            mirror = cg.new_Pvariable(conf[CONF_ID])
            await switch.register_switch(mirror, conf)
            cg.add(var.set_mirror(mirror))
            cg.add(mirror.publish_state(False))

        # 4. Palette
        if is_effect_target and is_included(EXCLUDE_PALETTE):
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXSelect)(f"{t_id}_palette"),
                CONF_NAME: f"{t_name} Palette",
                CONF_ICON: "mdi:palette",
                "optimistic": True,
            }
            palette = cg.new_Pvariable(conf[CONF_ID])
            await select.register_select(palette, conf, options=PALETTE_OPTIONS)
            cg.add(palette.publish_state("Default"))
            cg.add(var.set_palette(palette))

        # 5. Speed
        if is_effect_target and is_included(EXCLUDE_SPEED):
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_speed"),
                CONF_NAME: f"{t_name} Speed",
                CONF_ICON: "mdi:speedometer",
                "min_value": 0, "max_value": 255, "step": 1, "initial_value": 128,
                "optimistic": True,
                CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
            }
            speed = cg.new_Pvariable(conf[CONF_ID])
            await number.register_number(speed, conf, min_value=0, max_value=255, step=1)
            cg.add(speed.publish_state(128))
            cg.add(var.set_speed(speed))

        # 6. Intensity
        if is_effect_target and is_included(EXCLUDE_INTENSITY):
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_intensity"),
                CONF_NAME: f"{t_name} Intensity",
                CONF_ICON: "mdi:brightness-6",
                "min_value": 0, "max_value": 255, "step": 1, "initial_value": 128,
                "optimistic": True,
                CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
            }
            intensity = cg.new_Pvariable(conf[CONF_ID])
            await number.register_number(intensity, conf, min_value=0, max_value=255, step=1)
            cg.add(intensity.publish_state(128))
            cg.add(var.set_intensity(intensity))

        # 7. Intro & Intro Duration
        if is_effect_target and is_included(EXCLUDE_INTRO):
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXSelect)(f"{t_id}_intro"),
                CONF_NAME: f"{t_name} Intro",
                CONF_ICON: "mdi:animation-play",
                "optimistic": True,
            }
            intro = cg.new_Pvariable(conf[CONF_ID])
            await select.register_select(intro, conf, options=INTRO_OPTIONS)
            cg.add(intro.publish_state("None"))
            cg.add(var.set_intro_effect(intro))

            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_intro_dur"),
                CONF_NAME: f"{t_name} Intro Duration",
                CONF_ICON: "mdi:timer-outline",
                "min_value": 0.5, "max_value": 10.0, "step": 0.1, "initial_value": 1.0,
                "optimistic": True,
                CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
            }
            intro_dur = cg.new_Pvariable(conf[CONF_ID])
            await number.register_number(intro_dur, conf, min_value=0.5, max_value=10.0, step=0.1)
            cg.add(intro_dur.publish_state(1.0))
            cg.add(var.set_intro_duration(intro_dur))

        # 8. Outro & Outro Duration
        if is_effect_target and is_included(EXCLUDE_OUTRO):
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXSelect)(f"{t_id}_outro"),
                CONF_NAME: f"{t_name} Outro",
                CONF_ICON: "mdi:animation-play-outline",
                "optimistic": True,
            }
            outro = cg.new_Pvariable(conf[CONF_ID])
            await select.register_select(outro, conf, options=INTRO_OPTIONS)
            cg.add(outro.publish_state("None"))
            cg.add(var.set_outro_effect(outro))

            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_outro_dur"),
                CONF_NAME: f"{t_name} Outro Duration",
                CONF_ICON: "mdi:timer-outline",
                "min_value": 0.5, "max_value": 10.0, "step": 0.1, "initial_value": 1.0,
                "optimistic": True,
                CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
            }
            outro_dur = cg.new_Pvariable(conf[CONF_ID])
            await number.register_number(outro_dur, conf, min_value=0.5, max_value=10.0, step=0.1)
            cg.add(outro_dur.publish_state(1.0))
            cg.add(var.set_outro_duration(outro_dur))

        # 9. Intro Use Palette
        if is_effect_target and is_included(EXCLUDE_INTRO):
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_intro_pal"),
                CONF_NAME: f"{t_name} Intro Use Palette",
                CONF_ICON: "mdi:palette-swatch-variant",
                "optimistic": True,
                CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF"),
            }
            intro_pal = cg.new_Pvariable(conf[CONF_ID])
            await switch.register_switch(intro_pal, conf)
            cg.add(intro_pal.publish_state(False))
            cg.add(var.set_intro_use_palette(intro_pal))

        # 10. Timer
        if is_effect_target and is_included(EXCLUDE_TIMER):
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_timer"),
                CONF_NAME: f"{t_name} Timer (min)",
                CONF_ICON: "mdi:timer-sand",
                "min_value": 0, "max_value": 360, "step": 1, "initial_value": 0,
                "optimistic": True,
                CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
            }
            timer = cg.new_Pvariable(conf[CONF_ID])
            await number.register_number(timer, conf, min_value=0, max_value=360, step=1)
            cg.add(timer.publish_state(0))
            cg.add(var.set_timer(timer))

        # 11. Debug (ONLY on Master, placed LAST in Master block)
        if is_included(EXCLUDE_DEBUG) and idx == 0:
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_debug"),
                CONF_NAME: f"{t_name} Debug",
                CONF_ICON: "mdi:bug",
                "optimistic": True,
                CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF"),
                CONF_ENTITY_CATEGORY: cg.RawExpression("EntityCategory::ENTITY_CATEGORY_DIAGNOSTIC"),
            }
            debug = cg.new_Pvariable(conf[CONF_ID])
            await switch.register_switch(debug, conf)
            cg.add(debug.publish_state(False))
            cg.add(var.set_debug(debug))
