from pathlib import Path
import unittest

from esphome import config_validation as cv
from esphome.core import ID

from components.cfx_sync import (
    CONF_LIGHTS,
    CONF_LOCAL_INPUT,
    CONF_REMOTE_INPUT,
    CONF_ROLE,
    ROLE_CONTROLLER,
    ROLE_FOLLOWER,
    ROLE_LEADER,
    ROLE_SATELLITE,
    _extract_control_ids,
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

    def test_controller_requires_one_local_input_and_no_lights(self):
        with self.assertRaisesRegex(
            cv.Invalid, "controller requires local_input"
        ):
            _validate_role_lights(
                {CONF_ROLE: ROLE_CONTROLLER, CONF_LIGHTS: []}
            )

        with self.assertRaisesRegex(
            cv.Invalid, "controller cannot declare lights"
        ):
            _validate_role_lights(
                {
                    CONF_ROLE: ROLE_CONTROLLER,
                    CONF_LIGHTS: [light_id("unexpected_light")],
                    CONF_LOCAL_INPUT: light_id("wall_button"),
                }
            )

        config = {
            CONF_ROLE: ROLE_CONTROLLER,
            CONF_LIGHTS: [],
            CONF_LOCAL_INPUT: light_id("wall_button"),
        }
        self.assertIs(_validate_role_lights(config), config)

    def test_local_input_is_controller_or_satellite_only(self):
        with self.assertRaisesRegex(
            cv.Invalid,
            "local_input can only be used with role: controller or satellite",
        ):
            _validate_role_lights(
                {
                    CONF_ROLE: ROLE_LEADER,
                    CONF_LIGHTS: [light_id("leader_light")],
                    CONF_LOCAL_INPUT: light_id("wall_button"),
                }
            )

    def test_satellite_requires_lights_and_may_have_local_input(self):
        with self.assertRaisesRegex(
            cv.Invalid, "satellite requires at least one light"
        ):
            _validate_role_lights(
                {CONF_ROLE: ROLE_SATELLITE, CONF_LIGHTS: []}
            )

        config = {
            CONF_ROLE: ROLE_SATELLITE,
            CONF_LIGHTS: [light_id("local_light")],
            CONF_LOCAL_INPUT: light_id("wall_button"),
        }
        self.assertIs(_validate_role_lights(config), config)

    def test_satellite_rejects_remote_input(self):
        with self.assertRaisesRegex(
            cv.Invalid, "remote_input can only be used with role: leader"
        ):
            _validate_role_lights(
                {
                    CONF_ROLE: ROLE_SATELLITE,
                    CONF_LIGHTS: [light_id("local_light")],
                    CONF_REMOTE_INPUT: light_id("leader_button_host"),
                }
            )

    def test_remote_input_is_leader_only(self):
        leader_config = {
            CONF_ROLE: ROLE_LEADER,
            CONF_LIGHTS: [light_id("leader_light")],
            CONF_REMOTE_INPUT: light_id("remote_button_host"),
        }
        self.assertIs(_validate_role_lights(leader_config), leader_config)

        with self.assertRaisesRegex(
            cv.Invalid, "remote_input can only be used with role: leader"
        ):
            _validate_role_lights(
                {
                    CONF_ROLE: ROLE_FOLLOWER,
                    CONF_LIGHTS: [light_id("follower_light")],
                    CONF_REMOTE_INPUT: light_id("remote_button_host"),
                }
            )

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

    def test_segment_control_ids_follow_generated_cfx_control_names(self):
        lights = [
            {
                "platform": "cfx_light",
                "id": light_id("desk_master"),
                "is_rgbw": True,
                "segments": [
                    {"id": light_id("desk_left")},
                    {"id": light_id("desk_right")},
                ],
            }
        ]

        self.assertEqual(
            _extract_control_ids(light_id("desk_right"), lights),
            {
                "force_white": "cfx_auto_ctrl_desk_master_2_force_white",
                "intro": "cfx_auto_ctrl_desk_master_2_intro",
                "outro": "cfx_auto_ctrl_desk_master_2_outro",
                "inout_duration": "cfx_auto_ctrl_desk_master_2_inout_dur",
                "speed": "cfx_auto_ctrl_desk_master_2_speed",
                "intensity": "cfx_auto_ctrl_desk_master_2_intensity",
                "mirror": "cfx_auto_ctrl_desk_master_2_mirror",
                "palette": "cfx_auto_ctrl_desk_master_2_palette",
            },
        )

    def test_master_with_segments_has_no_effect_target_controls(self):
        lights = [
            {
                "platform": "cfx_light",
                "id": light_id("desk_master"),
                "is_rgbw": True,
                "segments": [{"id": light_id("desk_left")}],
            }
        ]

        self.assertEqual(
            _extract_control_ids(light_id("desk_master"), lights), {}
        )

    def test_excluded_or_missing_controls_are_not_bound(self):
        lights = [
            {
                "platform": "cfx_light",
                "id": light_id("plain_rgb"),
                "ctrl_exclude": [1, 2, 3, 4, 5],
            }
        ]

        self.assertEqual(
            _extract_control_ids(light_id("plain_rgb"), lights), {}
        )


if __name__ == "__main__":
    unittest.main()
