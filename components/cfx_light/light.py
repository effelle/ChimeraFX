"""
CFXLight - Async DMA LED Output for ESPHome
Copyright (c) 2026 Federico Leoni (effelle)

Drop-in replacement for esp32_rmt_led_strip with:
- DMA always enabled (fire-and-forget)
- Chipset-aware timing (WS2812X, SK6812 strict, WS2811)
- Auto-detected mem_block_symbols per ESP32 variant
- Auto RGBW from chipset (SK6812 = 4-byte), manual override available
- all_effects: true — auto-register all ChimeraFX effects from YAML
"""

import os
import yaml

import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome import pins
from esphome.const import (
    CONF_CHIPSET,
    CONF_EFFECTS,
    CONF_IS_RGBW,
    CONF_MAX_REFRESH_RATE,
    CONF_NAME,
    CONF_NUM_LEDS,
    CONF_NUMBER,
    CONF_OUTPUT_ID,
    CONF_PIN,
)

# Constants not present in all ESPHome versions — define locally
CONF_RGB_ORDER = "rgb_order"
CONF_RMT_SYMBOLS = "rmt_symbols"
CONF_IS_WRGB = "is_wrgb"
CONF_DEFAULT_TRANSITION_LENGTH = "default_transition_length"
CONF_ALL_EFFECTS = "all_effects"
CONF_VISUALIZER_IP = "visualizer_ip"
CONF_VISUALIZER_PORT = "visualizer_port"

# Segment configuration keys (Phase 1)
CONF_SEGMENTS = "segments"
CONF_SEGMENT_ID = "id"
CONF_SEGMENT_START = "start"
CONF_SEGMENT_STOP = "stop"
CONF_SEGMENT_MIRROR = "mirror"
CONF_SEGMENT_USE_INTRO = "use_intro"
CONF_SEGMENT_USE_OUTRO = "use_outro"
CONF_SEGMENT_INTRO_DUR = "intro_dur"

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["esp32", "wifi"]

cfx_light_ns = cg.esphome_ns.namespace("cfx_light")
CFXLightOutput = cfx_light_ns.class_(
    "CFXLightOutput", light.AddressableLight
)

ChimeraChipset = cfx_light_ns.enum("ChimeraChipset")
RGBOrder = cfx_light_ns.enum("RGBOrder")

# Chipset enum mapping
CHIPSETS = {
    "WS2812X": ChimeraChipset.CHIPSET_WS2812X,
    "SK6812": ChimeraChipset.CHIPSET_SK6812,
    "WS2811": ChimeraChipset.CHIPSET_WS2811,
}

# Chipsets that use 4-byte RGBW protocol by default
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

# Default byte order per chipset
DEFAULT_ORDER = {
    "WS2812X": RGBOrder.ORDER_GRB,
    "SK6812": RGBOrder.ORDER_GRB,
    "WS2811": RGBOrder.ORDER_RGB,
}


# --- Segment Schema & Validation (Phase 1) ---

SEGMENT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SEGMENT_ID): cv.string,
        cv.Required(CONF_SEGMENT_START): cv.uint16_t,
        cv.Required(CONF_SEGMENT_STOP): cv.uint16_t,
        cv.Optional(CONF_SEGMENT_MIRROR, default=False): cv.boolean,
        cv.Optional(CONF_SEGMENT_USE_INTRO): cv.uint8_t,
        cv.Optional(CONF_SEGMENT_USE_OUTRO): cv.uint8_t,
        cv.Optional(CONF_SEGMENT_INTRO_DUR): cv.positive_time_period_milliseconds,
    }
)

MAX_CFX_SEGMENTS = 6


