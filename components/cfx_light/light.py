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

# Component schema revision. Keep this near the top so ESPHome external-component
# caches see a Python-side change when validation behavior must be refreshed.
CFX_LIGHT_SCHEMA_REV = 2

import esphome.codegen as cg
from esphome.components import light, event
import esphome.config_validation as cv
import esphome.core as core
from esphome.core import CORE
from esphome.final_validate import full_config
from esphome import pins
from esphome.const import (
    CONF_BLUE,
    CONF_BRIGHTNESS,
    CONF_CHIPSET,
    CONF_COLOR_MODE,
    CONF_EFFECTS,
    CONF_GREEN,
    CONF_ID,
    CONF_INITIAL_STATE,
    CONF_IS_RGBW,
    CONF_MAX_REFRESH_RATE,
    CONF_NAME,
    CONF_NUM_LEDS,
    CONF_NUMBER,
    CONF_OUTPUT_ID,
    CONF_PIN,
    CONF_RED,
    CONF_UPDATE_INTERVAL,
    CONF_WHITE,
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
CONF_SEGMENT_NAME = "name"
CONF_SEGMENT_START = "start"
CONF_SEGMENT_STOP = "stop"
CONF_SEGMENT_MIRROR = "mirror"
CONF_SEGMENT_USE_INTRO = "use_intro"
CONF_SEGMENT_USE_OUTRO = "use_outro"
CONF_SEGMENT_INTRO_DUR = "inout_dur"
CONF_SEGMENT_SET_INTRO = "set_intro"
CONF_SEGMENT_SET_OUTRO = "set_outro"
CONF_SEGMENT_SET_INOUT_DUR = "set_inout_dur"
CONF_SEGMENT_SET_BRIGHTNESS = "set_brightness"
CONF_SEGMENT_SET_COLOR = "set_color"
CONF_SEGMENT_OUTPUT_ID = "output_id"
CONF_SEGMENT_LIGHT_ID = "light_id"

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["event", "cfx_effect"]

cfx_light_ns = cg.esphome_ns.namespace("cfx_light")
CFXLightOutput = cfx_light_ns.class_(
    "CFXLightOutput", light.AddressableLight
)
CFXVirtualSegmentLight = cfx_light_ns.class_(
    "CFXVirtualSegmentLight", light.AddressableLight, cg.Component
)

ChimeraChipset = cfx_light_ns.enum("ChimeraChipset")
RGBOrder = cfx_light_ns.enum("RGBOrder")

# Chipset enum mapping
CHIPSETS = {
    "WS2812X": ChimeraChipset.CHIPSET_WS2812X,
    "SK6812": ChimeraChipset.CHIPSET_SK6812,
    "WS2811": ChimeraChipset.CHIPSET_WS2811,
    "APA102": ChimeraChipset.CHIPSET_APA102,
    "SK9822": ChimeraChipset.CHIPSET_SK9822,
}

# Chipsets that use SPI transport instead of RMT
SPI_CHIPSETS = {"APA102", "SK9822"}

# Chipsets that use 4-byte RGBW protocol by default
RGBW_CHIPSETS = {"SK6812"}

# SPI Host mapping
SPI_HOSTS = {
    "SPI2_HOST": cfx_light_ns.enum("CFXSPIHost").SPI_HOST_2,
    "SPI3_HOST": cfx_light_ns.enum("CFXSPIHost").SPI_HOST_3,
}

# SPI variants that only have SPI2 (no SPI3_HOST available)
_SPI_SINGLE_HOST_VARIANTS = {"ESP32C3", "ESP32C5", "ESP32C6", "ESP32H2", "ESP32C2"}

# SPI host assignment registry key in CORE.data.
# Using CORE.data (reset between compile runs) instead of a module-level
# counter prevents spurious errors when ESPHome validates the same config
# multiple times within a single compile pass.
_SPI_HOST_REGISTRY_KEY = "cfx_spi_host_registry"

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
    "APA102": RGBOrder.ORDER_BGR,
    "SK9822": RGBOrder.ORDER_BGR,
}

# Config keys
CONF_DATA_PIN = "data_pin"
CONF_CLOCK_PIN = "clock_pin"
CONF_SPI_SPEED = "spi_speed"
CONF_SPI_HOST = "spi_host"
CONF_SET_INTRO = "set_intro"
CONF_SET_OUTRO = "set_outro"
CONF_SET_INOUT_DUR = "set_inout_dur"
CONF_SET_BRIGHTNESS = "set_brightness"
CONF_SET_COLOR = "set_color"

SET_COLOR_SCHEMA = cv.All(
    cv.ensure_list(cv.int_range(min=0, max=100)),
    cv.Length(min=3, max=4),
)

# --- Segment Schema & Validation (Phase 1) ---

