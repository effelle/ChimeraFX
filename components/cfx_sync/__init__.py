"""Authenticated ESP-NOW light-state synchronization for ChimeraFX."""

import hashlib

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    binary_sensor,
    espnow,
    light,
    number,
    select,
    switch,
)
from esphome.const import CONF_ID
from esphome.core import CORE, HexInt, TimePeriod, ID as CoreID
from esphome.final_validate import full_config

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = [
    "binary_sensor",
    "cfx_button",
    "cfx_effect_registry",
    "espnow",
    "hmac_sha256",
    "light",
    "number",
    "select",
    "switch",
]

try:
    from esphome.components.cfx_effect_registry import CFX_EFFECT_NAMES
except ImportError:
    from components.cfx_effect_registry import CFX_EFFECT_NAMES

try:
    from esphome.components import cfx_button
except ImportError:
    from components import cfx_button

CONF_ESPNOW_ID = "espnow_id"
CONF_INTERNAL_ESPNOW_ID = "_espnow_id"
CONF_ROLE = "role"
CONF_LIGHTS = "lights"
CONF_LOCAL_INPUT = "local_input"
CONF_REMOTE_INPUT = "remote_input"
CONF_PEER = "peer"
CONF_GROUP = "group"
CONF_KEY = "key"
CONF_HEARTBEAT = "heartbeat"
CONF_AUTO_ADD_PEER = "auto_add_peer"
CONF_EFFECT_CATALOGS = "_effect_catalogs"
CONF_CONTROL_IDS = "_control_ids"
CONF_EFFECTS = "effects"
CONF_EFFECT_ID = "effect_id"
CONF_NAME = "name"
CONF_PLATFORM = "platform"
CONF_SEGMENTS = "segments"
CONF_ADDRESSABLE_CFX = "addressable_cfx"
CONF_CONTROLS = "controls"
CONF_CTRL_EXCLUDE = "ctrl_exclude"
CONF_IS_RGBW = "is_rgbw"
CONF_IS_WRGB = "is_wrgb"
CONF_CHIPSET = "chipset"
CONF_FORCE_WHITE = "force_white"
CONF_INTRO = "intro"
CONF_OUTRO = "outro"
CONF_INOUT_DURATION = "inout_duration"
CONF_SPEED = "speed"
CONF_INTENSITY = "intensity"
CONF_MIRROR = "mirror"
CONF_PALETTE = "palette"

ROLE_LEADER = "leader"
ROLE_FOLLOWER = "follower"
ROLE_CONTROLLER = "controller"
EXCLUDE_SPEED = 1
EXCLUDE_INTENSITY = 2
EXCLUDE_PALETTE = 3
EXCLUDE_MIRROR = 4
EXCLUDE_INTRO = 5
EXCLUDE_FORCE_WHITE = 8

MIN_HEARTBEAT = TimePeriod(seconds=10)
MAX_HEARTBEAT = TimePeriod(minutes=5)
MAX_EFFECT_NAME_BYTES = 64
KEY_DERIVATION_PREFIX = b"CFX_SYNC_V1\x00"

cfx_sync_ns = cg.esphome_ns.namespace("cfx_sync")
CFXSyncComponent = cfx_sync_ns.class_("CFXSyncComponent", cg.Component)
CFXSyncRole = cfx_sync_ns.enum("CFXSyncRole")

ROLE_MAP = {
    ROLE_LEADER: CFXSyncRole.LEADER,
    ROLE_FOLLOWER: CFXSyncRole.FOLLOWER,
    ROLE_CONTROLLER: CFXSyncRole.CFX_SYNC_ROLE_CONTROLLER,
}

_LIGHTS_VALIDATOR = cv.ensure_list(cv.use_id(light.LightState))


def _normalize_lights(value):
    return _LIGHTS_VALIDATOR(value)


def _id_name(value):
    return getattr(value, "id", value)


