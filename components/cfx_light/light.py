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
CFX_LIGHT_SCHEMA_REV = 18

import esphome.codegen as cg
from esphome.components import light, event, sensor, select, text_sensor
import esphome.config_validation as cv
import esphome.core as core
import logging
import re
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
    CONF_ICON,
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
CONF_SACRIFICIAL_PIXEL = "sacrificial_pixel"
CONF_IS_WRGB = "is_wrgb"
CONF_DEFAULT_TRANSITION_LENGTH = "default_transition_length"
CONF_ALL_EFFECTS = "all_effects"
CONF_VISUALIZER_IP = "visualizer_ip"
CONF_VISUALIZER_PORT = "visualizer_port"
CONF_POWER_MONITOR = "power_monitor"
CONF_POWER_LIMIT = "power_limit"
CONF_CFX_POWER = "cfx_power"
CONF_MONITOR = "monitor"
CONF_LIMIT = "limit"
CONF_SUPPLY_VOLTAGE = "supply_voltage"
CONF_PSU_CURRENT_LIMIT = "psu_current_limit"
CONF_PSU_EFFICIENCY = "psu_efficiency"
CONF_POWER_FACTOR = "power_factor"
CONF_MAINS_VOLTAGE = "mains_voltage"
CONF_POWER_FACTOR_SENSOR = "power_factor_sensor"
CONF_MAINS_VOLTAGE_SENSOR = "mains_voltage_sensor"
CONF_IDLE_CURRENT_MA = "idle_current_ma"
CONF_RGB_CHANNEL_CURRENT_MA = "rgb_channel_current_ma"
CONF_WHITE_CHANNEL_CURRENT_MA = "white_channel_current_ma"
CONF_CONTROLLER_CURRENT_MA = "controller_current_ma"
CONF_SENSORS = "sensors"
CONF_DC_CURRENT = "dc_current"
CONF_DC_POWER = "dc_power"
CONF_AC_POWER = "ac_power"
CONF_APPARENT_POWER = "apparent_power"
CONF_AC_CURRENT = "ac_current"
CONF_ENERGY = "energy"
CONF_PSU_LOAD = "psu_load"
CONF_BUDGET_STATUS = "budget_status"
CONF_RESTORE = "restore"
CONF_REDUCTION = "reduction"
CONF_AUTO = "auto"
CONF_SAFE_HOLD_TIME = "safe_hold_time"

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
AUTO_LOAD = ["event", "cfx_effect", "sensor", "select", "text_sensor"]
_LOGGER = logging.getLogger(__name__)

cfx_light_ns = cg.esphome_ns.namespace("cfx_light")
CFXLightOutput = cfx_light_ns.class_(
    "CFXLightOutput", light.AddressableLight
)
CFXVirtualSegmentLight = cfx_light_ns.class_(
    "CFXVirtualSegmentLight", light.AddressableLight, cg.Component
)
CFXPowerManager = cfx_light_ns.class_("CFXPowerManager", cg.Component)
CFXPowerReductionSelect = cfx_light_ns.class_(
    "CFXPowerReductionSelect", select.Select
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
_SPI_SINGLE_HOST_VARIANTS = {
    "ESP32C2",
    "ESP32C3",
    "ESP32C5",
    "ESP32C6",
    "ESP32C61",
    "ESP32H2",
}

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
CONF_PARALLEL_GROUP = "parallel_group"
CONF_PARALLEL_STROBE_PIN = "parallel_strobe_pin"
CONF_SET_INTRO = "set_intro"
CONF_SET_OUTRO = "set_outro"
CONF_SET_INOUT_DUR = "set_inout_dur"
CONF_SET_BRIGHTNESS = "set_brightness"
CONF_SET_COLOR = "set_color"

SET_COLOR_SCHEMA = cv.All(
    cv.ensure_list(cv.int_range(min=0, max=100)),
    cv.Length(min=3, max=4),
)

PARALLEL_STROBE_PIN_DEFAULT = 22
PARALLEL_DC_PIN_DEFAULT = 21

def _current_ma(value):
    if isinstance(value, (int, float)):
        return float(value)
    return float(cv.current(value)) * 1000.0


def _current_to_ma(value):
    return float(cv.current(value)) * 1000.0


_ESTIMATED_CURRENT_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="A",
    icon="mdi:current-dc",
    accuracy_decimals=3,
    device_class="current",
    state_class="measurement",
)

_ESTIMATED_AC_CURRENT_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="A",
    icon="mdi:current-ac",
    accuracy_decimals=3,
    device_class="current",
    state_class="measurement",
)

_ESTIMATED_POWER_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="W",
    icon="mdi:flash",
    accuracy_decimals=1,
    device_class="power",
    state_class="measurement",
)

_ESTIMATED_APPARENT_POWER_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="VA",
    icon="mdi:flash-triangle",
    accuracy_decimals=1,
    device_class="apparent_power",
    state_class="measurement",
)

_ESTIMATED_ENERGY_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="kWh",
    icon="mdi:counter",
    accuracy_decimals=3,
    device_class="energy",
    state_class="total_increasing",
)

_ESTIMATED_PSU_LOAD_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="%",
    icon="mdi:gauge",
    accuracy_decimals=0,
    state_class="measurement",
)