SEGMENT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SEGMENT_ID): cv.declare_id(light.LightState),
        cv.Optional(CONF_SEGMENT_NAME): cv.string,
        cv.GenerateID(CONF_SEGMENT_OUTPUT_ID): cv.declare_id(CFXVirtualSegmentLight),
        cv.Required(CONF_SEGMENT_START): cv.uint16_t,
        cv.Required(CONF_SEGMENT_STOP): cv.uint16_t,
        cv.Optional(CONF_SEGMENT_MIRROR, default=False): cv.boolean,
        cv.Optional(CONF_SEGMENT_USE_INTRO): cv.uint8_t,
        cv.Optional(CONF_SEGMENT_SET_INTRO): cv.uint8_t,
        cv.Optional(CONF_SEGMENT_USE_OUTRO): cv.uint8_t,
        cv.Optional(CONF_SEGMENT_SET_OUTRO): cv.uint8_t,
        cv.Optional(CONF_SEGMENT_INTRO_DUR): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SEGMENT_SET_INOUT_DUR): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SEGMENT_SET_BRIGHTNESS): cv.percentage,
        cv.Optional(CONF_SEGMENT_SET_COLOR): SET_COLOR_SCHEMA,
    }
)

MAX_CFX_SEGMENTS = 4
MAX_CFX_LIGHTS = 4


def _coalesce_alias(config, canonical_key, alias_keys, *, scope):
    present = [key for key in [canonical_key, *alias_keys] if key in config]
    if not present:
        return None

    values = [config[key] for key in present]
    first = values[0]
    if any(value != first for value in values[1:]):
        joined = ", ".join(present)
        raise cv.Invalid(
            f"Conflicting values for {scope}: {joined}. Use only one spelling."
        )

    return first


def _normalize_control_aliases(config):
    config = dict(config)

    effects_conf = config.get(CONF_EFFECTS)
    if isinstance(effects_conf, dict):
        config[CONF_EFFECTS] = [effects_conf]

    intro_val = _coalesce_alias(
        config, "use_intro", [CONF_SET_INTRO], scope="cfx_light intro default"
    )
    if intro_val is not None:
        config["use_intro"] = intro_val

    outro_val = _coalesce_alias(
        config, "use_outro", [CONF_SET_OUTRO], scope="cfx_light outro default"
    )
    if outro_val is not None:
        config["use_outro"] = outro_val

    dur_val = _coalesce_alias(
        config, "inout_dur", [CONF_SET_INOUT_DUR], scope="cfx_light in/out duration default"
    )
    if dur_val is not None:
        config["inout_dur"] = dur_val

    if CONF_SEGMENTS in config:
        normalized_segments = []
        for seg in config[CONF_SEGMENTS]:
            seg = dict(seg)

            seg_intro_val = _coalesce_alias(
                seg,
                CONF_SEGMENT_USE_INTRO,
                [CONF_SEGMENT_SET_INTRO],
                scope=f"segment '{seg.get(CONF_SEGMENT_NAME, seg[CONF_SEGMENT_ID])}' intro default",
            )
            if seg_intro_val is not None:
                seg[CONF_SEGMENT_USE_INTRO] = seg_intro_val

            seg_outro_val = _coalesce_alias(
                seg,
                CONF_SEGMENT_USE_OUTRO,
                [CONF_SEGMENT_SET_OUTRO],
                scope=f"segment '{seg.get(CONF_SEGMENT_NAME, seg[CONF_SEGMENT_ID])}' outro default",
            )
            if seg_outro_val is not None:
                seg[CONF_SEGMENT_USE_OUTRO] = seg_outro_val

            seg_dur_val = _coalesce_alias(
                seg,
                CONF_SEGMENT_INTRO_DUR,
                [CONF_SEGMENT_SET_INOUT_DUR],
                scope=f"segment '{seg.get(CONF_SEGMENT_NAME, seg[CONF_SEGMENT_ID])}' in/out duration default",
            )
            if seg_dur_val is not None:
                seg[CONF_SEGMENT_INTRO_DUR] = seg_dur_val

            normalized_segments.append(seg)

        config[CONF_SEGMENTS] = normalized_segments

    return config


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


def _final_validate(config):
    fconf = full_config.get()
    all_lights = fconf.get_config_for_path(["light"])
    cfx_lights = [
        lconf for lconf in all_lights if lconf.get("platform", "") == "cfx_light"
    ]
    if len(cfx_lights) > MAX_CFX_LIGHTS:
        raise cv.Invalid(
            f"Too many cfx_light entries: {len(cfx_lights)} (max {MAX_CFX_LIGHTS} per node)"
        )
    return config


def _config_has_white_channel(config):
    if CONF_IS_RGBW in config:
        return config[CONF_IS_RGBW]
    return config.get(CONF_CHIPSET) in RGBW_CHIPSETS


