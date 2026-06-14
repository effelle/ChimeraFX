"""Authenticated ESP-NOW light-state synchronization for ChimeraFX."""

import hashlib

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

ROLE_LEADER = "leader"
ROLE_FOLLOWER = "follower"

MIN_HEARTBEAT = TimePeriod(seconds=10)
MAX_HEARTBEAT = TimePeriod(minutes=5)
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
    espnow_config = full_config.get().get_config_for_path(["espnow"])
    if espnow_config.get(CONF_AUTO_ADD_PEER, False):
        raise cv.Invalid(
            "cfx_sync requires espnow.auto_add_peer to remain false"
        )
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
    light_var = await cg.get_variable(config[CONF_LIGHT_ID])
    peer = config[CONF_PEER]
    peer_bytes = [HexInt(value) for value in peer.parts]
    key_bytes = [HexInt(value) for value in _derive_key(config[CONF_KEY])]

    cg.add(var.set_espnow(espnow_var))
    cg.add(var.set_light(light_var))
    cg.add(var.set_role(ROLE_MAP[config[CONF_ROLE]]))
    cg.add(var.set_peer(peer_bytes))
    cg.add(var.set_group_hash(_fnv1a_32(config[CONF_GROUP])))
    cg.add(var.set_key(key_bytes))
    cg.add(
        var.set_heartbeat_ms(config[CONF_HEARTBEAT].total_milliseconds)
    )

    # Register the single configured peer with ESPHome's ESP-NOW component.
    cg.add(espnow_var.add_peer(peer.parts))