_ESTIMATED_BUDGET_STATUS_SCHEMA = text_sensor.text_sensor_schema(
    icon="mdi:power-plug-battery",
)

DEFAULT_POWER_NODE_SENSORS = {
    CONF_DC_CURRENT: {CONF_NAME: "DC Current"},
    CONF_DC_POWER: {CONF_NAME: "DC Power"},
}

OPTIONAL_POWER_SENSOR_DEFAULTS = {
    CONF_AC_POWER: {CONF_NAME: "AC Power"},
    CONF_APPARENT_POWER: {CONF_NAME: "Apparent Power"},
    CONF_AC_CURRENT: {CONF_NAME: "AC Current"},
    CONF_ENERGY: {CONF_NAME: "Energy"},
    CONF_PSU_LOAD: {CONF_NAME: "PSU Load"},
    CONF_BUDGET_STATUS: {CONF_NAME: "Budget Status"},
}

POWER_SENSORS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_DC_CURRENT): _ESTIMATED_CURRENT_SENSOR_SCHEMA,
        cv.Optional(CONF_DC_POWER): _ESTIMATED_POWER_SENSOR_SCHEMA,
        cv.Optional(CONF_AC_POWER): _ESTIMATED_POWER_SENSOR_SCHEMA,
        cv.Optional(CONF_APPARENT_POWER): _ESTIMATED_APPARENT_POWER_SENSOR_SCHEMA,
        cv.Optional(CONF_AC_CURRENT): _ESTIMATED_AC_CURRENT_SENSOR_SCHEMA,
        cv.Optional(CONF_ENERGY): _ESTIMATED_ENERGY_SENSOR_SCHEMA,
        cv.Optional(CONF_PSU_LOAD): _ESTIMATED_PSU_LOAD_SENSOR_SCHEMA,
        cv.Optional(CONF_BUDGET_STATUS): _ESTIMATED_BUDGET_STATUS_SCHEMA,
    }
)


def _power_sensors_schema(config):
    if config is None:
        config = {}
    config = dict(config)
    for key, default_value in DEFAULT_POWER_NODE_SENSORS.items():
        config.setdefault(key, dict(default_value))
    for key, default_value in OPTIONAL_POWER_SENSOR_DEFAULTS.items():
        if key in config:
            sensor_conf = {} if config[key] is None else dict(config[key])
            merged = dict(default_value)
            merged.update(sensor_conf)
            config[key] = merged
    return POWER_SENSORS_SCHEMA(config)

POWER_MONITOR_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_UPDATE_INTERVAL, default="5s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SUPPLY_VOLTAGE, default=5.0): cv.float_range(min=0.1),
        cv.Optional(CONF_PSU_CURRENT_LIMIT, default="0A"): _current_to_ma,
        cv.Optional(CONF_PSU_EFFICIENCY, default=0.85): cv.float_range(min=0.01, max=1.0),
        cv.Optional(CONF_POWER_FACTOR, default=0.90): cv.float_range(min=0.01, max=1.0),
        cv.Required(CONF_MAINS_VOLTAGE): cv.float_range(min=1.0),
        cv.Optional(CONF_POWER_FACTOR_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_MAINS_VOLTAGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_IDLE_CURRENT_MA, default=1.0): _current_ma,
        cv.Optional(CONF_RGB_CHANNEL_CURRENT_MA, default=20.0): _current_ma,
        cv.Optional(CONF_WHITE_CHANNEL_CURRENT_MA, default=20.0): _current_ma,
        cv.Optional(CONF_CONTROLLER_CURRENT_MA, default=120.0): _current_ma,
        cv.Optional(CONF_SENSORS, default={}): _power_sensors_schema,
    }
)

_POWER_LIMIT_AUTO_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SAFE_HOLD_TIME, default="30s"): cv.positive_time_period_milliseconds,
    }
)


def POWER_LIMIT_AUTO_SCHEMA(config):
    if config is None:
        config = {}
    return _POWER_LIMIT_AUTO_SCHEMA(config)


_POWER_LIMIT_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_RESTORE, default=True): cv.boolean,
        cv.Optional(CONF_NAME, default="Power Reduction"): cv.string,
        cv.Optional(CONF_ICON, default="mdi:brightness-percent"): cv.icon,
        cv.Optional(CONF_AUTO): POWER_LIMIT_AUTO_SCHEMA,
    }
)

def POWER_LIMIT_SCHEMA(config):
    if config is None:
        config = {}
    config = dict(config)
    legacy_reduction = config.pop(CONF_REDUCTION, None)
    if legacy_reduction is not None:
        legacy_reduction = {} if legacy_reduction is None else dict(legacy_reduction)
        config.setdefault(CONF_NAME, legacy_reduction.get(CONF_NAME, "Power Reduction"))
        config.setdefault(
            CONF_ICON, legacy_reduction.get(CONF_ICON, "mdi:brightness-percent")
        )
    return _POWER_LIMIT_SCHEMA(config)


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