def _find_effect_source_config(light_id, all_lights):
    target_id = _id_name(light_id)
    for light_config in all_lights:
        if _id_name(light_config.get(CONF_ID)) == target_id:
            return light_config

    for light_config in all_lights:
        if light_config.get(CONF_PLATFORM) != "cfx_light":
            continue
        for segment in light_config.get(CONF_SEGMENTS, []):
            if _id_name(segment.get(CONF_ID)) == target_id:
                return light_config
    return None


def _find_cfx_light_target(light_id, all_lights):
    target_id = _id_name(light_id)
    for light_config in all_lights:
        if light_config.get(CONF_PLATFORM) != "cfx_light":
            continue
        master_id = _id_name(light_config.get(CONF_ID))
        if master_id == target_id:
            return light_config, master_id, 0
        for index, segment in enumerate(light_config.get(CONF_SEGMENTS, [])):
            if _id_name(segment.get(CONF_ID)) == target_id:
                return light_config, master_id, index + 1
    return None


def _has_white_channel(light_config):
    return (
        light_config.get(CONF_IS_RGBW, False)
        or light_config.get(CONF_IS_WRGB, False)
        or light_config.get(CONF_CHIPSET) == "SK6812"
    )


def _extract_control_ids(light_id, all_lights):
    target = _find_cfx_light_target(light_id, all_lights)
    if target is None:
        return {}

    light_config, master_id, target_index = target
    if not light_config.get(CONF_CONTROLS, True):
        return {}

    has_segments = bool(light_config.get(CONF_SEGMENTS, []))
    is_effect_target = not has_segments or target_index > 0
    if not is_effect_target:
        return {}

    exclude = {int(value) for value in light_config.get(CONF_CTRL_EXCLUDE, [])}
    prefix = f"cfx_auto_ctrl_{master_id}_{target_index}"
    result = {}

    if EXCLUDE_FORCE_WHITE not in exclude and _has_white_channel(light_config):
        result[CONF_FORCE_WHITE] = f"{prefix}_force_white"
    if EXCLUDE_SPEED not in exclude:
        result[CONF_SPEED] = f"{prefix}_speed"
    if EXCLUDE_INTENSITY not in exclude:
        result[CONF_INTENSITY] = f"{prefix}_intensity"
    if EXCLUDE_MIRROR not in exclude:
        result[CONF_MIRROR] = f"{prefix}_mirror"
    if EXCLUDE_PALETTE not in exclude:
        result[CONF_PALETTE] = f"{prefix}_palette"
    if EXCLUDE_INTRO not in exclude:
        result[CONF_INTRO] = f"{prefix}_intro"
        result[CONF_OUTRO] = f"{prefix}_outro"
        result[CONF_INOUT_DURATION] = f"{prefix}_inout_dur"
    return result


def _resolve_cfx_effect_name(effect_config):
    effect_id = effect_config[CONF_EFFECT_ID]
    name = effect_config.get(CONF_NAME, "CFX Effect")
    if name == "CFX Effect":
        name = CFX_EFFECT_NAMES.get(effect_id, name)
    return name


def _validate_effect_name_bytes(name):
    name = cv.string_strict(name)
    if not name:
        raise cv.Invalid("effect name must not be empty")
    if len(name.encode("utf-8")) > MAX_EFFECT_NAME_BYTES:
        raise cv.Invalid(
            "effect name must be at most 64 UTF-8 bytes"
        )
    return name


def _extract_cfx_effect_catalog(light_config):
    catalog = []
    seen_pairs = set()
    seen_names = set()

    for configured_effect in light_config.get(CONF_EFFECTS, []):
        if not isinstance(configured_effect, dict):
            continue

        for platform, effect_config in configured_effect.items():
            if not isinstance(effect_config, dict):
                continue

            if platform == CONF_ADDRESSABLE_CFX:
                effect_id = effect_config[CONF_EFFECT_ID]
                effect_name = _validate_effect_name_bytes(
                    _resolve_cfx_effect_name(effect_config)
                )
                pair = (effect_id, effect_name)
                if pair in seen_pairs:
                    raise cv.Invalid(
                        f"duplicate cfx_sync effect {pair!r}"
                    )
            elif CONF_NAME in effect_config:
                effect_name = _validate_effect_name_bytes(
                    effect_config[CONF_NAME]
                )
                pair = None
            else:
                continue

            folded_name = effect_name.casefold()
            if folded_name == "none":
                raise cv.Invalid("effect name 'None' is reserved")
            if folded_name in seen_names:
                raise cv.Invalid(
                    "effect names on one light must be unique "
                    "case-insensitively"
                )

            seen_names.add(folded_name)
            if pair is not None:
                seen_pairs.add(pair)
                catalog.append(pair)

    return catalog


