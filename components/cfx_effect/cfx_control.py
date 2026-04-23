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
    "Default",      # 0
    "Aurora",       # 1
    "Forest",       # 2
    "Halloween",    # 3
    "Rainbow",      # 4
    "Fire",         # 5
    "Sunset",       # 6
    "Ice",          # 7
    "Party",        # 8
    "Twilight",     # 9
    "Pastel",       # 10
    "Ocean",        # 11
    "HeatColors",   # 12
    "Sakura",       # 13
    "Rivendell",    # 14
    "Cyberpunk",    # 15
    "OrangeTeal",   # 16
    "Christmas",    # 17
    "RedBlue",      # 18
    "Matrix",       # 19
    "SunnyGold",    # 20
    "Solid",        # 21
    "Fairy",        # 22
    "Smart Random"  # 23 (ID 254)
]

COMMON_TRANSITIONS = ["None", "Center", "Eclipse", "Fade", "Gas Discharge", "Glitter", "Harmonic Settle", "Lithograph", "Morse Code", "Quadrant", "Tidal Surge", "Twin Pulse", "Wipe"]
INTRO_ONLY = ["Construct", "Crystallize", "Deep Breathe", "Dropping", "Inertia Sweep", "Interference", "Moiré Shift", "Pressurize", "Resonance", "Sonar Reveal", "Stellar Dust", "Telemetry", "Venetian"]
OUTRO_ONLY = ["Close Blinds", "Drain", "Decelerate", "Dismantle", "Emptying", "Erode", "Exhale", "Interference Fade", "Moiré Fade", "Resonance Fade", "Sonar Fade", "Stellar Fade", "Telemetry Fade"]

INTRO_OPTIONS = [
    "None",             # 0
    "Wipe",             # 1
    "Fade",             # 2
    "Center",           # 3
    "Glitter",          # 4
    "Twin Pulse",       # 5
    "Morse Code",       # 6
    "Quadrant",         # 7
    "Pressurize",       # 8
    "Dropping",         # 9
    "Construct",        # 10
    "Inertia Sweep",    # 11
    "Sonar Reveal",     # 12
    "Venetian",         # 13
    "Crystallize",      # 14
    "Deep Breathe",     # 15
    "Moiré Shift",      # 16
    "Resonance",        # 17
    "Telemetry",        # 18
    "Stellar Dust",     # 19
    "Interference",     # 20
    "Eclipse",          # 21
    "Gas Discharge",    # 22
    "Harmonic Settle",  # 23
    "Lithograph",       # 24
    "Tidal Surge",      # 25
    "Impact Flare"      # 26
]

OUTRO_OPTIONS = [
    "None",             # 0
    "Wipe",             # 1
    "Fade",             # 2
    "Center",           # 3
    "Glitter",          # 4
    "Twin Pulse",       # 5
    "Morse Code",       # 6
    "Quadrant",         # 7
    "Drain",            # 8
    "Emptying",         # 9
    "Dismantle",        # 10
    "Decelerate",       # 11
    "Sonar Fade",       # 12
    "Close Blinds",     # 13
    "Erode",            # 14
    "Exhale",           # 15
    "Moiré Fade",       # 16
    "Resonance Fade",   # 17
    "Telemetry Fade",   # 18
    "Stellar Fade",     # 19
    "Interference Fade",# 20
    "Eclipse",          # 21
    "Gas Discharge",    # 22
    "Harmonic Settle",  # 23
    "Lithograph",       # 24
    "Tidal Recede",     # 25
    "Center Squeeze"    # 27
]

CONF_LIGHT = "light_id"
CONF_EXCLUDE = "exclude"

EXCLUDE_SPEED = 1
EXCLUDE_INTENSITY = 2
EXCLUDE_PALETTE = 3
EXCLUDE_MIRROR = 4
EXCLUDE_INTRO = 5       # excludes: Intro select + Outro select + In/Out Duration slider
EXCLUDE_HA_EVENTS = 6  # CFX-026: replaces Timer — exclude to disable HA event firing
EXCLUDE_AUTOTUNE = 7
EXCLUDE_FORCE_WHITE = 8
EXCLUDE_DEBUG = 9

# CONFIG_SCHEMA removed — cfx_control is now internal-only.
# Controls are auto-generated from cfx_light entries via __init__.py.
# The to_code() function below is called with synthetic configs.

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
                        seg_light_id = seg.get("id")
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

        # 7. Intro/Outro suite (intro select + outro select + shared duration)
        #    ctrl_exclude: [5] removes all three at once.
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
                CONF_ID: cv.declare_id(CFXSelect)(f"{t_id}_outro"),
                CONF_NAME: f"{t_name} Outro",
                CONF_ICON: "mdi:animation-play-outline",
                "optimistic": True,
            }
            outro = cg.new_Pvariable(conf[CONF_ID])
            await select.register_select(outro, conf, options=OUTRO_OPTIONS)
            cg.add(outro.publish_state("None"))
            cg.add(var.set_outro_effect(outro))

            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXNumber)(f"{t_id}_inout_dur"),
                CONF_NAME: f"{t_name} In/Out Duration",
                CONF_ICON: "mdi:timer-outline",
                "min_value": 0.5, "max_value": 10.0, "step": 0.1, "initial_value": 1.0,
                "optimistic": True,
                CONF_MODE: number.NumberMode.NUMBER_MODE_AUTO,
            }
            inout_dur = cg.new_Pvariable(conf[CONF_ID])
            await number.register_number(inout_dur, conf, min_value=0.5, max_value=10.0, step=0.1)
            cg.add(inout_dur.publish_state(1.0))
            cg.add(var.set_inout_duration(inout_dur))

        # 10. HA Events (CFX-026: replaces Timer)
        # When ID 6 is included (default), HA event firing is enabled.
        # exclude: [6] disables cfx_reach/cfx_idle/cfx_start events to HA.
        if is_effect_target:
            import re
            def _cfx_slugify(name: str) -> str:
                return re.sub(r'[^a-z0-9]+', '_', name.lower()).strip('_')
            t_tag = _cfx_slugify(str(t_name)) if t_name else str(target["id"])
            
            if not is_included(EXCLUDE_HA_EVENTS):
                cg.add(cg.RawExpression(f'chimera_fx::CFXEventManager::get().set_ha_events_disabled_for_tag("{t_tag}");'))

        # 11. Debug (ONLY on Master, placed LAST in Master block)
        if is_included(EXCLUDE_DEBUG) and idx == 0:
            conf = {
                **base_entity_conf,
                CONF_ID: cv.declare_id(CFXSwitch)(f"{t_id}_debug"),
                CONF_NAME: f"{t_name} Debug",
                CONF_ICON: "mdi:bug",
                "optimistic": True,
                CONF_RESTORE_MODE: cg.RawExpression("switch_::SWITCH_RESTORE_DEFAULT_OFF"),
                CONF_ENTITY_CATEGORY: "diagnostic",
            }
            debug = cg.new_Pvariable(conf[CONF_ID])
            await switch.register_switch(debug, conf)
            cg.add(debug.publish_state(False))
            cg.add(var.set_debug(debug))
