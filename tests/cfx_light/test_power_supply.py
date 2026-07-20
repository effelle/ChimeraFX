from pathlib import Path
import unittest

import esphome.config_validation as cv
from esphome.core import ID
from esphome.const import (
    CONF_ENABLE_TIME,
    CONF_ID,
    CONF_KEEP_ON_TIME,
    CONF_POWER_SUPPLY,
)

from components.cfx_light import light as cfx_light_component


ROOT = Path(__file__).resolve().parents[2]
PYTHON_COMPONENT = ROOT / "components" / "cfx_light" / "light.py"
CPP_HEADER = ROOT / "components" / "cfx_light" / "cfx_light.h"
CPP_SOURCE = ROOT / "components" / "cfx_light" / "cfx_light.cpp"
DOCS = ROOT / "docs" / "cfx_light.md"


class CFXLightPowerSupplyTests(unittest.TestCase):
    def test_schema_uses_standard_esphome_power_supply(self):
        source = PYTHON_COMPONENT.read_text(encoding="utf-8")
        self.assertIn("cv.use_id(power_supply.PowerSupply)", source)
        self.assertIn("cg.add(var.set_power_supply(supply))", source)

    def test_requester_is_owned_by_physical_output(self):
        header = CPP_HEADER.read_text(encoding="utf-8")
        self.assertIn("power_supply::PowerSupplyRequester", header)
        self.assertIn("void set_power_supply(power_supply::PowerSupply *supply)", header)
        self.assertEqual(header.count("power_supply::PowerSupplyRequester"), 1)

    def test_release_waits_for_transport_completion(self):
        source = CPP_SOURCE.read_text(encoding="utf-8")
        self.assertIn("power_transmit_in_flight_()", source)
        self.assertIn("group->tx_in_flight_count > 0", source)
        self.assertIn("this->power_supply_requester_.unrequest();", source)

    def test_omitted_timings_receive_cfx_defaults(self):
        cfx_lights = [{CONF_POWER_SUPPLY: ID("led_power")}]
        power_supplies = [
            {
                CONF_ID: ID("led_power", is_declaration=True),
                CONF_ENABLE_TIME: cv.positive_time_period_milliseconds("20ms"),
                CONF_KEEP_ON_TIME: cv.positive_time_period_milliseconds("10s"),
            }
        ]
        raw_config = {
            CONF_POWER_SUPPLY: [{CONF_ID: "led_power", "pin": "GPIO18"}]
        }

        cfx_light_component._apply_cfx_power_supply_defaults(
            cfx_lights, power_supplies, raw_config
        )

        self.assertEqual(power_supplies[0][CONF_ENABLE_TIME].total_milliseconds, 100)
        self.assertEqual(power_supplies[0][CONF_KEEP_ON_TIME].total_milliseconds, 5000)

    def test_explicit_timings_and_unrelated_supplies_are_preserved(self):
        cfx_lights = [{CONF_POWER_SUPPLY: "led_power"}]
        power_supplies = [
            {
                CONF_ID: "led_power",
                CONF_ENABLE_TIME: cv.positive_time_period_milliseconds("250ms"),
                CONF_KEEP_ON_TIME: cv.positive_time_period_milliseconds("12s"),
            },
            {
                CONF_ID: "other_power",
                CONF_ENABLE_TIME: cv.positive_time_period_milliseconds("20ms"),
                CONF_KEEP_ON_TIME: cv.positive_time_period_milliseconds("10s"),
            },
        ]
        raw_config = {
            CONF_POWER_SUPPLY: [
                {
                    CONF_ID: "led_power",
                    CONF_ENABLE_TIME: "250ms",
                    CONF_KEEP_ON_TIME: "12s",
                    "pin": "GPIO18",
                },
                {CONF_ID: "other_power", "pin": "GPIO19"},
            ]
        }

        cfx_light_component._apply_cfx_power_supply_defaults(
            cfx_lights, power_supplies, raw_config
        )

        self.assertEqual(power_supplies[0][CONF_ENABLE_TIME].total_milliseconds, 250)
        self.assertEqual(power_supplies[0][CONF_KEEP_ON_TIME].total_milliseconds, 12000)
        self.assertEqual(power_supplies[1][CONF_ENABLE_TIME].total_milliseconds, 20)
        self.assertEqual(power_supplies[1][CONF_KEEP_ON_TIME].total_milliseconds, 10000)

    def test_defaults_are_applied_per_omitted_field(self):
        cfx_lights = [{CONF_POWER_SUPPLY: "led_power"}]
        power_supplies = [
            {
                CONF_ID: "led_power",
                CONF_ENABLE_TIME: cv.positive_time_period_milliseconds("300ms"),
                CONF_KEEP_ON_TIME: cv.positive_time_period_milliseconds("10s"),
            }
        ]
        raw_config = {
            CONF_POWER_SUPPLY: [
                {
                    CONF_ID: "led_power",
                    CONF_ENABLE_TIME: "300ms",
                    "pin": "GPIO18",
                }
            ]
        }

        cfx_light_component._apply_cfx_power_supply_defaults(
            cfx_lights, power_supplies, raw_config
        )

        self.assertEqual(power_supplies[0][CONF_ENABLE_TIME].total_milliseconds, 300)
        self.assertEqual(power_supplies[0][CONF_KEEP_ON_TIME].total_milliseconds, 5000)

    def test_documentation_contains_complete_example(self):
        docs = DOCS.read_text(encoding="utf-8")
        self.assertIn("power_supply:\n  - id: led_power\n    pin:", docs)
        self.assertIn("power_supply: led_power", docs)
        self.assertIn("`enable_time` defaults to `100ms`", docs)
        self.assertIn("`keep_on_time` defaults to `5s`", docs)
        self.assertIn("You can override either value", docs)
        self.assertIn("Segments share the single power request", docs)


if __name__ == "__main__":
    unittest.main()