def _validate_role_lights(config):
    role = config[CONF_ROLE]
    lights = config.get(CONF_LIGHTS, [])
    if role == ROLE_CONTROLLER:
        if lights:
            raise cv.Invalid("cfx_sync controller cannot declare lights")
        if CONF_LOCAL_INPUT not in config:
            raise cv.Invalid("cfx_sync controller requires local_input")
        if CONF_REMOTE_INPUT in config:
            raise cv.Invalid(
                "remote_input can only be used with role: leader"
            )
    elif CONF_LOCAL_INPUT in config:
        raise cv.Invalid(
            "local_input can only be used with role: controller"
        )

    if role == ROLE_LEADER and len(lights) != 1:
        raise cv.Invalid("cfx_sync leader requires exactly one light")
    if role == ROLE_FOLLOWER and not lights:
        raise cv.Invalid("cfx_sync follower requires at least one light")
    if role == ROLE_FOLLOWER and CONF_REMOTE_INPUT in config:
        raise cv.Invalid("remote_input can only be used with role: leader")

    seen = set()
    for light_id in lights:
        light_name = light_id.id
        if light_name in seen:
            raise cv.Invalid(
                f"cfx_sync duplicate light id '{light_name}'"
            )
        seen.add(light_name)
    return config


def _validate_group(value):
    value = cv.string_strict(value)
    if not value:
        raise cv.Invalid("group must not be empty")
    encoded = value.encode("utf-8")
    if len(encoded) > 64:
        raise cv.Invalid("group must be at most 64 UTF-8 bytes")
    return value


def _validate_key(value):
    value = cv.string_strict(value)
    if len(value) < 8:
        raise cv.Invalid("key must contain at least 8 characters")
    if len(value.encode("utf-8")) > 64:
        raise cv.Invalid("key must be at most 64 UTF-8 bytes")
    return value


def _derive_key(value):
    return hashlib.sha256(
        KEY_DERIVATION_PREFIX + value.encode("utf-8")
    ).digest()


def _fnv1a_32(value):
    result = 0x811C9DC5
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 0x01000193) & 0xFFFFFFFF
    return result


def _final_validate(config):
    final_config = full_config.get()
    all_lights = final_config.get_config_for_path(["light"])
    effect_catalogs = []
    control_ids = []
    for light_id in config[CONF_LIGHTS]:
        source_config = _find_effect_source_config(light_id, all_lights)
        effect_catalogs.append(
            _extract_cfx_effect_catalog(source_config)
            if source_config is not None
            else []
        )
        control_ids.append(_extract_control_ids(light_id, all_lights))
    config[CONF_EFFECT_CATALOGS] = effect_catalogs
    config[CONF_CONTROL_IDS] = control_ids
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CFXSyncComponent),
            cv.GenerateID(CONF_INTERNAL_ESPNOW_ID): cv.use_id(
                espnow.ESPNowComponent
            ),
            cv.Required(CONF_ROLE): cv.one_of(
                ROLE_LEADER, ROLE_FOLLOWER, ROLE_CONTROLLER, lower=True
            ),
            cv.Optional(CONF_LIGHTS, default=[]): _normalize_lights,
            cv.Optional(CONF_LOCAL_INPUT): cv.use_id(binary_sensor.BinarySensor),
            cv.Optional(CONF_REMOTE_INPUT): cv.use_id(cfx_button.CFXButton),
            cv.Optional(CONF_ESPNOW_ID): cv.invalid(
                "espnow_id is no longer used; cfx_sync uses ESP-NOW "
                "automatically"
            ),
            cv.Optional(CONF_PEER): cv.invalid(
                "peer is no longer used; cfx_sync discovers devices by "
                "group and key"
            ),
            cv.Required(CONF_GROUP): _validate_group,
            cv.Required(CONF_KEY): _validate_key,
            cv.Optional(
                CONF_HEARTBEAT, default="30s"
            ): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=MIN_HEARTBEAT, max=MAX_HEARTBEAT),
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_role_lights,
    cv.only_on_esp32,
)

FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    espnow_var = await cg.get_variable(config[CONF_INTERNAL_ESPNOW_ID])
    if CORE.using_arduino:
        cg.add_library("WiFi", None)
    cg.add_define("USE_ESPNOW")
    cg.add(espnow_var.set_auto_add_peer(False))

    key_bytes = [HexInt(value) for value in _derive_key(config[CONF_KEY])]
    effect_catalogs = config.get(
        CONF_EFFECT_CATALOGS, [[] for _ in config[CONF_LIGHTS]]
    )
    control_ids = config.get(
        CONF_CONTROL_IDS, [{} for _ in config[CONF_LIGHTS]]
    )

    cg.add(var.set_espnow(espnow_var))
    for light_index, light_id in enumerate(config[CONF_LIGHTS]):
        light_var = await cg.get_variable(light_id)
        cg.add(var.add_light(light_var))
        for effect_id, effect_name in effect_catalogs[light_index]:
            cg.add(
                var.add_effect(light_index, effect_id, effect_name)
            )
        controls = control_ids[light_index]
        if CONF_FORCE_WHITE in controls:
            control = await cg.get_variable(
                CoreID(controls[CONF_FORCE_WHITE], type=switch.Switch)
            )
            cg.add(var.set_force_white_control(light_index, control))
        if CONF_INTRO in controls:
            control = await cg.get_variable(
                CoreID(controls[CONF_INTRO], type=select.Select)
            )
            cg.add(var.set_intro_control(light_index, control))
        if CONF_OUTRO in controls:
            control = await cg.get_variable(
                CoreID(controls[CONF_OUTRO], type=select.Select)
            )
            cg.add(var.set_outro_control(light_index, control))
        if CONF_INOUT_DURATION in controls:
            control = await cg.get_variable(
                CoreID(controls[CONF_INOUT_DURATION], type=number.Number)
            )
            cg.add(var.set_inout_duration_control(light_index, control))
        if CONF_SPEED in controls:
            control = await cg.get_variable(
                CoreID(controls[CONF_SPEED], type=number.Number)
            )
            cg.add(var.set_speed_control(light_index, control))
        if CONF_INTENSITY in controls:
            control = await cg.get_variable(
                CoreID(controls[CONF_INTENSITY], type=number.Number)
            )
            cg.add(var.set_intensity_control(light_index, control))
        if CONF_MIRROR in controls:
            control = await cg.get_variable(
                CoreID(controls[CONF_MIRROR], type=switch.Switch)
            )
            cg.add(var.set_mirror_control(light_index, control))
        if CONF_PALETTE in controls:
            control = await cg.get_variable(
                CoreID(controls[CONF_PALETTE], type=select.Select)
            )
            cg.add(var.set_palette_control(light_index, control))
    if CONF_LOCAL_INPUT in config:
        local_input = await cg.get_variable(config[CONF_LOCAL_INPUT])
        cg.add(var.set_local_input(local_input))
    if CONF_REMOTE_INPUT in config:
        remote_input = await cg.get_variable(config[CONF_REMOTE_INPUT])
        cg.add(var.set_remote_input(remote_input))
    cg.add(var.set_role(ROLE_MAP[config[CONF_ROLE]]))
    cg.add(var.set_group_hash(_fnv1a_32(config[CONF_GROUP])))
    cg.add(var.set_key(key_bytes))
    cg.add(
        var.set_heartbeat_ms(config[CONF_HEARTBEAT].total_milliseconds)
    )