def _validate_segments(config):
    """Validate segment definitions: bounds, overlaps, uniqueness, count."""
    segments = config.get(CONF_SEGMENTS, [])
    if not segments:
        return config

    num_leds = config[CONF_NUM_LEDS]

    if len(segments) > MAX_CFX_SEGMENTS:
        raise cv.Invalid(
            f"Too many segments: {len(segments)} (max {MAX_CFX_SEGMENTS})"
        )

    seen_ids = set()
    ranges = []

    for i, seg in enumerate(segments):
        seg_id = seg[CONF_SEGMENT_ID]
        start = seg[CONF_SEGMENT_START]
        stop = seg[CONF_SEGMENT_STOP]

        if stop <= start:
            raise cv.Invalid(
                f"Segment '{seg_id}': stop ({stop}) must be > start ({start})"
            )
        if stop > num_leds:
            raise cv.Invalid(
                f"Segment '{seg_id}': stop ({stop}) exceeds num_leds ({num_leds})"
            )
        if seg_id in seen_ids:
            raise cv.Invalid(f"Duplicate segment id: '{seg_id}'")
        seen_ids.add(seg_id)
        ranges.append((start, stop, seg_id))

    # Check for overlaps (sort by start, then check consecutive)
    ranges.sort(key=lambda r: r[0])
    for i in range(1, len(ranges)):
        prev_start, prev_stop, prev_id = ranges[i - 1]
        curr_start, curr_stop, curr_id = ranges[i]
        if curr_start < prev_stop:
            raise cv.Invalid(
                f"Segments '{prev_id}' ({prev_start}-{prev_stop}) "
                f"and '{curr_id}' ({curr_start}-{curr_stop}) overlap"
            )

    return config


def _load_effects_yaml():
    """Load chimera_fx_effects.yaml from the project root (sibling of components/)."""
    this_dir = os.path.dirname(__file__)
    # components/cfx_light/ → components/ → project root
    project_root = os.path.dirname(os.path.dirname(this_dir))
    yaml_path = os.path.join(project_root, "chimera_fx_effects.yaml")
    if not os.path.isfile(yaml_path):
        return []
    with open(yaml_path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f) or []


def _inject_all_effects(config):
    """If all_effects is true, parse chimera_fx_effects.yaml and inject
    synthetic addressable_cfx entries into the effects list.
    User-defined effects with the same name take priority (overrides)."""
    if not config.get(CONF_ALL_EFFECTS, False):
        return config

    user_effects = list(config.get(CONF_EFFECTS, []))

    # Collect names already defined by the user (they take priority)
    user_names = set()
    for eff in user_effects:
        if "addressable_cfx" in eff:
            name = eff["addressable_cfx"].get(CONF_NAME, "")
    use_intro = config.get("use_intro")
    use_outro = config.get("use_outro")
    intro_dur_raw = config.get("intro_dur")
    
    intro_dur_sec = None
    if intro_dur_raw is not None:
        try:
            parsed_ms = cv.positive_time_period_milliseconds(intro_dur_raw)
            intro_dur_sec = float(parsed_ms) / 1000.0
        except Exception:
            # Fallback if invalid format during pre-validation
            pass

    # Parse the YAML and inject effects not already defined by the user
    for entry in _load_effects_yaml():
        if "addressable_cfx" not in entry:
            continue
        effect_data = entry["addressable_cfx"]
        name = effect_data.get("name", "")
        if name and name not in user_names:
            user_effects.append(entry)

    # Finally, apply global use_intro / use_outro to ALL effects if they don't already have one
    for eff in user_effects:
        if "addressable_cfx" in eff:
            effect_data = eff["addressable_cfx"]
            effect_id = effect_data.get("effect_id", -1)
            
            # Skip hardcoded exceptions
            if effect_id in [158, 159, 161]:
                continue
                
            if use_intro is not None and "set_intro" not in effect_data:
                effect_data["set_intro"] = use_intro
            if use_outro is not None and "set_outro" not in effect_data:
                effect_data["set_outro"] = use_outro
            if intro_dur_sec is not None:
                if "set_intro_dur" not in effect_data:
                    effect_data["set_intro_dur"] = intro_dur_sec
                if "set_outro_dur" not in effect_data:
                    effect_data["set_outro_dur"] = intro_dur_sec

    config[CONF_EFFECTS] = user_effects
    return config