def _validate_set_color(config):
    has_white_channel = _config_has_white_channel(config)

    def _check_color(color, scope):
        if len(color) == 4 and not has_white_channel:
            raise cv.Invalid(
                f"{scope} uses a 4-channel set_color, but this strip has no white channel."
            )

    root_color = config.get(CONF_SET_COLOR)
    if root_color is not None:
        _check_color(root_color, "cfx_light")

    initial_state = config.get(CONF_INITIAL_STATE)
    if initial_state is not None and root_color is not None:
        color_keys = {
            CONF_COLOR_MODE,
            CONF_RED,
            CONF_GREEN,
            CONF_BLUE,
            CONF_WHITE,
        }
        overlap = sorted(color_keys.intersection(initial_state))
        if overlap:
            joined = ", ".join(overlap)
            raise cv.Invalid(
                f"cfx_light cannot use both {CONF_SET_COLOR} and initial_state keys: {joined}"
            )

    if initial_state is not None and CONF_SET_BRIGHTNESS in config:
        if CONF_BRIGHTNESS in initial_state:
            raise cv.Invalid(
                f"cfx_light cannot use both {CONF_SET_BRIGHTNESS} and initial_state key: {CONF_BRIGHTNESS}"
            )

    for seg in config.get(CONF_SEGMENTS, []):
        seg_color = seg.get(CONF_SEGMENT_SET_COLOR)
        if seg_color is not None:
            _check_color(
                seg_color,
                f"segment '{seg.get(CONF_SEGMENT_NAME, seg[CONF_SEGMENT_ID])}'",
            )

    return config

def _inject_all_effects(config):
    """If all_effects is true, inject synthetic addressable_cfx entries from
    the CFX_EFFECTS Python registry in cfx_effect/__init__.py.
    User-defined effects with the same name take priority (overrides)."""
    chipset = str(config.get(CONF_CHIPSET, "")).upper()
    light_update_interval = "8ms" if chipset in SPI_CHIPSETS else None

    user_effects = list(config.get(CONF_EFFECTS, []))

    for eff in user_effects:
        if not isinstance(eff, dict):
            continue
        eff_cfx = eff.get("addressable_cfx")
        if isinstance(eff_cfx, dict) and light_update_interval is not None:
            eff_cfx.setdefault(CONF_UPDATE_INTERVAL, light_update_interval)

    if not config.get(CONF_ALL_EFFECTS, True):
        config[CONF_EFFECTS] = user_effects
        return config

    from esphome.components.cfx_effect import CFX_EFFECTS

    # Collect names already defined by the user (they take priority)
    user_names = set()
    has_user_presets = False
    has_presets_separator = False
    for eff in user_effects:
        if not isinstance(eff, dict):
            continue
        eff_cfx = eff.get("addressable_cfx")
        if isinstance(eff_cfx, dict):
            has_user_presets = True
            name = eff_cfx.get(CONF_NAME, "")
            if name:
                user_names.add(name)
                if name == "--- Presets ---":
                    has_presets_separator = True

    use_intro = config.get("use_intro")
    use_outro = config.get("use_outro")
    intro_dur_raw = config.get("inout_dur")
    intro_dur_sec = None
    if intro_dur_raw is not None:
        try:
            parsed_ms = cv.positive_time_period_milliseconds(intro_dur_raw)
            intro_dur_sec = float(parsed_ms) / 1000.0
        except Exception:
            pass

    if has_user_presets and not has_presets_separator:
        user_effects.insert(
            0,
            {
                "addressable_cfx": {
                    "effect_id": 185,
                    CONF_NAME: "--- Presets ---",
                }
            },
        )
        user_names.add("--- Presets ---")

    # Build synthetic entries from the Python registry (order preserved).
    for (cat, eid, name) in CFX_EFFECTS:
        if name in user_names:
            continue
        effect_data = {"effect_id": eid, CONF_NAME: name}
        if light_update_interval is not None:
            effect_data[CONF_UPDATE_INTERVAL] = light_update_interval
        if cat != "sep" and eid not in [158, 159, 161]:
            if use_intro is not None:
                effect_data["set_intro"] = use_intro
            if use_outro is not None:
                effect_data["set_outro"] = use_outro
            if intro_dur_sec is not None:
                effect_data["set_inout_dur"] = intro_dur_sec
        user_effects.append({"addressable_cfx": effect_data})

    config[CONF_EFFECTS] = user_effects
    return config

# Patch base schema to drop inherited default_transition_length so our 0ms default wins
_base_schema = light.ADDRESSABLE_LIGHT_SCHEMA.schema.copy()
_keys_to_drop = [k for k in _base_schema.keys() if str(k) == CONF_DEFAULT_TRANSITION_LENGTH or getattr(k, "key", None) == CONF_DEFAULT_TRANSITION_LENGTH]
for k in _keys_to_drop:
    del _base_schema[k]