_CFX_LIGHT_LIMITS_DEFAULT = {"total": 4, "spi": 2, "rmt": 4}
_CFX_LIGHT_LIMITS = {
    # Classic ESP32 can expose two SPI hosts and four RMT outputs electrically,
    # but bench testing showed mixed transport timing is not release-grade.
    # ChimeraFX nodes are validated as either RMT-only or SPI-only.
    "ESP32": {"total": 4, "spi": 2, "rmt": 4},
    # S3 has one strong GDMA-backed RMT lane plus non-DMA fallback lanes.
    # Physical V1.41 testing showed 2 RMT outputs are release-grade; 3+ RMT
    # outputs can show visible artifacts even at lower LED counts.
    "ESP32S3": {"total": 2, "spi": 2, "rmt": 2},
    # P4 stays conservative until physically validated.
    "ESP32P4": {"total": 4, "spi": 2, "rmt": 2},
    # Single-host C-series targets keep the existing two-output budget.
    # ESP32-C3 RMT is experimental for V1.41. It has only 96 RMT symbols and
    # is not Classic-class; physical validation showed one tuned RMT output is
    # useful, while two 600-LED RMT outputs remain unstable.
    "ESP32C3": {"total": 1, "spi": 1, "rmt": 1},
    "ESP32C5": {"total": 2, "spi": 1, "rmt": 2},
    "ESP32C6": {"total": 2, "spi": 1, "rmt": 2},
    "ESP32C61": {"total": 2, "spi": 1, "rmt": 2},
    "ESP32H2": {"total": 2, "spi": 1, "rmt": 2},
    "ESP32C2": {"total": 2, "spi": 1, "rmt": 2},
}


_ESP32_VARIANT_MATCH_ORDER = (
    "ESP32C61",
    "ESP32C6",
    "ESP32C5",
    "ESP32C3",
    "ESP32C2",
    "ESP32H2",
    "ESP32P4",
    "ESP32S3",
    "ESP32S2",
    "ESP32",
)


def _normalize_esp32_token(value):
    return "".join(ch for ch in str(value).upper() if ch.isalnum())


def _canonical_esp32_variant(value, *, allow_short_tokens=False):
    token = _normalize_esp32_token(value)
    if not token:
        return None

    for variant in _ESP32_VARIANT_MATCH_ORDER:
        if token == variant or token.startswith(variant):
            return variant

    if allow_short_tokens:
        short_map = (
            ("C61", "ESP32C61"),
            ("C6", "ESP32C6"),
            ("C5", "ESP32C5"),
            ("C3", "ESP32C3"),
            ("C2", "ESP32C2"),
            ("H2", "ESP32H2"),
            ("P4", "ESP32P4"),
            ("S3", "ESP32S3"),
            ("S2", "ESP32S2"),
        )
        for needle, variant in short_map:
            if needle in token:
                return variant

    return None


def _variant_from_board_id(board):
    if board is None:
        return None

    try:
        from esphome.components.esp32.boards import BOARDS

        board_id = str(board)
        board_info = BOARDS.get(board_id)
        seen = set()
        while isinstance(board_info, str) and board_info not in seen:
            seen.add(board_info)
            board_info = BOARDS.get(board_info)
        if isinstance(board_info, dict):
            variant = _canonical_esp32_variant(board_info.get("variant"))
            if variant is not None:
                return variant
    except Exception:
        pass

    return _canonical_esp32_variant(board, allow_short_tokens=True)


def _get_esp32_variant():
    try:
        from esphome.components.esp32 import get_esp32_variant

        variant = _canonical_esp32_variant(get_esp32_variant())
        if variant is not None:
            return variant
    except Exception:
        pass

    try:
        from esphome.components.esp32 import get_board

        variant = _variant_from_board_id(get_board())
        if variant is not None:
            return variant
    except Exception:
        pass

    try:
        esp32_conf = CORE.config.get("esp32", {})
        variant = _canonical_esp32_variant(esp32_conf.get("variant"))
        if variant is not None:
            return variant
        variant = _variant_from_board_id(esp32_conf.get("board"))
        if variant is not None:
            return variant
    except Exception:
        pass

    try:
        esp32_conf = full_config.get().get_config_for_path(["esp32"])
        variant = _canonical_esp32_variant(esp32_conf.get("variant"))
        if variant is not None:
            return variant
        variant = _variant_from_board_id(esp32_conf.get("board"))
        if variant is not None:
            return variant
    except Exception:
        pass

    return "ESP32"


def _get_cfx_light_limits(variant=None):
    variant = _canonical_esp32_variant(variant) if variant is not None else _get_esp32_variant()
    if variant in _CFX_LIGHT_LIMITS:
        return _CFX_LIGHT_LIMITS[variant]
    return _CFX_LIGHT_LIMITS_DEFAULT


def _is_spi_cfx_light(config):
    return str(config.get(CONF_CHIPSET, "")).upper() in SPI_CHIPSETS


def _is_parallel_cfx_light(config):
    return bool(config.get(CONF_PARALLEL_GROUP))


def _is_legacy_rmt_cfx_light(config):
    return not _is_spi_cfx_light(config) and not _is_parallel_cfx_light(config)


def _pin_number_or_default(config, key, default):
    value = config.get(key)
    if isinstance(value, dict):
        return int(value[CONF_NUMBER])
    if value is None:
        return int(default)
    return int(value)


def _classic_native_spi_host(data_pin, clock_pin):
    # ESP32 Classic native pin groups:
    #   HSPI / SPI2: MOSI=GPIO13, SCLK=GPIO14
    #   VSPI / SPI3: MOSI=GPIO23, SCLK=GPIO18
    # The GPIO matrix can route either host to many pins, but native routing is
    # materially cleaner for high-rate clocked LED output on real wiring.
    if data_pin == 13 and clock_pin == 14:
        return "SPI2_HOST"
    if data_pin == 23 and clock_pin == 18:
        return "SPI3_HOST"
    return None


