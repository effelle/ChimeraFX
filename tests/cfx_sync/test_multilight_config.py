from pathlib import Path
import unittest

from esphome import config_validation as cv
from esphome.core import ID

from components.cfx_sync import (
    CONF_LIGHTS,
    CONF_ROLE,
    ROLE_FOLLOWER,
    ROLE_LEADER,
    _normalize_lights,
    _validate_role_lights,
)


ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "cfx_sync" / "__init__.py"
COMMON = ROOT / "tests" / "cfx_sync" / "common.yaml"
MULTI_FOLLOWER = (
    ROOT / "tests" / "cfx_sync" / "follower-multilight.yaml"
)


def light_id(name):
    return ID(name)


class MultiLightConfigTests(unittest.TestCase):
    def test_scalar_light_is_normalized_to_one_item(self):
        lights = _normalize_lights("leader_segment")
        self.assertEqual([item.id for item in lights], ["leader_segment"])

    def test_list_of_lights_is_preserved(self):
        lights = _normalize_lights(
            ["follower_segment_a", "follower_segment_b"]
        )
        self.assertEqual(
            [item.id for item in lights],
            ["follower_segment_a", "follower_segment_b"],
        )

    def test_leader_requires_exactly_one_light(self):
        with self.assertRaisesRegex(
            cv.Invalid, "leader requires exactly one light"
        ):
            _validate_role_lights(
                {
                    CONF_ROLE: ROLE_LEADER,
                    CONF_LIGHTS: [light_id("one"), light_id("two")],
                }
            )

    def test_follower_requires_at_least_one_light(self):
        with self.assertRaisesRegex(
            cv.Invalid, "follower requires at least one light"
        ):
            _validate_role_lights(
                {CONF_ROLE: ROLE_FOLLOWER, CONF_LIGHTS: []}
            )

    def test_duplicate_lights_are_rejected(self):
        with self.assertRaisesRegex(
            cv.Invalid, "duplicate light id 'same_light'"
        ):
            _validate_role_lights(
                {
                    CONF_ROLE: ROLE_FOLLOWER,
                    CONF_LIGHTS: [
                        light_id("same_light"),
                        light_id("same_light"),
                    ],
                }
            )

    def test_valid_follower_fanout_is_accepted(self):
        config = {
            CONF_ROLE: ROLE_FOLLOWER,
            CONF_LIGHTS: [light_id("one"), light_id("two")],
        }
        self.assertIs(_validate_role_lights(config), config)

    def test_legacy_light_id_option_is_removed(self):
        source = COMPONENT.read_text(encoding="utf-8")
        self.assertNotIn('CONF_LIGHT_ID = "light_id"', source)
        self.assertNotIn("cv.Required(CONF_LIGHT_ID)", source)

    def test_codegen_registers_every_configured_light(self):
        source = COMPONENT.read_text(encoding="utf-8")
        self.assertIn("for light_id in config[CONF_LIGHTS]:", source)
        self.assertIn(
            "light_var = await cg.get_variable(light_id)", source
        )
        self.assertIn("cg.add(var.add_light(light_var))", source)
        self.assertNotIn("var.set_light(", source)

    def test_yaml_fixtures_use_lights_interface(self):
        common = COMMON.read_text(encoding="utf-8")
        follower = MULTI_FOLLOWER.read_text(encoding="utf-8")

        self.assertIn("lights: sync_light", common)
        self.assertNotIn("light_id:", common)
        self.assertIn("- sync_light", follower)
        self.assertIn("- sync_light_b", follower)
        self.assertIn("- sync_light_c", follower)

    def test_yaml_fixtures_expose_shared_effect_registry(self):
        for fixture in (COMMON, MULTI_FOLLOWER):
            source = fixture.read_text(encoding="utf-8")
            self.assertIn("cfx_effect_registry", source)


if __name__ == "__main__":
    unittest.main()
