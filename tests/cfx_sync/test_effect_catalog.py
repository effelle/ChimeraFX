from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch

from esphome import automation
from esphome import config_validation as cv
from esphome.core import ID

from components import cfx_sync

if __package__:
    from .esphome_test_support import (
        ensure_register_action_supports_synchronous,
    )
else:
    from esphome_test_support import (
        ensure_register_action_supports_synchronous,
    )


ensure_register_action_supports_synchronous(automation)


ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "cfx_sync" / "__init__.py"


def light_id(name):
    return ID(name)


def cfx_effect(effect_id, name=None):
    config = {"effect_id": effect_id}
    if name is not None:
        config["name"] = name
    return {"addressable_cfx": config}


class _FakeFullConfig:
    def __init__(self, espnow, lights):
        self.espnow = espnow
        self.lights = lights

    def get_config_for_path(self, path):
        if path == ["espnow"]:
            return self.espnow
        if path == ["light"]:
            return self.lights
        raise AssertionError(f"unexpected config path: {path}")


class EffectCatalogTests(unittest.IsolatedAsyncioTestCase):
    def test_register_action_support_accepts_synchronous_keyword(self):
        calls = []

        def legacy_register_action(name, action_type, schema):
            calls.append((name, action_type, schema))
            return lambda function: function

        legacy_automation = SimpleNamespace(
            register_action=legacy_register_action
        )

        ensure_register_action_supports_synchronous(legacy_automation)
        decorator = legacy_automation.register_action(
            "test.action", object, {}, synchronous=True
        )

        self.assertTrue(callable(decorator))
        self.assertEqual(len(calls), 1)

    def test_cfx_effect_and_cfx_sync_share_effect_name_registry(self):
        from components import cfx_effect
        from components.cfx_effect_registry import CFX_EFFECT_NAMES

        self.assertIs(cfx_effect.CFX_EFFECT_NAMES, CFX_EFFECT_NAMES)
        self.assertIs(cfx_sync.CFX_EFFECT_NAMES, CFX_EFFECT_NAMES)

    def test_every_published_effect_has_a_canonical_name(self):
        from components import cfx_effect
        from components.cfx_effect_registry import CFX_EFFECT_NAMES

        published_ids = {
            effect_id
            for category, effect_id, _ in cfx_effect.CFX_EFFECTS
            if category != "sep"
        }

        self.assertEqual(published_ids - CFX_EFFECT_NAMES.keys(), set())

    def test_cfx_sync_has_no_effect_registry_source_parsing(self):
        source = COMPONENT.read_text(encoding="utf-8")

        self.assertNotIn("import ast", source)
        self.assertNotIn("_load_cfx_effect_names", source)
        self.assertNotIn("ast.parse", source)
        self.assertNotIn("read_text", source)

    def test_canonical_name_resolution_does_not_import_cfx_effect(self):
        real_import = __import__

        def reject_cfx_effect_import(name, *args, **kwargs):
            if name in (
                "components.cfx_effect",
                "esphome.components.cfx_effect",
            ):
                raise AssertionError("cfx_effect module must not be imported")
            return real_import(name, *args, **kwargs)

        with patch(
            "builtins.__import__", side_effect=reject_cfx_effect_import
        ):
            self.assertEqual(
                cfx_sync._resolve_cfx_effect_name({"effect_id": 0}),
                "Solid",
            )

    def test_catalog_includes_only_addressable_cfx_entries(self):
        light_config = {
            "effects": [
                cfx_effect(0),
                {"addressable_lambda": {"name": "Diagnostic"}},
                cfx_effect(1, "Custom Blink"),
            ]
        }

        self.assertEqual(
            cfx_sync._extract_cfx_effect_catalog(light_config),
            [(0, "Solid"), (1, "Custom Blink")],
        )

    def test_same_effect_id_with_different_names_is_valid(self):
        light_config = {
            "effects": [
                cfx_effect(7, "Dynamic Slow"),
                cfx_effect(7, "Dynamic Fast"),
            ]
        }

        self.assertEqual(
            cfx_sync._extract_cfx_effect_catalog(light_config),
            [(7, "Dynamic Slow"), (7, "Dynamic Fast")],
        )

    def test_exact_duplicate_id_and_name_is_rejected(self):
        light_config = {
            "effects": [
                cfx_effect(7, "Dynamic"),
                cfx_effect(7, "Dynamic"),
            ]
        }

        with self.assertRaises(cv.Invalid):
            cfx_sync._extract_cfx_effect_catalog(light_config)

    def test_case_insensitive_name_collision_is_rejected(self):
        light_config = {
            "effects": [
                cfx_effect(7, "Dynamic"),
                cfx_effect(8, "DYNAMIC"),
            ]
        }

        with self.assertRaises(cv.Invalid):
            cfx_sync._extract_cfx_effect_catalog(light_config)

    def test_reserved_none_name_is_rejected_case_insensitively(self):
        for name in ("None", "NONE", "none"):
            with self.subTest(name=name), self.assertRaises(cv.Invalid):
                cfx_sync._extract_cfx_effect_catalog(
                    {"effects": [cfx_effect(1, name)]}
                )

    def test_addressable_cfx_name_over_64_utf8_bytes_is_rejected(self):
        with self.assertRaises(cv.Invalid):
            cfx_sync._extract_cfx_effect_catalog(
                {"effects": [cfx_effect(1, "é" * 33)]}
            )

    def test_unsupported_effect_name_over_64_utf8_bytes_is_rejected(self):
        light_config = {
            "effects": [
                {"addressable_lambda": {"name": "é" * 33}},
            ]
        }

        with self.assertRaises(cv.Invalid):
            cfx_sync._extract_cfx_effect_catalog(light_config)

    def test_empty_configured_effect_name_is_rejected(self):
        with self.assertRaises(cv.Invalid):
            cfx_sync._extract_cfx_effect_catalog(
                {"effects": [cfx_effect(1, "")]}
            )

    def test_non_string_configured_effect_name_is_rejected(self):
        with self.assertRaises(cv.Invalid):
            cfx_sync._extract_cfx_effect_catalog(
                {
                    "effects": [
                        {"addressable_lambda": {"name": 123}},
                    ]
                }
            )

    def test_find_effect_source_returns_direct_light_config(self):
        direct = {
            "platform": "addressable_lambda",
            "id": light_id("direct_light"),
            "effects": [],
        }
        all_lights = [
            {
                "platform": "cfx_light",
                "id": light_id("parent"),
                "segments": [{"id": light_id("direct_light")}],
            },
            direct,
        ]

        self.assertIs(
            cfx_sync._find_effect_source_config(
                light_id("direct_light"), all_lights
            ),
            direct,
        )

    def test_find_effect_source_returns_parent_for_cfx_light_segment(self):
        parent = {
            "platform": "cfx_light",
            "id": light_id("parent"),
            "segments": [
                {"id": light_id("segment_a")},
                {"id": light_id("segment_b")},
            ],
            "effects": [cfx_effect(0)],
        }

        self.assertIs(
            cfx_sync._find_effect_source_config(
                light_id("segment_b"), [parent]
            ),
            parent,
        )

    def test_find_effect_source_returns_none_for_unknown_light(self):
        self.assertIsNone(
            cfx_sync._find_effect_source_config(
                light_id("missing"), []
            )
        )

    def test_final_validate_builds_aligned_effect_catalogs(self):
        direct = {
            "platform": "addressable_lambda",
            "id": light_id("direct"),
            "effects": [cfx_effect(1, "Direct Effect")],
        }
        parent = {
            "platform": "cfx_light",
            "id": light_id("parent"),
            "segments": [{"id": light_id("segment")}],
            "effects": [cfx_effect(2, "Segment Effect")],
        }
        final_config = _FakeFullConfig(
            espnow={"auto_add_peer": False},
            lights=[direct, parent],
        )
        config = {
            "lights": [
                light_id("direct"),
                light_id("segment"),
                light_id("missing"),
            ]
        }

        token = cfx_sync.full_config.set(final_config)
        try:
            result = cfx_sync._final_validate(config)
        finally:
            cfx_sync.full_config.reset(token)

        self.assertIs(result, config)
        self.assertEqual(
            config["_effect_catalogs"],
            [
                [(1, "Direct Effect")],
                [(2, "Segment Effect")],
                [],
            ],
        )

    def test_final_validate_retains_auto_add_peer_policy(self):
        final_config = _FakeFullConfig(
            espnow={"auto_add_peer": True},
            lights=[],
        )

        token = cfx_sync.full_config.set(final_config)
        try:
            with self.assertRaisesRegex(cv.Invalid, "auto_add_peer"):
                cfx_sync._final_validate(
                    {"lights": [light_id("missing")]}
                )
        finally:
            cfx_sync.full_config.reset(token)

    async def test_codegen_emits_each_catalog_entry_for_its_light_index(self):
        emitted = []

        class _Var:
            def __getattr__(self, name):
                return lambda *args: (name, *args)

        var = _Var()
        espnow_var = _Var()
        light_a = object()
        light_b = object()
        peer = SimpleNamespace(parts=[0, 1, 2, 3, 4, 5])
        heartbeat = SimpleNamespace(total_milliseconds=30_000)
        config = {
            "id": light_id("sync"),
            "espnow_id": light_id("espnow"),
            "role": "follower",
            "lights": [light_id("a"), light_id("b")],
            "peer": peer,
            "group": "room",
            "key": "password",
            "heartbeat": heartbeat,
            "_effect_catalogs": [
                [(3, "Wipe"), (3, "Slow Wipe")],
                [(9, "Rainbow")],
            ],
        }

        with (
            patch.object(cfx_sync.cg, "new_Pvariable", return_value=var),
            patch.object(
                cfx_sync.cg,
                "register_component",
                new=AsyncMock(),
            ),
            patch.object(
                cfx_sync.cg,
                "get_variable",
                new=AsyncMock(
                    side_effect=[espnow_var, light_a, light_b]
                ),
            ),
            patch.object(
                cfx_sync.cg,
                "add",
                side_effect=emitted.append,
            ),
        ):
            await cfx_sync.to_code(config)

        self.assertEqual(
            emitted[1:6],
            [
                ("add_light", light_a),
                ("add_effect", 0, 3, "Wipe"),
                ("add_effect", 0, 3, "Slow Wipe"),
                ("add_light", light_b),
                ("add_effect", 1, 9, "Rainbow"),
            ],
        )


if __name__ == "__main__":
    unittest.main()