def _choose_spi_host_name(data_pin, clock_pin, used_hosts):
    variant = _get_esp32_variant()
    available_hosts = ["SPI2_HOST"]
    if variant not in _SPI_SINGLE_HOST_VARIANTS:
        available_hosts.append("SPI3_HOST")

    preferred = None
    if variant == "ESP32":
        preferred = _classic_native_spi_host(data_pin, clock_pin)

    if preferred in available_hosts and preferred not in used_hosts:
        return preferred

    for host in available_hosts:
        if host not in used_hosts:
            return host

    return available_hosts[0]


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
    variant = _get_esp32_variant()
    limits = _get_cfx_light_limits(variant)
    spi_count = sum(1 for lconf in cfx_lights if _is_spi_cfx_light(lconf))
    parallel_lights = [lconf for lconf in cfx_lights if _is_parallel_cfx_light(lconf)]
    legacy_rmt_count = sum(1 for lconf in cfx_lights if _is_legacy_rmt_cfx_light(lconf))
    _LOGGER.info(
        "CFXLight limits: variant=%s total=%d/%d spi=%d/%d rmt=%d/%d parallel=%d",
        variant,
        len(cfx_lights),
        limits["total"],
        spi_count,
        limits["spi"],
        legacy_rmt_count,
        limits["rmt"],
        len(parallel_lights),
    )

    if spi_count > 0 and (legacy_rmt_count > 0 or parallel_lights):
        raise cv.Invalid(
            "Mixed SPI and RMT cfx_light entries are not supported in this "
            "ChimeraFX release. Use either SPI-only or RMT-only per ESP32 node; "
            "move the other transport to a second controller."
        )

    if parallel_lights:
        if variant not in ("ESP32", "ESP32S3"):
            raise cv.Invalid(
                f"cfx_light parallel_group is only supported on ESP32 and ESP32-S3 "
                f"for V1; detected {variant}."
            )
        if legacy_rmt_count > 0:
            raise cv.Invalid(
                "cfx_light parallel_group cannot be mixed with legacy RMT "
                "cfx_light entries in the first parallel-driver release."
            )

        groups = {}
        for lconf in parallel_lights:
            groups.setdefault(str(lconf[CONF_PARALLEL_GROUP]), []).append(lconf)
        if len(groups) > 1:
            names = ", ".join(sorted(groups))
            raise cv.Invalid(
                f"Only one cfx_light parallel_group is supported in V1; got {names}."
            )

        group_name, group_lights = next(iter(groups.items()))
        max_parallel_lanes = 4
        if len(group_lights) > max_parallel_lanes:
            raise cv.Invalid(
                f"cfx_light parallel_group '{group_name}' has {len(group_lights)} "
                f"lanes; V1 supports at most {max_parallel_lanes} lanes."
            )

        chipsets = {str(lconf.get(CONF_CHIPSET, "")).upper() for lconf in group_lights}
        if any(chipset in SPI_CHIPSETS for chipset in chipsets):
            raise cv.Invalid(
                f"cfx_light parallel_group '{group_name}' is one-wire only; "
                "SPI chipsets cannot join a parallel group."
            )
        if len(chipsets) != 1:
            raise cv.Invalid(
                f"cfx_light parallel_group '{group_name}' cannot mix chipsets: "
                f"{', '.join(sorted(chipsets))}."
            )
        if chipsets != {"SK6812"}:
            raise cv.Invalid(
                f"cfx_light parallel_group '{group_name}' V1 supports SK6812 "
                f"RGBW lanes only; got {', '.join(sorted(chipsets))}."
            )

        num_leds = {int(lconf[CONF_NUM_LEDS]) for lconf in group_lights}
        if len(num_leds) != 1:
            raise cv.Invalid(
                f"cfx_light parallel_group '{group_name}' requires equal num_leds "
                f"for V1; got {', '.join(str(v) for v in sorted(num_leds))}."
            )

        protocol_shapes = {
            (
                bool(lconf.get(CONF_IS_RGBW, lconf.get(CONF_CHIPSET) in RGBW_CHIPSETS)),
                bool(lconf.get(CONF_IS_WRGB, False)),
            )
            for lconf in group_lights
        }
        if len(protocol_shapes) != 1:
            raise cv.Invalid(
                f"cfx_light parallel_group '{group_name}' cannot mix RGB/RGBW "
                "or WRGB protocol shapes."
            )
        lane_pins = [int(lconf[CONF_PIN][CONF_NUMBER]) for lconf in group_lights]
        if variant == "ESP32S3":
            reserved_pins = {
                _pin_number_or_default(
                    lconf, CONF_PARALLEL_STROBE_PIN, PARALLEL_STROBE_PIN_DEFAULT
                )
                for lconf in group_lights
            }
            if len(reserved_pins) != 1:
                raise cv.Invalid(
                    f"cfx_light parallel_group '{group_name}' must use one shared "
                    f"{CONF_PARALLEL_STROBE_PIN}; got {', '.join(str(v) for v in sorted(reserved_pins))}."
                )
            strobe_pin = next(iter(reserved_pins))
            if strobe_pin == PARALLEL_DC_PIN_DEFAULT:
                raise cv.Invalid(
                    f"cfx_light parallel_group '{group_name}' cannot use the same "
                    f"GPIO for {CONF_PARALLEL_STROBE_PIN} and the internal I80 "
                    f"D/C reservation GPIO{PARALLEL_DC_PIN_DEFAULT}."
                )
            if strobe_pin in lane_pins or PARALLEL_DC_PIN_DEFAULT in lane_pins:
                raise cv.Invalid(
                    f"cfx_light parallel_group '{group_name}' reserves GPIO{strobe_pin} "
                    f"for internal I80 WR/strobe and GPIO{PARALLEL_DC_PIN_DEFAULT} "
                    "for internal I80 D/C; lane pins must not use either GPIO."
                )
        return config

    if len(cfx_lights) > limits["total"]:
        raise cv.Invalid(
            f"Too many cfx_light entries for {variant}: {len(cfx_lights)} "
            f"(max {limits['total']} total: {limits['spi']} SPI + {limits['rmt']} RMT)"
        )
    if spi_count > limits["spi"]:
        raise cv.Invalid(
            f"Too many SPI cfx_light entries for {variant}: {spi_count} "
            f"(max {limits['spi']})"
        )
    if legacy_rmt_count > limits["rmt"]:
        raise cv.Invalid(
            f"Too many RMT cfx_light entries for {variant}: {legacy_rmt_count} "
            f"(max {limits['rmt']})"
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


def _reject_legacy_power_keys(config):
    if CONF_POWER_MONITOR in config:
        raise cv.Invalid(
            "`power_monitor` moved out of `cfx_light`. Use top-level "
            "`cfx_power: monitor:` instead."
        )
    if CONF_POWER_LIMIT in config:
        raise cv.Invalid(
            "`power_limit` moved out of `cfx_light`. Use top-level "
            "`cfx_power: limit:` instead."
        )
    return config

def _inject_all_effects(config):
    """If all_effects is true, inject synthetic addressable_cfx entries from
    the CFX_EFFECTS Python registry in cfx_effect/__init__.py.
    User-defined effects with the same name take priority (overrides)."""
    chipset = str(config.get(CONF_CHIPSET, "")).upper()
    light_update_interval = "14ms" if chipset in SPI_CHIPSETS else None

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
    _reject_legacy_power_keys,
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
            cv.Optional(CONF_PARALLEL_GROUP): cv.string,
            cv.Optional(CONF_PARALLEL_STROBE_PIN): pins.internal_gpio_output_pin_schema,
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
            cv.Optional(CONF_SACRIFICIAL_PIXEL, default=False): cv.boolean,
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
    _reserve_cfx_light_component_slot(config)
    chipset = config[CONF_CHIPSET]
    is_spi = chipset in SPI_CHIPSETS
    parallel_group = config.get(CONF_PARALLEL_GROUP)
    if parallel_group is not None:
        parallel_group = str(parallel_group).strip()
        if not parallel_group:
            raise cv.Invalid(f"'{CONF_PARALLEL_GROUP}' cannot be empty")
        config[CONF_PARALLEL_GROUP] = parallel_group

    if is_spi:
        if parallel_group is not None:
            raise cv.Invalid(
                f"'{CONF_PARALLEL_GROUP}' is only supported by one-wire chipsets "
                f"(WS2812X, SK6812, WS2811), not SPI chipset {chipset}."
            )
        if CONF_PIN in config:
            raise cv.Invalid(f"'{CONF_PIN}' cannot be used with SPI chipset {chipset}")
        if CONF_PARALLEL_STROBE_PIN in config:
            raise cv.Invalid(
                f"'{CONF_PARALLEL_STROBE_PIN}' is only supported with "
                f"'{CONF_PARALLEL_GROUP}'."
            )
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
        if config.get(CONF_SACRIFICIAL_PIXEL, False):
            raise cv.Invalid(
                f"'{CONF_SACRIFICIAL_PIXEL}' is only supported by RMT chipsets "
                f"(WS2812X, SK6812, WS2811)."
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
        if parallel_group is not None:
            if config.get(CONF_RMT_SYMBOLS, 0) != 0:
                raise cv.Invalid(
                    f"'{CONF_RMT_SYMBOLS}' is only used by legacy RMT. "
                    f"Remove it from cfx_light entries using '{CONF_PARALLEL_GROUP}'."
                )
            if config.get(CONF_SACRIFICIAL_PIXEL, False):
                raise cv.Invalid(
                    f"'{CONF_SACRIFICIAL_PIXEL}' is only supported by legacy RMT "
                    f"in V1. Remove it from cfx_light entries using "
                    f"'{CONF_PARALLEL_GROUP}'."
                )
        else:
            if CONF_PARALLEL_STROBE_PIN in config:
                raise cv.Invalid(
                    f"'{CONF_PARALLEL_STROBE_PIN}' is only supported with "
                    f"'{CONF_PARALLEL_GROUP}'."
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
    "ESP32C61": {"total":  96, "block": 48},
    "ESP32H2": {"total":  96, "block": 48},
}
_RMT_DEFAULT_BUDGET = {"total": 512, "block": 64}  # conservative fallback

def _get_rmt_symbols_auto(n_strips: int, manual_reserved: int = 0) -> int:
    """Compute the per-strip RMT symbol count for auto-configured strips.
    Subtracts manually-set allocations from the hardware total before dividing,
    floors to the nearest block boundary, and enforces a one-block minimum.
    Emits a compile-time error if the total allocation exceeds hardware capacity."""
    import logging as _log
    variant = _get_esp32_variant()

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


_POWER_MANAGER_DATA_KEY = "cfx_power_manager_var"
_COMPONENT_RESERVE_DATA_KEY = "cfx_light_component_capacity_reserves"


def _reserve_cfx_light_component_slot(config):
    """Reserve App component capacity for the hidden output component.

    ESPHome sizes its static component vector from CORE.component_ids before
    generated C++ runs. cfx_light creates a public LightState plus a hidden
    AddressableLight output component, and some ESPHome versions undercount the
    hidden output in larger configs. Reserve one extra slot per configured
    cfx_light so later outputs are not dropped from Application::setup().
    """
    reserve_key = getattr(config.get(CONF_OUTPUT_ID), "id", None)
    if reserve_key is None:
        reserve_key = getattr(config.get(CONF_ID), "id", None)
    if reserve_key is None:
        pin_conf = config.get(CONF_PIN) or config.get(CONF_DATA_PIN) or {}
        reserve_key = f"{config.get(CONF_NAME, 'unnamed')}_{pin_conf}"

    reserves = CORE.data.setdefault(_COMPONENT_RESERVE_DATA_KEY, set())
    reserve_key = str(reserve_key)
    if reserve_key in reserves:
        return
    reserves.add(reserve_key)
    safe_key = re.sub(r"[^a-zA-Z0-9_]", "_", reserve_key)
    core.CORE.component_ids.add(f"cfx_light_output_capacity_reserve_{safe_key}")


def _cfx_power_config():
    return CORE.config.get(CONF_CFX_POWER) or {}


def _power_monitor_config():
    return _cfx_power_config().get(CONF_MONITOR)


def _power_limit_config():
    power_conf = _cfx_power_config()
    if CONF_LIMIT in power_conf:
        return power_conf[CONF_LIMIT]
    if CONF_MONITOR in power_conf:
        return POWER_LIMIT_SCHEMA({})
    return None


def _cfx_light_configs():
    return [
        lconf
        for lconf in CORE.config.get("light", [])
        if lconf.get("platform", "") == "cfx_light"
    ]


def _first_power_monitor_config():
    return _power_monitor_config()


def _first_power_limit_config():
    return _power_limit_config()


def _power_sensor_configured():
    return _power_monitor_config() is not None


def _first_node_power_sensor_config(key):
    monitor = _power_monitor_config()
    if not monitor:
        return None
    return monitor.get(CONF_SENSORS, {}).get(key)


def _power_manager_needed():
    return _power_sensor_configured() or _first_power_limit_config() is not None


def _with_sensor_defaults(user_conf, default_id, default_name):
    conf = {
        CONF_ID: cv.declare_id(sensor.Sensor)(default_id),
        CONF_NAME: default_name,
        "disabled_by_default": False,
        "force_update": False,
    }
    if user_conf:
        conf.update(user_conf)
    return conf


async def _new_power_sensor(user_conf, default_id, default_name):
    conf = _with_sensor_defaults(user_conf, default_id, default_name)
    var = cg.new_Pvariable(conf[CONF_ID])
    core.CORE.component_ids.add(conf[CONF_ID].id)
    await sensor.register_sensor(var, conf)
    return var


async def _new_power_text_sensor(user_conf, default_id, default_name):
    conf = {
        CONF_ID: cv.declare_id(text_sensor.TextSensor)(default_id),
        CONF_NAME: default_name,
        "disabled_by_default": False,
    }
    if user_conf:
        conf.update(user_conf)
    var = cg.new_Pvariable(conf[CONF_ID])
    core.CORE.component_ids.add(conf[CONF_ID].id)
    await text_sensor.register_text_sensor(var, conf)
    return var


async def _ensure_power_manager():
    manager = CORE.data.get(_POWER_MANAGER_DATA_KEY)
    if manager is not None:
        return manager

    manager_id = core.ID("cfx_power_manager", is_declaration=True, type=CFXPowerManager)
    manager = cg.new_Pvariable(manager_id)
    CORE.data[_POWER_MANAGER_DATA_KEY] = manager
    core.CORE.component_ids.add("cfx_power_manager")
    await cg.register_component(manager, {CONF_ID: manager_id})

    monitor_conf = _first_power_monitor_config()
    if monitor_conf is not None and _power_sensor_configured():
        cg.add(
            manager.configure_monitor(
                monitor_conf[CONF_UPDATE_INTERVAL].total_milliseconds,
                monitor_conf[CONF_SUPPLY_VOLTAGE],
                monitor_conf[CONF_PSU_CURRENT_LIMIT],
                monitor_conf[CONF_PSU_EFFICIENCY],
                monitor_conf[CONF_POWER_FACTOR],
                monitor_conf[CONF_MAINS_VOLTAGE],
                monitor_conf[CONF_CONTROLLER_CURRENT_MA],
            )
        )
        mains_voltage_sensor = cg.nullptr
        if CONF_MAINS_VOLTAGE_SENSOR in monitor_conf:
            mains_voltage_sensor = await cg.get_variable(
                monitor_conf[CONF_MAINS_VOLTAGE_SENSOR]
            )
        power_factor_sensor = cg.nullptr
        if CONF_POWER_FACTOR_SENSOR in monitor_conf:
            power_factor_sensor = await cg.get_variable(
                monitor_conf[CONF_POWER_FACTOR_SENSOR]
            )
        cg.add(manager.set_meter_sensors(mains_voltage_sensor, power_factor_sensor))
        dc_current = cg.nullptr
        dc_current_conf = _first_node_power_sensor_config(CONF_DC_CURRENT)
        if dc_current_conf is not None:
            dc_current = await _new_power_sensor(
                dc_current_conf,
                "cfx_estimated_dc_current",
                "CFX DC Current",
            )
        dc_power = cg.nullptr
        dc_power_conf = _first_node_power_sensor_config(CONF_DC_POWER)
        if dc_power_conf is not None:
            dc_power = await _new_power_sensor(
                dc_power_conf,
                "cfx_estimated_dc_power",
                "CFX DC Power",
            )
        ac_power = cg.nullptr
        ac_power_conf = _first_node_power_sensor_config(CONF_AC_POWER)
        if ac_power_conf is not None:
            ac_power = await _new_power_sensor(
                ac_power_conf,
                "cfx_estimated_ac_power",
                "CFX AC Power Demand",
            )
        apparent_power = cg.nullptr
        apparent_power_conf = _first_node_power_sensor_config(CONF_APPARENT_POWER)
        if apparent_power_conf is not None:
            apparent_power = await _new_power_sensor(
                apparent_power_conf,
                "cfx_estimated_apparent_power",
                "CFX Apparent Power Demand",
            )
        ac_current = cg.nullptr
        ac_current_conf = _first_node_power_sensor_config(CONF_AC_CURRENT)
        if ac_current_conf is not None:
            ac_current = await _new_power_sensor(
                ac_current_conf,
                "cfx_estimated_ac_current",
                "CFX AC Current Demand",
            )
        energy = cg.nullptr
        energy_conf = _first_node_power_sensor_config(CONF_ENERGY)
        if energy_conf is not None:
            energy = await _new_power_sensor(
                energy_conf,
                "cfx_estimated_energy",
                "CFX Energy",
            )
        psu_load = cg.nullptr
        psu_load_conf = _first_node_power_sensor_config(CONF_PSU_LOAD)
        if psu_load_conf is not None:
            psu_load = await _new_power_sensor(
                psu_load_conf,
                "cfx_estimated_psu_load",
                "CFX PSU Load",
            )
        budget_status = cg.nullptr
        budget_status_conf = _first_node_power_sensor_config(CONF_BUDGET_STATUS)
        if budget_status_conf is not None:
            budget_status = await _new_power_text_sensor(
                budget_status_conf,
                "cfx_estimated_budget_status",
                "CFX Power Budget Status",
            )
        cg.add(
            manager.set_node_sensors(
                dc_current, dc_power, ac_power, apparent_power, ac_current,
                energy, psu_load, budget_status
            )
        )

    limit_conf = _first_power_limit_config()
    if limit_conf is not None:
        cg.add(
            manager.configure_reduction(
                limit_conf[CONF_RESTORE],
                800,
            )
        )
        if CONF_AUTO in limit_conf:
            cg.add(
                manager.configure_auto_reduction(
                    limit_conf[CONF_AUTO][CONF_SAFE_HOLD_TIME].total_milliseconds,
                )
            )
        reduction_id = core.ID(
            "cfx_power_reduction", is_declaration=True, type=CFXPowerReductionSelect
        )
        reduction_var = cg.new_Pvariable(reduction_id)
        core.CORE.component_ids.add("cfx_power_reduction")
        reduction_conf = {
            CONF_ID: reduction_id,
            CONF_NAME: limit_conf[CONF_NAME],
            CONF_ICON: limit_conf[CONF_ICON],
            "disabled_by_default": False,
        }
        await select.register_select(
            reduction_var, reduction_conf,
            options=[
                "0%", "10%", "20%", "30%", "40%",
                "50%", "60%", "70%", "80%", "90%"
            ]
        )
        cg.add(manager.set_reduction_select(reduction_var))

    return manager


async def _register_power_output(var, config):
    if not _power_manager_needed():
        return

    manager = await _ensure_power_manager()
    monitor_conf = _power_monitor_config() or {}

    cg.add(
        manager.register_output(
            var,
            config.get(CONF_NAME, config[CONF_ID].id),
            monitor_conf.get(CONF_IDLE_CURRENT_MA, 1.0),
            monitor_conf.get(CONF_RGB_CHANNEL_CURRENT_MA, 20.0),
            monitor_conf.get(CONF_WHITE_CHANNEL_CURRENT_MA, 20.0),
        )
    )


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    _LOGGER.debug(
        "CFXLight codegen: light_id=%s output_id=%s name=%s chipset=%s pin=%s",
        config[CONF_ID],
        config[CONF_OUTPUT_ID],
        config.get(CONF_NAME, config[CONF_ID].id),
        config[CONF_CHIPSET],
        config.get(CONF_PIN),
    )

    segments = config.get(CONF_SEGMENTS, [])
    light_config = config
    # Keep the user-facing light id for light.register_light(), but register
    # the hardware output component under its generated output_id.
    component_config = dict(config)
    component_config[CONF_ID] = config[CONF_OUTPUT_ID]
    if segments:
        # --- Phase 2: Per-segment light entities ---
        # The segmented parent keeps its own master LightState so ESPHome state
        # sync, controls, and debug routing behave the same as whole strips.
        master_config = dict(light_config)
        master_config[CONF_EFFECTS] = []  # No effects on master
        await light.register_light(var, master_config)
        light_state = await cg.get_variable(config[CONF_ID])
        cg.add(var.set_master_light_state(light_state))
        await cg.register_component(var, component_config)
    else:
        # No segments: original single-light behavior
        await light.register_light(var, light_config)
        light_state = await cg.get_variable(config[CONF_ID])
        cg.add(var.set_master_light_state(light_state))
        await cg.register_component(var, component_config)

    # --- Hardware configuration (always) ---
    cg.add(var.set_num_leds(config[CONF_NUM_LEDS]))
    cg.add(var.set_sacrificial_pixel(config[CONF_SACRIFICIAL_PIXEL]))
    chipset_name = config[CONF_CHIPSET]
    cg.add(var.set_chipset(CHIPSETS[chipset_name]))
    
    is_spi = chipset_name in SPI_CHIPSETS
    is_parallel = bool(config.get(CONF_PARALLEL_GROUP))
    
    # Re-enable ESP-IDF's SPI and RMT drivers unconditionally
    # because cfx_light.h includes both <driver/spi_master.h> and <driver/rmt_tx.h>
    try:
        from esphome.components.esp32 import include_builtin_idf_component
        include_builtin_idf_component("esp_driver_spi")
        include_builtin_idf_component("esp_driver_rmt")
        if is_parallel and _get_esp32_variant() == "ESP32S3":
            include_builtin_idf_component("esp_lcd")
            cg.add_define("CFX_PARALLEL_I80_ENABLED")
    except ImportError:
        pass

    if is_spi:
        cg.add(var.set_transport(cfx_light_ns.enum("CFXTransport").TRANSPORT_SPI))
        data_pin = config[CONF_DATA_PIN][CONF_NUMBER]
        clock_pin = config[CONF_CLOCK_PIN][CONF_NUMBER]
        cg.add(var.set_spi_data_pin(data_pin))
        cg.add(var.set_spi_clock_pin(clock_pin))
        cg.add(var.set_spi_speed_hz(config.get(CONF_SPI_SPEED, 10000000)))
        
        # Get or create SPI host registry counter in CORE.data
        registry = CORE.data.get(_SPI_HOST_REGISTRY_KEY, [])
        host_name = _choose_spi_host_name(data_pin, clock_pin, registry)
        cg.add(var.set_spi_host(SPI_HOSTS[host_name]))
        registry.append(host_name)
        CORE.data[_SPI_HOST_REGISTRY_KEY] = registry
    elif is_parallel:
        cg.add(var.set_transport(cfx_light_ns.enum("CFXTransport").TRANSPORT_PARALLEL))
        cg.add(var.set_pin(config[CONF_PIN][CONF_NUMBER]))
        cg.add(var.set_parallel_group(config[CONF_PARALLEL_GROUP]))
        all_lights = CORE.config.get("light", [])
        group_lights = [
            lconf
            for lconf in all_lights
            if lconf.get("platform", "") == "cfx_light"
            and lconf.get(CONF_PARALLEL_GROUP) == config[CONF_PARALLEL_GROUP]
        ]
        lane_pins = [lconf[CONF_PIN][CONF_NUMBER] for lconf in group_lights]
        lane_index = lane_pins.index(config[CONF_PIN][CONF_NUMBER])
        cg.add(var.set_parallel_lane_index(lane_index))
        cg.add(var.set_parallel_lane_count(len(lane_pins)))
        cg.add(
            var.set_parallel_strobe_pin(
                _pin_number_or_default(
                    config, CONF_PARALLEL_STROBE_PIN, PARALLEL_STROBE_PIN_DEFAULT
                )
            )
        )
        cg.add(
            var.set_parallel_dc_pin(
                PARALLEL_DC_PIN_DEFAULT
            )
        )
        for idx, pin in enumerate(lane_pins):
            cg.add(var.set_parallel_lane_pin(idx, pin))
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
    if not is_spi and not is_parallel:
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
                       and not _is_spi_cfx_light(lconf)
                )
                n_auto = sum(
                    1 for lconf in _core_rmt.CORE.config.get("light", [])
                    if lconf.get("platform", "") == "cfx_light"
                       and lconf.get(CONF_RMT_SYMBOLS, 0) == 0
                       and not _is_spi_cfx_light(lconf)
                )
                n_total_lights = n_auto + (1 if manual_total > 0 else 0)
                # Enforce per-chip light limit (RMT only)
                _esp32_variant = _get_esp32_variant()
                _n_all_rmt_lights = sum(
                    1 for lconf in _core_rmt.CORE.config.get("light", [])
                    if lconf.get("platform", "") == "cfx_light"
                       and not _is_spi_cfx_light(lconf)
                )
                _max_lights = _get_cfx_light_limits(_esp32_variant)["rmt"]
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

    await _register_power_output(var, config)

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
    for seg_idx, seg in enumerate(segments):
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