CONFIG_SCHEMA = cv.All(
    _inject_all_effects,
    light.ADDRESSABLE_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(CFXLightOutput),
            cv.Required(CONF_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_NUM_LEDS): cv.positive_not_null_int,
            cv.Required(CONF_CHIPSET): cv.one_of(*CHIPSETS, upper=True),
            cv.Optional(CONF_RGB_ORDER): cv.enum(RGB_ORDERS, upper=True),
            cv.Optional(CONF_IS_RGBW): cv.boolean,
            cv.Optional(CONF_IS_WRGB, default=False): cv.boolean,
            cv.Optional(CONF_ALL_EFFECTS, default=False): cv.boolean,
            cv.Optional("use_intro"): cv.uint8_t,
            cv.Optional("use_outro"): cv.uint8_t,
            cv.Optional("intro_dur"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_DEFAULT_TRANSITION_LENGTH, default="0ms"): (
                cv.positive_time_period_milliseconds
            ),
            cv.Optional(CONF_MAX_REFRESH_RATE): cv.positive_time_period_microseconds,
            cv.Optional(CONF_RMT_SYMBOLS, default=0): cv.uint32_t,
            cv.Optional(CONF_VISUALIZER_IP): cv.string,
            cv.Optional(CONF_VISUALIZER_PORT, default=7777): cv.port,
            # Segment definitions (Phase 1)
            cv.Optional(CONF_SEGMENTS): cv.ensure_list(SEGMENT_SCHEMA),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_segments,  # Must run AFTER schema accepts the 'segments' key
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

    # RGBW: explicit override > auto-detect from chipset
    if CONF_IS_RGBW in config:
        is_rgbw = config[CONF_IS_RGBW]
    else:
        is_rgbw = chipset_name in RGBW_CHIPSETS
    cg.add(var.set_is_rgbw(is_rgbw))

    # wrgb explicitly required by some variants
    cg.add(var.set_is_wrgb(config[CONF_IS_WRGB]))

    # RGB Order: explicit > auto-detect from chipset
    if CONF_RGB_ORDER in config:
        rgb_order = config[CONF_RGB_ORDER]
    else:
        rgb_order = DEFAULT_ORDER.get(chipset_name, RGBOrder.ORDER_GRB)
    cg.add(var.set_rgb_order(rgb_order))

    # 0 = auto-detect from chip variant in C++
    cg.add(var.set_rmt_symbols(config[CONF_RMT_SYMBOLS]))

    if CONF_MAX_REFRESH_RATE in config:
        cg.add(var.set_max_refresh_rate(config[CONF_MAX_REFRESH_RATE]))

    if CONF_VISUALIZER_IP in config:
        cg.add(var.set_visualizer_ip(config[CONF_VISUALIZER_IP]))
        cg.add(var.set_visualizer_port(config[CONF_VISUALIZER_PORT]))

    # --- Segment codegen (Phase 1) ---
    # Root-level intro/outro defaults (segments inherit these)
    if "use_intro" in config:
        cg.add(var.set_default_intro_mode(config["use_intro"]))
    if "use_outro" in config:
        cg.add(var.set_default_outro_mode(config["use_outro"]))
    if "intro_dur" in config:
        intro_dur_ms = config["intro_dur"]
        cg.add(var.set_default_intro_dur(float(intro_dur_ms) / 1000.0))
        cg.add(var.set_default_outro_dur(float(intro_dur_ms) / 1000.0))

    for seg in config.get(CONF_SEGMENTS, []):
        seg_id = seg[CONF_SEGMENT_ID]
        seg_start = seg[CONF_SEGMENT_START]
        seg_stop = seg[CONF_SEGMENT_STOP]
        seg_mirror = seg.get(CONF_SEGMENT_MIRROR, False)
        seg_intro = seg.get(CONF_SEGMENT_USE_INTRO, 0)
        seg_outro = seg.get(CONF_SEGMENT_USE_OUTRO, 0)

        seg_intro_dur = 0.0
        if CONF_SEGMENT_INTRO_DUR in seg:
            seg_intro_dur = float(seg[CONF_SEGMENT_INTRO_DUR]) / 1000.0

        cg.add(
            var.add_segment_def(
                seg_id, seg_start, seg_stop, seg_mirror,
                seg_intro, seg_outro, seg_intro_dur, seg_intro_dur
            )
        )
