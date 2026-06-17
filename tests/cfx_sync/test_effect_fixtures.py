from pathlib import Path
import unittest

import yaml

from components.cfx_sync import _extract_cfx_effect_catalog


ROOT = Path(__file__).resolve().parents[2]
FIXTURE_DIR = ROOT / "tests" / "cfx_sync"
COMMON = FIXTURE_DIR / "effect-common.yaml"
LEADER = FIXTURE_DIR / "leader-effect-arduino.yaml"
FOLLOWER = FIXTURE_DIR / "follower-effect-espidf.yaml"


class _FixtureLoader(yaml.SafeLoader):
    pass


def _include(loader, node):
    return loader.construct_scalar(node)


_FixtureLoader.add_constructor("!include", _include)


def load_fixture(path):
    return yaml.load(path.read_text(encoding="utf-8"), Loader=_FixtureLoader)


def bound_lights(config):
    lights_by_id = {light["id"]: light for light in config["light"]}
    return [
        lights_by_id[light_id]
        for light_id in config["cfx_sync"]["lights"]
    ]


def assert_effect_catalog(test_case, light):
    effects = light["effects"]
    test_case.assertEqual(
        _extract_cfx_effect_catalog(light),
        [(3, "Wipe"), (3, "Slow Red Wipe")],
    )
    test_case.assertEqual(
        effects[0],
        {"addressable_cfx": {"effect_id": 3}},
    )
    test_case.assertEqual(
        effects[1]["addressable_cfx"]["name"],
        "Slow Red Wipe",
    )
    test_case.assertEqual(
        effects[1]["addressable_cfx"]["effect_id"],
        3,
    )

    names = [
        effect_config["name"]
        for effect in effects
        for effect_config in effect.values()
        if "name" in effect_config
    ]
    test_case.assertTrue(
        all(len(name.encode("utf-8")) <= 64 for name in names)
    )
    test_case.assertEqual(
        len({name.casefold() for name in names}),
        len(names),
    )


class EffectFixtureTests(unittest.TestCase):
    def test_common_contains_only_shared_infrastructure(self):
        config = load_fixture(COMMON)

        self.assertEqual(
            config["external_components"][0]["components"],
            [
                "cfx_sync",
                "cfx_light",
                "cfx_effect",
                "cfx_effect_registry",
            ],
        )
        self.assertEqual(
            set(config),
            {"external_components", "logger"},
        )

    def test_leader_binds_one_direct_light_with_unsupported_effect(self):
        config = load_fixture(LEADER)
        lights = bound_lights(config)

        self.assertEqual(config["cfx_sync"]["role"], "leader")
        self.assertEqual(len(config["light"]), 1)
        self.assertEqual(
            config["cfx_sync"]["lights"],
            ["sync_effect_leader"],
        )
        self.assertEqual(len(lights), 1)
        self.assertNotIn("segments", lights[0])
        assert_effect_catalog(self, lights[0])
        unsupported = [
            effect["addressable_lambda"]
            for effect in lights[0]["effects"]
            if "addressable_lambda" in effect
        ]
        self.assertEqual(
            [effect["name"] for effect in unsupported],
            ["Unsupported Leader Diagnostic"],
        )
        self.assertEqual(
            config["esp32"]["framework"]["type"],
            "arduino",
        )

    def test_follower_binds_two_direct_effect_lights(self):
        config = load_fixture(FOLLOWER)
        lights = bound_lights(config)

        self.assertEqual(config["cfx_sync"]["role"], "follower")
        self.assertEqual(len(config["light"]), 2)
        self.assertEqual(
            config["cfx_sync"]["lights"],
            ["sync_effect_follower_a", "sync_effect_follower_b"],
        )
        self.assertEqual(len(lights), 2)
        self.assertTrue(all("segments" not in light for light in lights))
        for light in lights:
            assert_effect_catalog(self, light)
        self.assertEqual(
            config["esp32"]["framework"]["type"],
            "esp-idf",
        )


if __name__ == "__main__":
    unittest.main()