CONFIG_SCHEMA = cv.All(
    _normalize_control_aliases,
    _inject_all_effects,
    cv.Schema(_base_schema).extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(CFXLightOutput),
            cv.Required(CONF_NUM_LEDS): cv.positive_not_null_int,
            cv.Required(CONF_CHIPSET): cv.one_of(*CHIPSETS, upper=True),
            # RMT pins (Optional, validated by _validate_transport)
            cv.Optional(CONF_PIN): pins.internal_gpio_output_pin_schema,
            # SPI pins (Optional, validated by _validate_transport)
            cv.Optional(CONF_DATA_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_CLOCK_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_SPI_SPEED): cv.frequency,
            cv.Optional(CONF_SPI_HOST): cv.string,
            # spi_host is intentionally NOT exposed in the user schema.
            # Host assignment is automatic: 1st SPI strip → SPI2_HOST,
            # 2nd SPI strip → SPI3_HOST. A 3rd strip triggers a compile error.
            # CONF_SPI_HOST is written into config by _validate_transport and
            # read back by to_code — it must remain defined as a key constant.
            # General config
            cv.Optional(CONF_RGB_ORDER): cv.enum(RGB_ORDERS, upper=True),
            cv.Optional(CONF_IS_RGBW): cv.boolean,
            cv.Optional(CONF_IS_WRGB, default=False): cv.boolean,
            cv.Optional(CONF_ALL_EFFECTS, default=True): cv.boolean,
            cv.Optional("use_intro"): cv.uint8_t,
            cv.Optional(CONF_SET_INTRO): cv.uint8_t,
            cv.Optional("use_outro"): cv.uint8_t,
            cv.Optional(CONF_SET_OUTRO): cv.uint8_t,
            cv.Optional("inout_dur"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_SET_INOUT_DUR): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_SET_BRIGHTNESS): cv.percentage,
            cv.Optional(CONF_SET_COLOR): SET_COLOR_SCHEMA,
            cv.Optional(CONF_DEFAULT_TRANSITION_LENGTH, default="0ms"): (
                cv.positive_time_period_milliseconds
            ),
            cv.Optional(CONF_MAX_REFRESH_RATE): cv.positive_time_period_microseconds,
            cv.Optional(CONF_RMT_SYMBOLS, default=0): cv.uint32_t,
            cv.Optional(CONF_VISUALIZER_IP): cv.string,
            cv.Optional(CONF_VISUALIZER_PORT, default=7777): cv.port,
            # Auto-controls (cfx_control entities generated from cfx_light)
            cv.Optional("controls", default=True): cv.boolean,
            cv.Optional("ctrl_exclude", default=[]): cv.ensure_list(cv.int_range(min=1, max=9)),
            # Segment definitions (Phase 1)
            cv.Optional(CONF_SEGMENTS): cv.ensure_list(SEGMENT_SCHEMA),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_segments,  # Must run AFTER schema accepts the 'segments' key
    _validate_set_color,
)

def _validate_transport(config):
    """Ensure transport-specific configuration maps to the selected chipset.

    For SPI chipsets (APA102, SK9822):
    - Validates required pins are present.
    - Auto-assigns SPI host: 1st strip → SPI2_HOST, 2nd → SPI3_HOST.
    - Raises a clear compile-time error for a 3rd SPI strip or unsupported
      host on single-bus variants (C3, H2, etc.).
    """
    chipset = config[CONF_CHIPSET]
    is_spi = chipset in SPI_CHIPSETS

    if is_spi:
        if CONF_PIN in config:
            raise cv.Invalid(f"'{CONF_PIN}' cannot be used with SPI chipset {chipset}")
        if CONF_DATA_PIN not in config:
            raise cv.RequiredFieldInvalid(
                f"'{CONF_DATA_PIN}' is required for SPI chipset {chipset}",
                path=[CONF_DATA_PIN],
            )
        if CONF_CLOCK_PIN not in config:
            raise cv.RequiredFieldInvalid(
                f"'{CONF_CLOCK_PIN}' is required for SPI chipset {chipset}",
                path=[CONF_CLOCK_PIN],
            )
        if config.get(CONF_RMT_SYMBOLS, 0) != 0:
            raise cv.Invalid(
                f"'{CONF_RMT_SYMBOLS}' has no effect on SPI. "
                f"Host assignment is now handled deterministically in to_code "
                f"based on the light's position in the global config."
            )
        return config

    else:
        # RMT path: ensure no SPI keys were accidentally provided.
        if CONF_PIN not in config:
            raise cv.RequiredFieldInvalid(
                f"'{CONF_PIN}' is required for RMT chipset {chipset}",
                path=[CONF_PIN],
            )
        invalid_keys = [
            key
            for key in [CONF_DATA_PIN, CONF_CLOCK_PIN, CONF_SPI_SPEED]
            if key in config
        ]
        if invalid_keys:
            joined_keys = ", ".join(invalid_keys)
            raise cv.Invalid(
                f"SPI options ({joined_keys}) cannot be used with RMT chipset {chipset}"
            )

    return config

CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, _validate_transport)
FINAL_VALIDATE_SCHEMA = _final_validate


