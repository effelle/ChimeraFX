from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, PropertyMock, patch

from esphome import config_validation as cv
from esphome.core import ID

from components import cfx_sync


ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "cfx_sync" / "__init__.py"


def component_source():
    return COMPONENT.read_text(encoding="utf-8")


def light_id(name):
    return ID(name)


class UDPTransportConfigTests(unittest.IsolatedAsyncioTestCase):
    def test_transport_constants_are_exported(self):
        self.assertEqual(cfx_sync.CONF_TRANSPORT, "transport")
        self.assertEqual(cfx_sync.TRANSPORT_ESPNOW, "espnow")
        self.assertEqual(cfx_sync.TRANSPORT_UDP, "udp")

    def test_schema_defaults_to_espnow_and_accepts_udp(self):
        source = component_source()

        self.assertIn(
            "cv.Optional(CONF_TRANSPORT, default=TRANSPORT_ESPNOW): "
            "cv.one_of(",
            source,
        )
        self.assertIn("TRANSPORT_ESPNOW,", source)
        self.assertIn("TRANSPORT_UDP,", source)
        self.assertIn("lower=True", source)

    def test_autoload_includes_espnow_only_for_espnow_transport(self):
        base_autoload = cfx_sync.AUTO_LOAD({"transport": "udp"})

        self.assertNotIn("espnow", base_autoload)
        self.assertIn("espnow", cfx_sync.AUTO_LOAD({}))
        self.assertIn("espnow", cfx_sync.AUTO_LOAD({"transport": "espnow"}))

    async def test_codegen_emits_espnow_transport_enum(self):
        emitted = []
        defines = []

        class _Var:
            def __getattr__(self, name):
                return lambda *args: (name, *args)

        var = _Var()
        espnow_var = _Var()
        heartbeat = SimpleNamespace(total_milliseconds=30_000)
        config = {
            "id": light_id("sync"),
            "_espnow_id": light_id("espnow"),
            "role": "controller",
            "lights": [],
            "local_input": light_id("wall_button"),
            "group": "room",
            "key": "password",
            "heartbeat": heartbeat,
            "transport": "espnow",
        }

        with (
            patch.object(cfx_sync.cg, "new_Pvariable", return_value=var),
            patch.object(
                type(cfx_sync.CORE),
                "using_arduino",
                new_callable=PropertyMock,
                return_value=False,
            ),
            patch.object(cfx_sync.cg, "register_component", new=AsyncMock()),
            patch.object(
                cfx_sync.cg,
                "get_variable",
                new=AsyncMock(side_effect=[espnow_var, object()]),
            ),
            patch.object(cfx_sync.cg, "add_define", side_effect=defines.append),
            patch.object(cfx_sync.cg, "add", side_effect=emitted.append),
        ):
            await cfx_sync.to_code(config)

        self.assertIn("USE_ESPNOW", defines)
        self.assertIn(("set_auto_add_peer", False), emitted)
        self.assertIn(("set_espnow", espnow_var), emitted)
        self.assertIn(
            (
                "set_transport",
                cfx_sync.CFXSyncTransport.CFX_SYNC_TRANSPORT_ESPNOW,
            ),
            emitted,
        )

    async def test_codegen_emits_udp_transport_enum(self):
        emitted = []
        defines = []

        class _Var:
            def __getattr__(self, name):
                return lambda *args: (name, *args)

        var = _Var()
        get_variable = AsyncMock(side_effect=[object()])
        heartbeat = SimpleNamespace(total_milliseconds=30_000)
        config = {
            "id": light_id("sync"),
            "role": "controller",
            "lights": [],
            "local_input": light_id("wall_button"),
            "group": "room",
            "key": "password",
            "heartbeat": heartbeat,
            "transport": "udp",
        }

        with (
            patch.object(cfx_sync.cg, "new_Pvariable", return_value=var),
            patch.object(
                type(cfx_sync.CORE),
                "using_arduino",
                new_callable=PropertyMock,
                return_value=False,
            ),
            patch.object(cfx_sync.cg, "register_component", new=AsyncMock()),
            patch.object(
                cfx_sync.cg,
                "get_variable",
                new=get_variable,
            ),
            patch.object(cfx_sync.cg, "add_define", side_effect=defines.append),
            patch.object(cfx_sync.cg, "add", side_effect=emitted.append),
        ):
            await cfx_sync.to_code(config)

        get_variable.assert_awaited_once_with(light_id("wall_button"))
        self.assertNotIn("USE_ESPNOW", defines)
        self.assertTrue(
            all(entry[0] != "set_auto_add_peer" for entry in emitted)
        )
        self.assertTrue(all(entry[0] != "set_espnow" for entry in emitted))
        self.assertIn(
            (
                "set_transport",
                cfx_sync.CFXSyncTransport.CFX_SYNC_TRANSPORT_UDP,
            ),
            emitted,
        )

    def test_esp8266_support_is_controller_only_over_udp(self):
        valid = {
            "role": "controller",
            "transport": "udp",
            "lights": [],
            "local_input": light_id("wall_button"),
        }
        invalid_configs = [
            {
                "role": "leader",
                "transport": "udp",
                "lights": [light_id("leader_light")],
            },
            {
                "role": "controller",
                "transport": "espnow",
                "lights": [],
                "local_input": light_id("wall_button"),
            },
            {
                "role": "controller",
                "transport": "udp",
                "lights": [light_id("unexpected_light")],
                "local_input": light_id("wall_button"),
            },
            {
                "role": "controller",
                "transport": "udp",
                "lights": [],
                "local_input": light_id("wall_button"),
                "remote_input": light_id("remote_button_host"),
            },
        ]

        with patch.object(
            type(cfx_sync.CORE),
            "is_esp8266",
            new_callable=PropertyMock,
            return_value=True,
        ):
            self.assertIs(cfx_sync._validate_platform_support(valid), valid)

            for config in invalid_configs:
                with self.subTest(config=config), self.assertRaisesRegex(
                    cv.Invalid,
                    "ESP8266 cfx_sync support is controller-only over UDP",
                ):
                    cfx_sync._validate_platform_support(config)


if __name__ == "__main__":
    unittest.main()
