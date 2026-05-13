"""Global ChimeraFX power monitor/limit configuration."""

import esphome.config_validation as cv

from esphome.components.cfx_light.light import (
    CONF_AUTO,
    CONF_LIMIT,
    CONF_MONITOR,
    CONF_PSU_CURRENT_LIMIT,
    POWER_LIMIT_SCHEMA,
    POWER_MONITOR_SCHEMA,
)

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["sensor", "select", "text_sensor"]


def _validate_power_config(config):
    limit = config.get(CONF_LIMIT) or {}
    if CONF_AUTO in limit:
        if CONF_MONITOR not in config:
            raise cv.Invalid("cfx_power.limit.auto requires cfx_power.monitor")
        if config[CONF_MONITOR].get(CONF_PSU_CURRENT_LIMIT, 0.0) <= 0.0:
            raise cv.Invalid(
                "cfx_power.limit.auto requires monitor.psu_current_limit"
            )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(CONF_MONITOR): POWER_MONITOR_SCHEMA,
            cv.Optional(CONF_LIMIT): POWER_LIMIT_SCHEMA,
        }
    ),
    cv.has_at_least_one_key(CONF_MONITOR, CONF_LIMIT),
    _validate_power_config,
)


async def to_code(config):
    # cfx_light reads this validated top-level config from CORE.config and
    # autogenerates the singleton manager only when at least one output exists.
    pass
