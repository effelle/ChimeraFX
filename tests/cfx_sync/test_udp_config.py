from pathlib import Path
import re
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


class _FinalConfig:
    def __init__(self, entries):
        self.entries = entries

    def get_config_for_path(self, path):
        key = path[0]
        if key not in self.entries:
            raise KeyError(key)
        return self.entries[key]


class UDPTransportConfigTests(unittest.IsolatedAsyncioTestCase):
    def test_transport_constants_are_exported(self):
        self.assertEqual(cfx_sync.CONF_TRANSPORT, "transport")
        self.assertEqual(cfx_sync.TRANSPORT_AUTO, "auto")
        self.assertEqual(cfx_sync.TRANSPORT_ESPNOW, "espnow")
        self.assertEqual(cfx_sync.TRANSPORT_UDP, "udp")
        self.assertEqual(cfx_sync.CONF_LOCAL_LIGHT_INPUT, "local_light_input")

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

    def test_multi_conf_autoload_handles_config_lists(self):
        with patch.object(
            type(cfx_sync.CORE),
            "is_esp8266",
            new_callable=PropertyMock,
            return_value=False,
        ):
            self.assertNotIn(
                "espnow",
                cfx_sync.AUTO_LOAD(
                    [{"transport": "udp"}, {"transport": "udp"}]
                ),
            )
            self.assertIn(
                "espnow",
                cfx_sync.AUTO_LOAD(
                    [{"transport": "udp"}, {"transport": "auto"}]
                ),
            )

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

    def test_esp8266_satellite_autoload_includes_light_without_espnow(self):
        with patch.object(
            type(cfx_sync.CORE),
            "is_esp8266",
            new_callable=PropertyMock,
            return_value=True,
        ):
            autoload = cfx_sync.AUTO_LOAD(
                {"role": "satellite", "transport": "auto"}
            )

        self.assertIn("binary_sensor", autoload)
        self.assertIn("hmac_sha256", autoload)
        self.assertIn("light", autoload)
        self.assertNotIn("espnow", autoload)
        self.assertNotIn("cfx_button", autoload)
        self.assertNotIn("cfx_effect_registry", autoload)
        self.assertNotIn("number", autoload)
        self.assertNotIn("select", autoload)

    def test_local_input_binary_sensor_prefers_owning_cfx_button(self):
        config = {
            "role": "satellite",
            "lights": [],
            "local_input": light_id("wall_button"),
        }
        final_config = _FinalConfig(
            {
                "binary_sensor": [{"id": light_id("wall_button")}],
                "cfx_button": [
                    {
                        "id": light_id("wall_dimmer"),
                        "button": light_id("wall_button"),
                        "dimmer": {"lights": []},
                    }
                ],
            }
        )

        cfx_sync._validate_local_input_id(config, final_config)

        self.assertEqual(config["_local_input_kind"], "cfx_button")
        self.assertEqual(config["_local_button_id"], light_id("wall_dimmer"))

    async def test_codegen_uses_resolved_local_cfx_button_id(self):
        emitted = []

        class _Var:
            def __getattr__(self, name):
                return lambda *args: (name, *args)

        var = _Var()
        local_button = object()
        heartbeat = SimpleNamespace(total_milliseconds=30_000)
        config = {
            "id": light_id("sync"),
            "role": "satellite",
            "lights": [],
            "local_input": light_id("wall_button"),
            "_local_input_kind": "cfx_button",
            "_local_button_id": light_id("wall_dimmer"),
            "local_light_input": False,
            "group": "room",
            "key": "password",
            "heartbeat": heartbeat,
            "transport": "udp",
        }

        with (
            patch.object(cfx_sync.cg, "new_Pvariable", return_value=var),
            patch.object(cfx_sync.cg, "register_component", new=AsyncMock()),
            patch.object(
                cfx_sync.cg,
                "get_variable",
                new=AsyncMock(side_effect=[local_button]),
            ),
            patch.object(cfx_sync.cg, "add", side_effect=emitted.append),
        ):
            await cfx_sync.to_code(config)

        self.assertIn(("set_local_button", local_button), emitted)
        self.assertNotIn(("set_local_input", local_button), emitted)

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

    def test_esp8266_support_is_controller_or_satellite_over_udp(self):
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
        valid_satellite = {
            "role": "satellite",
            "transport": "auto",
            "lights": [light_id("tuya_light")],
        }
        valid_satellite_udp = {
            "role": "satellite",
            "transport": "udp",
            "lights": [light_id("tuya_light")],
            "local_input": light_id("wall_button"),
        }
        valid_follower = {
            "role": "follower",
            "transport": "auto",
            "lights": [light_id("tuya_light")],
        }
        valid_follower_udp = {
            "role": "follower",
            "transport": "udp",
            "lights": [light_id("tuya_light")],
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
            {
                "role": "follower",
                "transport": "espnow",
                "lights": [light_id("tuya_light")],
            },
            {
                "role": "follower",
                "transport": "udp",
                "lights": [],
            },
            {
                "role": "satellite",
                "transport": "espnow",
                "lights": [light_id("tuya_light")],
            },
            {
                "role": "satellite",
                "transport": "udp",
                "lights": [],
            },
            {
                "role": "satellite",
                "transport": "udp",
                "lights": [light_id("tuya_light")],
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
            self.assertIs(
                cfx_sync._validate_platform_support(valid_satellite),
                valid_satellite,
            )
            self.assertIs(
                cfx_sync._validate_platform_support(valid_satellite_udp),
                valid_satellite_udp,
            )
            self.assertIs(
                cfx_sync._validate_platform_support(valid_follower),
                valid_follower,
            )
            self.assertIs(
                cfx_sync._validate_platform_support(valid_follower_udp),
                valid_follower_udp,
            )

            for config in invalid_configs:
                with self.subTest(config=config), self.assertRaisesRegex(
                    cv.Invalid,
                    "ESP8266 cfx_sync support is controller or light follower over UDP",
                ):
                    cfx_sync._validate_platform_support(config)

    def test_local_light_input_is_satellite_only_and_single_light(self):
        valid = {
            "role": "satellite",
            "transport": "udp",
            "lights": [light_id("tuya_light")],
            "local_light_input": True,
        }

        self.assertIs(cfx_sync._validate_role_lights(valid), valid)

        invalid_configs = [
            {
                "role": "leader",
                "transport": "udp",
                "lights": [light_id("leader_light")],
                "local_light_input": True,
            },
            {
                "role": "follower",
                "transport": "udp",
                "lights": [light_id("follower_light")],
                "local_light_input": True,
            },
            {
                "role": "controller",
                "transport": "udp",
                "lights": [],
                "local_input": light_id("wall_button"),
                "local_light_input": True,
            },
            {
                "role": "satellite",
                "transport": "udp",
                "lights": [light_id("one"), light_id("two")],
                "local_light_input": True,
            },
        ]

        for config in invalid_configs:
            with self.subTest(config=config), self.assertRaisesRegex(
                cv.Invalid,
                "local_light_input can only be used by a satellite with exactly one light",
            ):
                cfx_sync._validate_role_lights(config)

    async def test_codegen_emits_esp8266_satellite_light_and_udp_transport(self):
        emitted = []

        class _Var:
            def __getattr__(self, name):
                return lambda *args: (name, *args)

        var = _Var()
        light = object()
        local_input = object()
        heartbeat = SimpleNamespace(total_milliseconds=30_000)
        config = {
            "id": light_id("sync"),
            "role": "satellite",
            "lights": [light_id("tuya_light")],
            "local_input": light_id("wall_button"),
            "group": "room",
            "key": "password",
            "heartbeat": heartbeat,
            "transport": "auto",
            "local_light_input": True,
            "_effect_catalogs": [[]],
            "_control_ids": [{}],
        }

        with (
            patch.object(
                type(cfx_sync.CORE),
                "is_esp8266",
                new_callable=PropertyMock,
                return_value=True,
            ),
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
                new=AsyncMock(side_effect=[light, local_input]),
            ),
            patch.object(cfx_sync.cg, "add", side_effect=emitted.append),
        ):
            await cfx_sync.to_code(config)

        self.assertIn(("add_light", light), emitted)
        self.assertIn(("set_local_input", local_input), emitted)
        self.assertIn(("set_local_light_input", True), emitted)
        self.assertIn(
            (
                "set_transport",
                cfx_sync.CFXSyncTransport.CFX_SYNC_TRANSPORT_AUTO,
            ),
            emitted,
        )

    def test_esp8266_satellite_runtime_applies_supported_light_state_fields(self):
        header = (ROOT / "components" / "cfx_sync" / "cfx_sync.h").read_text(
            encoding="utf-8"
        )
        source = (ROOT / "components" / "cfx_sync" / "cfx_sync.cpp").read_text(
            encoding="utf-8"
        )
        color_header = (
            ROOT / "components" / "cfx_sync" / "cfx_sync_color.h"
        ).read_text(encoding="utf-8")

        self.assertIn('#include "cfx_sync_color.h"', header)
        self.assertNotRegex(
            header,
            re.compile(
                r"#if defined\(USE_ESP32\)\s*"
                r'#include "cfx_sync_color.h"',
                re.DOTALL,
            ),
        )
        self.assertNotRegex(
            color_header,
            re.compile(
                r"#if defined\(USE_ESP32\)\s*"
                r"inline bool light_supports_rgb_white",
            ),
        )
        self.assertIn("#include \"esphome/components/light/light_state.h\"", header)
        self.assertIn("void add_light(light::LightState *light)", header)
        self.assertIn("std::vector<light::LightState *> lights_;", header)
        self.assertIn("bool apply_remote_state_(const CFXSyncPacket &packet);", header)
        self.assertIn("#if defined(USE_ESP8266)", source)
        self.assertIn("light->remote_values.is_on() != packet.power", source)
        self.assertIn("call.set_state(packet.power);", source)
        self.assertIn("const auto traits = light->get_traits();", source)
        self.assertIn("const bool supports_brightness =", source)
        self.assertIn("traits.supports_color_mode(light::ColorMode::BRIGHTNESS)", source)
        self.assertIn("traits.supports_color_mode(light::ColorMode::WHITE)", source)
        self.assertIn("traits.supports_color_mode(light::ColorMode::RGB)", source)
        self.assertIn("traits.supports_color_mode(light::ColorMode::RGB_WHITE)", source)
        self.assertIn(
            "traits.supports_color_mode(light::ColorMode::COLOR_TEMPERATURE)",
            source,
        )
        self.assertIn(
            "traits.supports_color_mode(light::ColorMode::COLD_WARM_WHITE)",
            source,
        )
        self.assertIn(
            "traits.supports_color_mode(light::ColorMode::RGB_COLOR_TEMPERATURE)",
            source,
        )
        self.assertIn(
            "traits.supports_color_mode(light::ColorMode::RGB_COLD_WARM_WHITE)",
            source,
        )
        self.assertIn(
            "if (packet.has_brightness && supports_brightness && apply_visual_state)",
            source,
        )
        self.assertIn("light->remote_values.get_brightness()", source)
        self.assertIn(
            "call.set_brightness_if_supported(desired_brightness);", source
        )
        self.assertIn("light_supports_rgb_white(*light)", source)
        self.assertIn("light_supports_rgb(*light)", source)
        self.assertIn("convert_color_for_follower(snapshot, true)", source)
        self.assertIn("convert_color_for_follower(snapshot, false)", source)
        self.assertIn("call.set_rgb(", source)
        self.assertIn("call.set_white(", source)
        self.assertIn("light_supports_color_temperature(*light)", source)
        self.assertIn(
            "call.set_color_temperature(packet.color_temperature_mireds)",
            source,
        )
        self.assertIn("light_supports_cold_warm_white(*light)", source)
        self.assertIn("call.set_cold_white(packet.cold_white / 255.0f)", source)
        self.assertIn("call.set_warm_white(packet.warm_white / 255.0f)", source)
        self.assertIn("return has_action;", source)
        self.assertIn(
            'ESP_LOGV(TAG, "ESP8266 light follower ignoring ChimeraFX-only fields")',
            source,
        )
        self.assertRegex(
            source,
            re.compile(
                r"if \(packet\.has_effect \|\| packet\.has_controls \|\|"
                r"\s*packet\.has_transition \|\|\s*packet\.has_ramp\)",
                re.DOTALL,
            ),
        )

    def test_esp8266_satellite_local_light_input_sends_state_to_leader(self):
        header = (ROOT / "components" / "cfx_sync" / "cfx_sync.h").read_text(
            encoding="utf-8"
        )
        source = (ROOT / "components" / "cfx_sync" / "cfx_sync.cpp").read_text(
            encoding="utf-8"
        )
        color_header = (
            ROOT / "components" / "cfx_sync" / "cfx_sync_color.h"
        ).read_text(encoding="utf-8")

        self.assertIn("void set_local_light_input(bool enabled)", header)
        self.assertIn("bool local_light_input_{false};", header)
        self.assertIn("bool send_satellite_local_state_();", header)
        self.assertIn("bool send_satellite_state_packet_(std::vector<uint8_t> &packet);", header)
        self.assertIn("CFXSyncLightListener light_listener_{this};", header)
        listener_index = header.index("class CFXSyncLightListener")
        enable_switch_index = header.index("class CFXSyncEnableSwitch")
        self.assertLess(enable_switch_index, listener_index)
        self.assertIn("#endif\n\nclass CFXSyncLightListener", header)
        self.assertNotRegex(
            color_header,
            re.compile(
                r"#if defined\(USE_ESP32\)\s*"
                r"inline CFXSyncLightSnapshot capture_light_snapshot",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"if \(this->role_ == CFXSyncRole::SATELLITE &&"
                r"\s*this->local_light_input_\).*?"
                r"this->lights_\[0\]->add_remote_values_listener"
                r"\(&this->light_listener_\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_satellite_local_state_\(\)"
                r".*?capture_light_snapshot\(\*light\).*?"
                r"CFXSyncPacketCodec::encode_state_snapshot"
                r".*?this->send_satellite_state_packet_\(packet\)",
                re.DOTALL,
            ),
        )
        sender = re.search(
            r"bool CFXSyncComponent::send_satellite_state_packet_"
            r".*?\n\}\n\nbool CFXSyncComponent::send_state_ack_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(sender)
        sender = sender.group(0)
        self.assertIn("peer.node_role != CFXSyncNodeRole::LEADER", sender)
        self.assertIn("peer.transport != CFXSyncTransportKind::UDP", sender)
        self.assertIn("this->send_packet_to_peer_(peer, packet)", sender)
        self.assertIn("this->send_packet_to_(BROADCAST_MAC, packet)", sender)
        self.assertIn(
            "this->handle_satellite_state_proposal_(*peer, packet)", source
        )
        handler = re.search(
            r"bool CFXSyncComponent::handle_satellite_state_proposal_"
            r".*?\n\}\n#endif",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(handler)
        handler = handler.group(0)
        self.assertIn("peer.node_role != CFXSyncNodeRole::SATELLITE", handler)
        self.assertIn("CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER", handler)
        self.assertIn("this->apply_remote_state_(packet)", handler)
        self.assertIn("this->send_state_()", handler)

    def test_satellite_local_light_input_suppresses_remote_apply_echo(self):
        source = (ROOT / "components" / "cfx_sync" / "cfx_sync.cpp").read_text(
            encoding="utf-8"
        )

        self.assertRegex(
            source,
            re.compile(
                r"if \(this->role_ == CFXSyncRole::SATELLITE && applied\)"
                r".*?this->observed_state_ = capture_light_snapshot"
                r"\(\*this->lights_\[0\]\);"
                r".*?this->has_observed_state_ = true;",
                re.DOTALL,
            ),
        )

    def test_satellite_logs_apply_only_when_state_performs(self):
        source = (ROOT / "components" / "cfx_sync" / "cfx_sync.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("const bool applied = this->apply_remote_state_(packet);", source)
        self.assertIn(
            "if (this->role_ == CFXSyncRole::SATELLITE && applied)",
            source,
        )

    def test_toggle_mode_preserves_settled_release_edge(self):
        source = (ROOT / "components" / "cfx_sync" / "cfx_sync.cpp").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("if (toggle && !pressed)", source)
        self.assertIn(
            "this->send_input_state_(\n"
            "                            pressed, maintained, toggle,\n"
            "                            CFXSyncInputAction::PRIMARY);",
            source,
        )


if __name__ == "__main__":
    unittest.main()