# RMT hardware budget per ESP32 variant.
# total_symbols: total RMT symbol memory available across all TX channels.
# block_size:    minimum allocation granularity (symbols must be a multiple).
# Source: ESP-IDF RMT driver docs + empirical testing.
_RMT_BUDGET = {
    "ESP32":   {"total": 512, "block": 64},
    "ESP32S2": {"total": 256, "block": 64},
    "ESP32S3": {"total": 192, "block": 48},
    "ESP32P4": {"total": 192, "block": 48},
    "ESP32C3": {"total":  96, "block": 48},
    "ESP32C5": {"total":  96, "block": 48},
    "ESP32C6": {"total":  96, "block": 48},
    "ESP32H2": {"total":  96, "block": 48},
}
_RMT_DEFAULT_BUDGET = {"total": 512, "block": 64}  # conservative fallback

# Maximum CFXLight instances per ESP32 variant.
# Derived from RMT channel count and practical CPU headroom.
#   ESP32 classic : 8 RMT TX channels, 4 lights @ 2 blocks each
#   ESP32-S2      : 4 RMT TX channels, 4 lights @ 1 block each
#   ESP32-S3/P4   : 4 RMT TX channels, 4 lights @ 1 block each
#   ESP32-C3/C5/C6/H2 : 2 RMT TX channels, 2 lights @ 1 block each
_MAX_LIGHTS = {
    "ESP32":   4,
    "ESP32S2": 4,
    "ESP32S3": 4,
    "ESP32P4": 4,
    "ESP32C3": 2,
    "ESP32C5": 2,
    "ESP32C6": 2,
    "ESP32H2": 2,
}
_MAX_LIGHTS_DEFAULT = 4  # conservative fallback


