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
        self.assertEqual(cfx_sync.TRANSPORT_AUTO, "auto")
        self.assertEqual(cfx_sync.TRANSPORT_ESPNOW, "espnow")
        self.assertEqual(cfx_sync.TRANSPORT_UDP, "udp")

    def test_schema_defaults_to_auto_and_accepts_explicit_transports(self):
        source = component_source()

        self.assertIn(
            "cv.Optional(CONF_TRANSPORT, default=TRANSPORT_AUTO): "
            "cv.one_of(",
            source,
        )
        self.assertIn("TRANSPORT_AUTO,", source)
        self.assertIn("TRANSPORT_ESPNOW,", source)
        self.assertIn("TRANSPORT_UDP,", source)
        self.assertIn("lower=True", source)

    def test_autoload_uses_espnow_for_auto_on_esp32(self):
        with patch.object(
            type(cfx_sync.CORE),
            "is_esp8266",
            new_callable=PropertyMock,
            return_value=False,
        ):
            base_autoload = cfx_sync.AUTO_LOAD({"transport": "udp"})

            self.assertNotIn("espnow", base_autoload)
            self.assertIn("espnow", cfx_sync.AUTO_LOAD({}))
            self.assertIn("espnow", cfx_sync.AUTO_LOAD({"transport": "auto"}))
            self.assertIn("espnow", cfx_sync.AUTO_LOAD({"transport": "espnow"}))

    def test_autoload_omits_espnow_for_esp8266_auto_or_udp_controller(self):
        with patch.object(
            type(cfx_sync.CORE),
            "is_esp8266",
            new_callable=PropertyMock,
            return_value=True,
        ):
            self.assertNotIn("espnow", cfx_sync.AUTO_LOAD({}))
            self.assertNotIn("espnow", cfx_sync.AUTO_LOAD({"transport": "auto"}))
            self.assertNotIn("espnow", cfx_sync.AUTO_LOAD({"transport": "udp"}))

    def test_esp8266_controller_autoload_is_minimal(self):
        with patch.object(
            type(cfx_sync.CORE),
            "is_esp8266",
            new_callable=PropertyMock,
            return_value=True,
        ):
            autoload = cfx_sync.AUTO_LOAD(
                {"role": "controller", "transport": "auto"}
            )

        self.assertIn("binary_sensor", autoload)
        self.assertIn("hmac_sha256", autoload)
        self.assertNotIn("espnow", autoload)
        self.assertNotIn("cfx_button", autoload)
        self.assertNotIn("cfx_effect_registry", autoload)
        self.assertNotIn("light", autoload)
        self.assertNotIn("number", autoload)
        self.assertNotIn("select", autoload)
        self.assertNotIn("switch", autoload)

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

    async def test_codegen_emits_udp_transport_enum_without_espnow(self):
        emitted = []
        defines = []

        class _Var:
            def __getattr__(self, name):
                return lambda *args: (name, *args)

        var = _Var()
        local_input = object()
        get_variable = AsyncMock(side_effect=[local_input])
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

        self.assertEqual(get_variable.await_count, 1)
        self.assertNotIn("USE_ESPNOW", defines)
        self.assertNotIn(("set_auto_add_peer", False), emitted)
        self.assertFalse(any(item[0] == "set_espnow" for item in emitted))
        self.assertIn(
            (
                "set_transport",
                cfx_sync.CFXSyncTransport.CFX_SYNC_TRANSPORT_UDP,
            ),
            emitted,
        )

    async def test_codegen_emits_auto_transport_enum_and_espnow_for_esp32(self):
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
            "transport": "auto",
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
        self.assertIn(("set_espnow", espnow_var), emitted)
        self.assertIn(
            (
                "set_transport",
                cfx_sync.CFXSyncTransport.CFX_SYNC_TRANSPORT_AUTO,
            ),
            emitted,
        )

    def test_esp8266_support_is_controller_only_over_udp(self):
        valid = {
            "role": "controller",
            "transport": "auto",
            "lights": [],
            "local_input": light_id("wall_button"),
        }
        valid_udp = {
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
            self.assertIs(
                cfx_sync._validate_platform_support(valid_udp), valid_udp
            )

            for config in invalid_configs:
                with self.subTest(config=config), self.assertRaisesRegex(
                    cv.Invalid,
                    "ESP8266 cfx_sync support is controller-only over UDP",
                ):
                    cfx_sync._validate_platform_support(config)


if __name__ == "__main__":
    unittest.main()
