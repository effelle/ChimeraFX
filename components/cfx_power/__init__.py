"""Global ChimeraFX power monitor/limit configuration."""

import esphome.config_validation as cv

from esphome.components.cfx_light.light import (
    CONF_LIMIT,
    CONF_MONITOR,
    POWER_LIMIT_SCHEMA,
    POWER_MONITOR_SCHEMA,
)

CODEOWNERS = ["@effelle"]
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["sensor", "select", "text_sensor"]


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(CONF_MONITOR): POWER_MONITOR_SCHEMA,
            cv.Optional(CONF_LIMIT): POWER_LIMIT_SCHEMA,
        }
    ),
    cv.has_at_least_one_key(CONF_MONITOR, CONF_LIMIT),
)


async def to_code(config):
    # cfx_light reads this validated top-level config from CORE.config and
    # autogenerates the singleton manager only when at least one output exists.
    pass
