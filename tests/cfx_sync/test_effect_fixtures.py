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


class EffectFixtureTests(unittest.TestCase):
    def test_common_declares_two_segmented_effect_catalogs(self):
        config = load_fixture(COMMON)
        lights = config["light"]

        self.assertEqual(
            config["external_components"][0]["components"],
            [
                "cfx_sync",
                "cfx_light",
                "cfx_effect",
                "cfx_effect_registry",
            ],
        )
        self.assertEqual(len(lights), 2)
        self.assertEqual(
            [light["platform"] for light in lights],
            ["cfx_light", "cfx_light"],
        )
        self.assertEqual(
            [segment["id"] for light in lights for segment in light["segments"]],
            ["sync_effect_segment_a", "sync_effect_segment_b"],
        )

        for light in lights:
            effects = light["effects"]
            self.assertEqual(
                _extract_cfx_effect_catalog(light),
                [(3, "Wipe"), (3, "Slow Red Wipe")],
            )
            self.assertEqual(len(effects), 3)
            self.assertEqual(
                effects[0],
                {"addressable_cfx": {"effect_id": 3}},
            )
            self.assertEqual(
                effects[1]["addressable_cfx"]["name"],
                "Slow Red Wipe",
            )
            self.assertEqual(
                effects[1]["addressable_cfx"]["effect_id"],
                3,
            )
            self.assertEqual(
                list(effects[2]),
                ["addressable_lambda"],
            )

            names = [
                effect_config["name"]
                for effect in effects
                for effect_config in effect.values()
                if "name" in effect_config
            ]
            self.assertTrue(
                all(len(name.encode("utf-8")) <= 64 for name in names)
            )
            self.assertEqual(
                len({name.casefold() for name in names}),
                len(names),
            )

    def test_leader_binds_exactly_one_effect_segment(self):
        config = load_fixture(LEADER)

        self.assertEqual(config["cfx_sync"]["role"], "leader")
        self.assertEqual(
            config["cfx_sync"]["lights"],
            ["sync_effect_segment_a"],
        )
        self.assertEqual(
            config["esp32"]["framework"]["type"],
            "arduino",
        )

    def test_follower_binds_two_effect_segments(self):
        config = load_fixture(FOLLOWER)

        self.assertEqual(config["cfx_sync"]["role"], "follower")
        self.assertEqual(
            config["cfx_sync"]["lights"],
            ["sync_effect_segment_a", "sync_effect_segment_b"],
        )
        self.assertEqual(
            config["esp32"]["framework"]["type"],
            "esp-idf",
        )


if __name__ == "__main__":
    unittest.main()
