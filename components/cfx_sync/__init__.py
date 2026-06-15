"""Authenticated ESP-NOW light-state synchronization for ChimeraFX."""

import ast
from functools import lru_cache
import hashlib
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import espnow, light
from esphome.const import CONF_ID
from esphome.core import HexInt, TimePeriod
from esphome.final_validate import full_config

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["esp32", "espnow", "light"]
AUTO_LOAD = ["hmac_sha256"]

CONF_ESPNOW_ID = "espnow_id"
CONF_ROLE = "role"
CONF_LIGHTS = "lights"
CONF_PEER = "peer"
CONF_GROUP = "group"
CONF_KEY = "key"
CONF_HEARTBEAT = "heartbeat"
CONF_AUTO_ADD_PEER = "auto_add_peer"
CONF_EFFECT_CATALOGS = "_effect_catalogs"
CONF_EFFECTS = "effects"
CONF_EFFECT_ID = "effect_id"
CONF_NAME = "name"
CONF_PLATFORM = "platform"
CONF_SEGMENTS = "segments"
CONF_ADDRESSABLE_CFX = "addressable_cfx"

ROLE_LEADER = "leader"
ROLE_FOLLOWER = "follower"

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


@lru_cache(maxsize=1)
def _load_cfx_effect_names():
    effect_module = (
        Path(__file__).resolve().parent.parent
        / "cfx_effect"
        / "__init__.py"
    )
    module = ast.parse(effect_module.read_text(encoding="utf-8"))
    for statement in module.body:
        if not isinstance(statement, ast.Assign):
            continue
        if any(
            isinstance(target, ast.Name)
            and target.id == "CFX_EFFECT_NAMES"
            for target in statement.targets
        ):
            return ast.literal_eval(statement.value)
    raise RuntimeError("CFX_EFFECT_NAMES registry not found")


def _resolve_cfx_effect_name(effect_config):
    effect_id = effect_config[CONF_EFFECT_ID]
    name = effect_config.get(CONF_NAME, "CFX Effect")
    if name == "CFX Effect":
        name = _load_cfx_effect_names().get(effect_id, name)
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

        for effect_config in configured_effect.values():
            if (
                isinstance(effect_config, dict)
                and CONF_NAME in effect_config
            ):
                _validate_effect_name_bytes(effect_config[CONF_NAME])

        if CONF_ADDRESSABLE_CFX not in configured_effect:
            continue

        effect_config = configured_effect[CONF_ADDRESSABLE_CFX]
        effect_id = effect_config[CONF_EFFECT_ID]
        effect_name = _validate_effect_name_bytes(
            _resolve_cfx_effect_name(effect_config)
        )
        if effect_name.casefold() == "none":
            raise cv.Invalid("effect name 'None' is reserved")

        pair = (effect_id, effect_name)
        if pair in seen_pairs:
            raise cv.Invalid(
                f"duplicate cfx_sync effect {pair!r}"
            )

        folded_name = effect_name.casefold()
        if folded_name in seen_names:
            raise cv.Invalid(
                "effect names on one light must be unique "
                "case-insensitively"
            )

        seen_pairs.add(pair)
        seen_names.add(folded_name)
        catalog.append(pair)

    return catalog


def _validate_role_lights(config):
    lights = config[CONF_LIGHTS]
    if config[CONF_ROLE] == ROLE_LEADER and len(lights) != 1:
        raise cv.Invalid("cfx_sync leader requires exactly one light")
    if config[CONF_ROLE] == ROLE_FOLLOWER and not lights:
        raise cv.Invalid("cfx_sync follower requires at least one light")

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
    espnow_config = final_config.get_config_for_path(["espnow"])
    if espnow_config.get(CONF_AUTO_ADD_PEER, False):
        raise cv.Invalid(
            "cfx_sync requires espnow.auto_add_peer to remain false"
        )

    all_lights = final_config.get_config_for_path(["light"])
    effect_catalogs = []
    for light_id in config[CONF_LIGHTS]:
        source_config = _find_effect_source_config(light_id, all_lights)
        effect_catalogs.append(
            _extract_cfx_effect_catalog(source_config)
            if source_config is not None
            else []
        )
    config[CONF_EFFECT_CATALOGS] = effect_catalogs
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CFXSyncComponent),
            cv.Required(CONF_ESPNOW_ID): cv.use_id(espnow.ESPNowComponent),
            cv.Required(CONF_ROLE): cv.one_of(
                ROLE_LEADER, ROLE_FOLLOWER, lower=True
            ),
            cv.Required(CONF_LIGHTS): _normalize_lights,
            cv.Required(CONF_PEER): cv.mac_address,
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

    espnow_var = await cg.get_variable(config[CONF_ESPNOW_ID])
    peer = config[CONF_PEER]
    peer_bytes = [HexInt(value) for value in peer.parts]
    key_bytes = [HexInt(value) for value in _derive_key(config[CONF_KEY])]
    effect_catalogs = config.get(
        CONF_EFFECT_CATALOGS, [[] for _ in config[CONF_LIGHTS]]
    )

    cg.add(var.set_espnow(espnow_var))
    for light_index, light_id in enumerate(config[CONF_LIGHTS]):
        light_var = await cg.get_variable(light_id)
        cg.add(var.add_light(light_var))
        for effect_id, effect_name in effect_catalogs[light_index]:
            cg.add(
                var.add_effect(light_index, effect_id, effect_name)
            )
    cg.add(var.set_role(ROLE_MAP[config[CONF_ROLE]]))
    cg.add(var.set_peer(peer_bytes))
    cg.add(var.set_group_hash(_fnv1a_32(config[CONF_GROUP])))
    cg.add(var.set_key(key_bytes))
    cg.add(
        var.set_heartbeat_ms(config[CONF_HEARTBEAT].total_milliseconds)
    )

    # Register the single configured peer with ESPHome's ESP-NOW component.
    cg.add(espnow_var.add_peer(peer.parts))