def _get_rmt_symbols_auto(n_strips: int, manual_reserved: int = 0) -> int:
    """Compute the per-strip RMT symbol count for auto-configured strips.
    Subtracts manually-set allocations from the hardware total before dividing,
    floors to the nearest block boundary, and enforces a one-block minimum.
    Emits a compile-time error if the total allocation exceeds hardware capacity."""
    import logging as _log
    import esphome.core as _core
    variant = "ESP32"  # safe default
    try:
        esp32_conf = _core.CORE.config.get("esp32", {})
        variant_raw = esp32_conf.get("variant", "ESP32")
        # ESPHome stores variant as e.g. "esp32s3" or "ESP32S3" — normalise
        variant = str(variant_raw).upper().replace("-", "").replace("_", "")
    except Exception:
        pass

    budget   = _RMT_BUDGET.get(variant, _RMT_DEFAULT_BUDGET)
    total    = budget["total"]
    block    = budget["block"]

    # Remaining budget after subtracting manually-set allocations
    remaining = total - manual_reserved
    if remaining <= 0:
        _log.getLogger(__name__).error(
            "CFXLight: manually-set rmt_symbols (%d) exhausts the entire "
            "%s RMT budget (%d). No budget left for auto strips.",
            manual_reserved, variant, total,
        )
        remaining = block  # give at least one block as fallback

    max_auto_strips = remaining // block
    if n_strips > max_auto_strips:
        _log.getLogger(__name__).error(
            "CFXLight: %s has %d RMT symbols remaining after manual "
            "reservations (%d used). That supports max %d auto strip(s) "
            "but %d are declared.",
            variant, remaining, manual_reserved, max_auto_strips, n_strips,
        )

    per_strip = remaining // max(n_strips, 1)
    # Floor to block boundary
    per_strip = (per_strip // block) * block
    # Never less than one block — hardware minimum
    per_strip = max(per_strip, block)
    return per_strip


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])

    segments = config.get(CONF_SEGMENTS, [])
    light_config = config
    if segments:
        # --- Phase 2: Per-segment light entities ---
        # The segmented parent keeps its own master LightState so ESPHome state
        # sync, controls, and debug routing behave the same as whole strips.
        master_config = dict(light_config)
        master_config[CONF_EFFECTS] = []  # No effects on master
        await light.register_light(var, master_config)
        light_state = await cg.get_variable(config[CONF_ID])
        cg.add(var.set_master_light_state(light_state))
        await cg.register_component(var, config)
    else:
        # No segments: original single-light behavior
        await light.register_light(var, light_config)
        light_state = await cg.get_variable(config[CONF_ID])
        cg.add(var.set_master_light_state(light_state))
        await cg.register_component(var, config)

    # --- Hardware configuration (always) ---
    cg.add(var.set_num_leds(config[CONF_NUM_LEDS]))
    chipset_name = config[CONF_CHIPSET]
    cg.add(var.set_chipset(CHIPSETS[chipset_name]))
    
    is_spi = chipset_name in SPI_CHIPSETS
    
    # Re-enable ESP-IDF's SPI and RMT drivers unconditionally
    # because cfx_light.h includes both <driver/spi_master.h> and <driver/rmt_tx.h>
    try:
        from esphome.components.esp32 import include_builtin_idf_component
        include_builtin_idf_component("esp_driver_spi")
        include_builtin_idf_component("esp_driver_rmt")
    except ImportError:
        pass

    if is_spi:
        cg.add(var.set_transport(cfx_light_ns.enum("CFXTransport").TRANSPORT_SPI))
        cg.add(var.set_spi_data_pin(config[CONF_DATA_PIN][CONF_NUMBER]))
        cg.add(var.set_spi_clock_pin(config[CONF_CLOCK_PIN][CONF_NUMBER]))
        cg.add(var.set_spi_speed_hz(config.get(CONF_SPI_SPEED, 10000000)))
        
        # Get or create SPI host registry counter in CORE.data
        registry = CORE.data.get(_SPI_HOST_REGISTRY_KEY, [])
        # Assign host: 1st SPI strip gets SPI2, 2nd gets SPI3
        host_idx = len(registry)
        spi_host = cfx_light_ns.enum("CFXSPIHost").SPI_HOST_2 if host_idx % 2 == 0 else cfx_light_ns.enum("CFXSPIHost").SPI_HOST_3
        cg.add(var.set_spi_host(spi_host))
        registry.append(spi_host)
        CORE.data[_SPI_HOST_REGISTRY_KEY] = registry
    else:
        cg.add(var.set_transport(cfx_light_ns.enum("CFXTransport").TRANSPORT_RMT))
        cg.add(var.set_pin(config[CONF_PIN][CONF_NUMBER]))

    # RGBW: explicit override > auto-detect from chipset
    if CONF_IS_RGBW in config:
        is_rgbw = config[CONF_IS_RGBW]
    else:
        is_rgbw = chipset_name in RGBW_CHIPSETS
    cg.add(var.set_is_rgbw(is_rgbw))

    # wrgb explicitly required by some variants
    cg.add(var.set_is_wrgb(config[CONF_IS_WRGB]))

    if CONF_SET_BRIGHTNESS in config:
        cg.add(var.set_turn_on_brightness(config[CONF_SET_BRIGHTNESS]))
    if CONF_SET_COLOR in config:
        color = config[CONF_SET_COLOR]
        if len(color) == 3:
            cg.add(
                var.set_turn_on_color_rgb(
                    color[0] / 100.0, color[1] / 100.0, color[2] / 100.0
                )
            )
        else:
            cg.add(
                var.set_turn_on_color_rgbw(
                    color[0] / 100.0,
                    color[1] / 100.0,
                    color[2] / 100.0,
                    color[3] / 100.0,
                )
            )

    # RGB Order: explicit > auto-detect from chipset
    if CONF_RGB_ORDER in config:
        rgb_order = config[CONF_RGB_ORDER]
    else:
        rgb_order = DEFAULT_ORDER.get(chipset_name, RGBOrder.ORDER_GRB)
    cg.add(var.set_rgb_order(rgb_order))

    # Auto-compute RMT symbols if not set manually.
    # We must EXCLUDE SPI strips from the auto-budget calculation!
    if not is_spi:
        import esphome.core as _core_rmt
        import logging as _log_rmt
        rmt_sym = config.get(CONF_RMT_SYMBOLS, 0)
        if rmt_sym == 0:
            if "cfx_light_total" not in _core_rmt.CORE.data:
                # First call: count auto RMT strips AND subtract manually-set budget
                manual_total = sum(
                    lconf.get(CONF_RMT_SYMBOLS, 0)
                    for lconf in _core_rmt.CORE.config.get("light", [])
                    if lconf.get("platform", "") == "cfx_light"
                       and lconf.get(CONF_RMT_SYMBOLS, 0) != 0
                       and lconf.get(CONF_CHIPSET, "") not in SPI_CHIPSETS
                )
                n_auto = sum(
                    1 for lconf in _core_rmt.CORE.config.get("light", [])
                    if lconf.get("platform", "") == "cfx_light"
                       and lconf.get(CONF_RMT_SYMBOLS, 0) == 0
                       and lconf.get(CONF_CHIPSET, "") not in SPI_CHIPSETS
                )
                n_total_lights = n_auto + (1 if manual_total > 0 else 0)
                # Enforce per-chip light limit (RMT only)
                _esp32_variant = "ESP32"
                try:
                    from esphome.core import CORE as _c
                    _esp32_variant = str(_c.config.get("esp32", {}).get("variant", "ESP32")).upper().replace("-", "").replace("_", "")
                except Exception:
                    pass
                _n_all_rmt_lights = sum(
                    1 for lconf in _core_rmt.CORE.config.get("light", [])
                    if lconf.get("platform", "") == "cfx_light"
                       and lconf.get(CONF_CHIPSET, "") not in SPI_CHIPSETS
                )
                _max_lights = _MAX_LIGHTS.get(_esp32_variant, _MAX_LIGHTS_DEFAULT)
                if _n_all_rmt_lights > _max_lights:
                    _log_rmt.getLogger(__name__).error(
                        "CFXLight: %s supports max %d RMT light(s) but %d are declared. "
                        "Reduce the number of RMT cfx_light instances.",
                        _esp32_variant, _max_lights, _n_all_rmt_lights,
                    )
                _core_rmt.CORE.data["cfx_light_total"]   = max(n_auto, 1)
                _core_rmt.CORE.data["cfx_light_manual"]  = manual_total
                _log_rmt.getLogger(__name__).info(
                    "CFXLight: %d auto RMT strip(s), %d manual symbols reserved — "
                    "dividing remaining RMT budget evenly",
                    n_auto, manual_total,
                )
            n_total      = _core_rmt.CORE.data["cfx_light_total"]
            manual_used  = _core_rmt.CORE.data.get("cfx_light_manual", 0)
            rmt_sym = _get_rmt_symbols_auto(n_total, manual_used)
            _log_rmt.getLogger(__name__).info(
                "CFXLight: auto rmt_symbols=%d (%d strip(s))",
                rmt_sym, n_total,
            )
        cg.add(var.set_rmt_symbols(rmt_sym))

    if CONF_MAX_REFRESH_RATE in config:
        cg.add(var.set_max_refresh_rate(config[CONF_MAX_REFRESH_RATE]))

    default_transition_ms = config[CONF_DEFAULT_TRANSITION_LENGTH].total_milliseconds
    cg.add(var.set_default_transition_length(default_transition_ms))

    if CONF_VISUALIZER_IP in config:
        # Visualizer is an internal dev tool. Gated behind a build flag so
        # it compiles to zero code in production builds. The flag activates
        # the sendto() path inside write_state() and the private fields.
        cg.add_build_flag("-DCFX_VISUALIZER_ENABLED")
        cg.add(var.set_visualizer_ip(config[CONF_VISUALIZER_IP]))
        cg.add(var.set_visualizer_port(config[CONF_VISUALIZER_PORT]))
        cg.add(var.set_visualizer_enabled(True))

    # --- Root-level intro/outro defaults ---
    if "use_intro" in config:
        cg.add(var.set_default_intro_mode(config["use_intro"]))
    if "use_outro" in config:
        cg.add(var.set_default_outro_mode(config["use_outro"]))
    if "inout_dur" in config:
        intro_dur_ms = config["inout_dur"]
        intro_dur_s = float(intro_dur_ms.total_milliseconds) / 1000.0
        cg.add(var.set_default_intro_dur(intro_dur_s))
        cg.add(var.set_default_outro_dur(intro_dur_s))

    # --- Segment codegen ---
    for seg in segments:
        seg_id_obj = seg[CONF_SEGMENT_ID]
        seg_id_str = str(seg_id_obj)
        seg_start = seg[CONF_SEGMENT_START]
        seg_stop = seg[CONF_SEGMENT_STOP]
        seg_mirror = seg.get(CONF_SEGMENT_MIRROR, False)
        seg_intro = seg.get(CONF_SEGMENT_USE_INTRO, 0)
        seg_outro = seg.get(CONF_SEGMENT_USE_OUTRO, 0)

        seg_intro_dur = 0.0
        if CONF_SEGMENT_INTRO_DUR in seg:
            seg_intro_dur = (
                float(seg[CONF_SEGMENT_INTRO_DUR].total_milliseconds) / 1000.0
            )

        # Register segment definition on parent (for C++ access)
        cg.add(
            var.add_segment_def(
                seg_id_str, seg_start, seg_stop, seg_mirror,
                seg_intro, seg_outro, seg_intro_dur, seg_intro_dur
            )
        )

        # Phase 2: Create virtual segment light + independent LightState.
        # The LightState remains the HA/control shell, but active CFX segment
        # rendering is parent-owned at runtime by CFXLightOutput.
        vl = cg.new_Pvariable(
            seg[CONF_SEGMENT_OUTPUT_ID], var, seg_start, seg_stop, seg_id_str
        )

        # Prepare segment config WITHOUT effects to avoid ESPHome's internal collision
        seg_light_config = {
            CONF_ID: seg_id_obj,
            CONF_NAME: seg[CONF_SEGMENT_NAME] if CONF_SEGMENT_NAME in seg else seg_id_str,
            CONF_OUTPUT_ID: seg[CONF_SEGMENT_OUTPUT_ID],
            CONF_EFFECTS: [],  # Skip internal effect registration
            CONF_DEFAULT_TRANSITION_LENGTH: config.get(
                CONF_DEFAULT_TRANSITION_LENGTH,
                cv.positive_time_period_milliseconds("0ms"),
            ),
            "disabled_by_default": False,
            "internal": False,
        }
        if "restore_mode" in config:
            seg_light_config["restore_mode"] = config["restore_mode"]
        # Register the LightState (without effects)
        await light.register_light(vl, seg_light_config)
        light_state = await cg.get_variable(seg_id_obj)

        if CONF_SEGMENT_SET_BRIGHTNESS in seg:
            cg.add(vl.set_turn_on_brightness(seg[CONF_SEGMENT_SET_BRIGHTNESS]))
        if CONF_SEGMENT_SET_COLOR in seg:
            seg_color = seg[CONF_SEGMENT_SET_COLOR]
            if len(seg_color) == 3:
                cg.add(
                    vl.set_turn_on_color_rgb(
                        seg_color[0] / 100.0,
                        seg_color[1] / 100.0,
                        seg_color[2] / 100.0,
                    )
                )
            else:
                cg.add(
                    vl.set_turn_on_color_rgbw(
                        seg_color[0] / 100.0,
                        seg_color[1] / 100.0,
                        seg_color[2] / 100.0,
                        seg_color[3] / 100.0,
                    )
                )

        # ── Stub + Singleton pattern (CFX-058) ──────────────────────────────
        # Instead of creating 81 full CFXAddressableLightEffect objects per
        # segment (~60 B each = 4.9 KB), we create ONE real singleton effect
        # and 81 lightweight CFXEffectStub proxies (~36 B each).
        # Saves ~2.2 KB per segment (37% heap reduction).
        from esphome.core import ID as CoreID
        from esphome.components.cfx_effect import (
            cfx_effect_to_code, CFXAddressableLightEffect, CFXEffectStub,
            CFX_EFFECT_NAMES,
        )

        seg_idx = segments.index(seg)
        parent_id = config[CONF_ID].id

        # Phase 1: Create the singleton — one real effect per segment.
        # This is the only object that allocates CFXActivation + CFXRunner.
        singleton_str = f"{parent_id}_cfx_singleton_s{seg_idx}"
        singleton_id = CoreID(
            singleton_str, is_declaration=True, type=CFXAddressableLightEffect
        )
        singleton_var = cg.new_Pvariable(singleton_id, "CFX Segment Singleton")
        cg.add(singleton_var.set_virtual_segment(True))

        # Ensure the stub header is included in the generated C++ output
        cg.add_global(
            cg.RawExpression(
                '#include "esphome/components/cfx_effect/cfx_effect_stub.h"\n'
            )
        )

        # Phase 2: Create lightweight stubs for the HA effect dropdown.
        effect_vars = []
        for eff_idx, eff in enumerate(config.get(CONF_EFFECTS, [])):
            if not isinstance(eff, dict):
                continue
            if "addressable_cfx" not in eff:
                continue

            eff_conf = eff["addressable_cfx"]
            eff_num = eff_conf.get("effect_id", 0)

            # Resolve the display name (same logic as cfx_effect_to_code)
            name = eff_conf.get(CONF_NAME, "CFX Effect")
            if name == "CFX Effect" and eff_num in CFX_EFFECT_NAMES:
                name = CFX_EFFECT_NAMES[eff_num]

            stub_str = f"{parent_id}_cfx_stub_s{seg_idx}_e{eff_num}_{eff_idx}"
            stub_id = CoreID(
                stub_str, is_declaration=True, type=CFXEffectStub
            )
            stub_var = cg.new_Pvariable(stub_id, name, eff_num, singleton_var)
            effect_vars.append(stub_var)

        if effect_vars:
            cg.add(light_state.add_effects(effect_vars))

        # Register the segment with the parent output so it can sync on/off and brightness
        cg.add(var.add_segment_light_state(light_state))

    # --- Phase 3: Event Entity Setup (CFX-037) ---
    import re
    def _cfx_slugify(name: str) -> str:
        return re.sub(r'[^a-z0-9]+', '_', name.lower()).strip('_')
        
    import esphome.core as core
    progress_step = 10
    milestones = list(range(progress_step, 101, progress_step))
    if 100 not in milestones:
        milestones.append(100)
        
    tags_to_register = []

    if segments:
        # Segmented parents are not event-capable in HA; only register the
        # logical segment event entities.
        for seg in segments:
            seg_id_obj = seg[CONF_SEGMENT_ID]
            seg_name = seg.get(CONF_SEGMENT_NAME, "")
            seg_tag = _cfx_slugify(str(seg_name)) if seg_name else str(seg_id_obj.id)
            tags_to_register.append(seg_tag)
    else:
        parent_obj = config[CONF_ID]
        parent_name = config.get(CONF_NAME, "")
        parent_tag = _cfx_slugify(str(parent_name)) if parent_name else str(parent_obj.id)
        tags_to_register.append(parent_tag)
        
    for tag in tags_to_register:
        safe_id = re.sub(r'[^a-z0-9_]', '_', tag)
        eid = core.ID(
            f"cfx_events_{safe_id}",
            is_declaration=True,
            type=event.Event,
        )
        evar = cg.new_Pvariable(eid)
        core.CORE.component_ids.add(f"cfx_events_{safe_id}")
        
        event_types = (
            [f"cfx_start:{tag}", f"cfx_begin:{tag}",
             f"cfx_stop:{tag}",  f"cfx_complete:{tag}"]
            + [f"cfx_reach:{tag}:{m}" for m in milestones]
        )
        
        econf = {
            "id": eid,
            "name": f"CFX Events: {tag}",
            "icon": "mdi:animation-play",
            "disabled_by_default": False,
            "internal": False,
        }
        await event.register_event(evar, econf, event_types=event_types)
        
        # Register this strip's entity with the EventManager directly
        cg.add(cg.RawExpression(
            f'chimera_fx::CFXEventManager::get().register_strip_entity'
            f'("{tag}", {evar})'
        ))

