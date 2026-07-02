from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
PY_COMPONENT = ROOT / "components" / "cfx_sync" / "__init__.py"
HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync.h"
SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync.cpp"
TRANSPORT_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_transport.h"
BUS_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_bus.h"
BUS_SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync_bus.cpp"
UDP_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_udp.h"
UDP_SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync_udp.cpp"
PACKET_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_packet.h"
PACKET_SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync_packet.cpp"
COLOR_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_color.h"
EFFECT_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_effect.h"
CFX_BUTTON_PY = ROOT / "components" / "cfx_button" / "__init__.py"
CFX_BUTTON_HEADER = ROOT / "components" / "cfx_button" / "cfx_button.h"
CFX_BUTTON_SOURCE = ROOT / "components" / "cfx_button" / "cfx_button.cpp"
CFX_BUTTON_STATE_HEADER = ROOT / "components" / "cfx_button" / "cfx_button_state.h"
CFX_DIMMER_SOURCE = ROOT / "components" / "cfx_button" / "cfx_dimmer.cpp"
CFX_DIMMER_TIMING_HEADER = (
    ROOT / "components" / "cfx_button" / "cfx_dimmer_timing.h"
)
CFX_CCT_SWEEPER_SOURCE = ROOT / "components" / "cfx_button" / "cfx_cct_sweeper.cpp"
CFX_HUE_CYCLER_SOURCE = ROOT / "components" / "cfx_button" / "cfx_hue_cycler.cpp"


class CFXSyncUDPTransportRuntimeTests(unittest.TestCase):
    def test_shared_bus_files_exist(self):
        self.assertTrue(
            BUS_HEADER.exists(),
            "cfx_sync_bus.h must declare the shared Group Bus runtime",
        )
        self.assertTrue(
            BUS_SOURCE.exists(),
            "cfx_sync_bus.cpp must define the shared Group Bus runtime",
        )

    def test_shared_bus_owns_transport_resources(self):
        self.assertTrue(BUS_HEADER.exists())
        header = BUS_HEADER.read_text(encoding="utf-8")

        self.assertIn("class CFXSyncBus", header)
        self.assertIn("void set_espnow(espnow::ESPNowComponent *espnow)", header)
        self.assertIn("bool begin_espnow();", header)
        self.assertIn("bool begin_udp(uint16_t port);", header)
        self.assertIn("void register_group(CFXSyncComponent *group);", header)
        self.assertIn("void poll();", header)
        self.assertIn("CFXSyncUDPTransport udp_;", header)
        self.assertIn("espnow::ESPNowComponent *espnow_{nullptr};", header)
        self.assertIn("CFXSyncBus &global_cfx_sync_bus();", header)

    def test_shared_bus_loads_esphome_defines_before_espnow_guards(self):
        header = BUS_HEADER.read_text(encoding="utf-8")

        defines_include = header.find('#include "esphome/core/defines.h"')
        espnow_guard = header.find("#ifdef USE_ESPNOW")

        self.assertNotEqual(
            defines_include,
            -1,
            "cfx_sync_bus.h must include ESPHome generated defines directly",
        )
        self.assertNotEqual(espnow_guard, -1)
        self.assertLess(
            defines_include,
            espnow_guard,
            "USE_ESPNOW guards must not run before ESPHome defines are visible",
        )

    def test_bus_callbacks_dispatch_packets_to_registered_groups(self):
        self.assertTrue(BUS_SOURCE.exists())
        source = BUS_SOURCE.read_text(encoding="utf-8")

        self.assertIn("CFXSyncBus &global_cfx_sync_bus()", source)
        self.assertIn("this->udp_.poll(this);", source)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncBus::dispatch_packet"
                r"\(const CFXSyncSource &source,\s*"
                r"const uint8_t \*data, size_t size\).*?"
                r"for \(auto \*group : this->groups_\).*?"
                r"group->handle_packet_\(source, data, size\)",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncBus::dispatch_unknown_packet"
                r"\(const CFXSyncSource &source,\s*"
                r"const uint8_t \*data, size_t size\).*?"
                r"group->handle_unknown_packet_\(source, data, size\)",
                re.DOTALL,
            ),
        )

    def test_bus_supports_multiple_registered_groups(self):
        header = BUS_HEADER.read_text(encoding="utf-8")
        source = BUS_SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            header,
            re.compile(r"static constexpr size_t MAX_GROUPS = [2-9][0-9]*;"),
        )
        self.assertNotIn("Only one CFX Sync group is supported", source)

    def test_bus_routes_by_packet_group_hash_before_decode(self):
        header = BUS_HEADER.read_text(encoding="utf-8")
        source = BUS_SOURCE.read_text(encoding="utf-8")
        packet_header = PACKET_HEADER.read_text(encoding="utf-8")

        self.assertIn(
            "static CFXSyncDecodeResult peek_group_hash(",
            packet_header,
        )
        self.assertIn("uint32_t group_hash() const", HEADER.read_text(encoding="utf-8"))
        self.assertRegex(
            source,
            re.compile(
                r"CFXSyncPacketCodec::peek_group_hash\(data, size, "
                r"packet_group_hash\).*?"
                r"group->group_hash\(\) != packet_group_hash.*?"
                r"continue;.*?"
                r"group->handle_packet_\(source, data, size\)",
                re.DOTALL,
            ),
        )

    def test_udp_transport_helper_files_exist(self):
        self.assertTrue(
            UDP_HEADER.exists(),
            "cfx_sync_udp.h must declare the UDP transport runtime helper",
        )
        self.assertTrue(
            UDP_SOURCE.exists(),
            "cfx_sync_udp.cpp must define the UDP transport runtime",
        )

    def test_udp_transport_helper_declares_runtime_contract(self):
        self.assertTrue(UDP_HEADER.exists())
        header = UDP_HEADER.read_text(encoding="utf-8")

        self.assertIn("class CFXSyncUDPTransport", header)
        self.assertIn("bool begin(uint16_t port);", header)
        self.assertIn("void poll(CFXSyncBus *bus);", header)
        self.assertIn(
            "bool send_broadcast(const std::vector<uint8_t> &packet);",
            header,
        )
        self.assertIn("bool is_ready() const", header)
        self.assertIn("~CFXSyncUDPTransport();", header)
        self.assertIn("void close_();", header)
        self.assertIn("int socket_fd_{-1};", header)
        self.assertIn("bool ready_{false};", header)
        self.assertIn("uint16_t port_{0};", header)

    def test_udp_transport_opens_nonblocking_broadcast_socket(self):
        self.assertTrue(UDP_SOURCE.exists())
        source = UDP_SOURCE.read_text(encoding="utf-8")

        self.assertIn("#include <lwip/inet.h>", source)
        self.assertIn("#include <lwip/netif.h>", source)
        self.assertIn("#include <lwip/sockets.h>", source)
        self.assertIn("::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)", source)
        self.assertIn("SO_REUSEADDR", source)
        self.assertIn("SO_BROADCAST", source)
        self.assertIn("::bind(this->socket_fd_", source)
        self.assertIn("O_NONBLOCK", source)
        self.assertIn("this->ready_ = true;", source)
        self.assertNotIn("UDP transport shell initialized", source)
        self.assertNotIn("return false;\n}", source.split("bool CFXSyncUDPTransport::begin", 1)[1].split("void CFXSyncUDPTransport::poll", 1)[0])

    def test_udp_transport_dispatches_received_packets_with_udp_identity(self):
        source = UDP_SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncUDPTransport::poll\(CFXSyncBus \*bus\)"
                r".*?::recvfrom\(this->socket_fd_.*?"
                r"CFXSyncSource source =\s*CFXSyncSource::from_udp"
                r"\(addr\.sin_addr\.s_addr, ntohs\(addr\.sin_port\)\);"
                r".*?bus->dispatch_packet\(source, buffer, "
                r"static_cast<size_t>\(received\)\);",
                re.DOTALL,
            ),
        )

    def test_udp_transport_sends_broadcast_datagrams(self):
        source = UDP_SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncUDPTransport::send_broadcast"
                r"\(const std::vector<uint8_t> &packet\)"
                r".*?for \(netif \*interface = netif_list;.*?"
                r"const uint32_t subnet_broadcast = "
                r"ip->addr \| ~netmask->addr;.*?"
                r"this->send_to_\(subnet_broadcast, packet\).*?"
                r"return this->send_to_\(INADDR_BROADCAST, packet\)",
                re.DOTALL,
            ),
        )

    def test_component_exposes_transport_enum_setter_and_udp_members(self):
        header = HEADER.read_text(encoding="utf-8")

        self.assertIn('#include "cfx_sync_bus.h"', header)
        self.assertIn(
            "enum class CFXSyncTransport : uint8_t {\n"
            "  CFX_SYNC_TRANSPORT_AUTO = 0,\n"
            "  CFX_SYNC_TRANSPORT_ESPNOW = 1,\n"
            "  CFX_SYNC_TRANSPORT_UDP = 2,\n"
            "};",
            header,
        )
        self.assertIn(
            "void set_transport(CFXSyncTransport transport) {\n"
            "    this->transport_ = transport;\n"
            "  }",
            header,
        )
        self.assertIn(
            "static constexpr uint16_t DEFAULT_UDP_PORT = 39580;",
            header,
        )
        self.assertIn(
            "CFXSyncTransport transport_{"
            "CFXSyncTransport::CFX_SYNC_TRANSPORT_AUTO};",
            header,
        )
        self.assertIn("CFXSyncBus *bus_{&global_cfx_sync_bus()};", header)
        self.assertNotIn("CFXSyncUDPTransport udp_;", header)
        self.assertIn("uint16_t udp_port_{DEFAULT_UDP_PORT};", header)

    def test_loop_polls_udp_transport_without_skipping_espnow_maintenance(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::loop\(\) \{\s*"
                r"if \(this->use_udp_transport_\(\)\) \{\s*"
                r"this->bus_->poll\(\);\s*"
                r"\}\s*"
                r"if \(!this->use_espnow_transport_\(\)\) \{\s*"
                r"return;",
                re.DOTALL,
            ),
        )

    def test_auto_transport_leader_enables_udp_bridge_on_esp32(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::use_udp_transport_\(\) const \{.*?"
                r"#if defined\(USE_ESP32\).*?"
                r"this->transport_ =="
                r"\s*CFXSyncTransport::CFX_SYNC_TRANSPORT_AUTO &&"
                r"\s*this->role_ == CFXSyncRole::LEADER",
                re.DOTALL,
            ),
        )
        setup_body = re.search(
            r"void CFXSyncComponent::setup\(\).*?"
            r"\nvoid CFXSyncComponent::loop\(\)",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(setup_body)
        setup_body = setup_body.group(0)
        self.assertIn("if (this->use_udp_transport_())", setup_body)
        self.assertIn("if (this->use_espnow_transport_())", setup_body)
        self.assertNotIn("} else if (this->use_espnow_transport_())", setup_body)

    def test_leader_state_fans_out_to_all_active_transports(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool send_udp_packet_(std::vector<uint8_t> &packet);", header)
        self.assertIn(
            "bool send_espnow_packet_to_(const std::array<uint8_t, 6> &mac,",
            header,
        )
        self.assertIn("bool send_state_packet_to_followers_(std::vector<uint8_t> &packet);", header)
        self.assertIn("uint32_t udp_state_sent_{0};", header)
        self.assertIn("uint32_t espnow_state_sent_{0};", header)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_to_followers_"
                r"\(.*?"
                r"if \(!this->send_state_packet_to_followers_\(packet\)\) \{"
                r"\s*return false;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_packet_to_followers_"
                r"\(\s*std::vector<uint8_t> &packet\).*?"
                r"if \(this->use_udp_transport_\(\)\).*?"
                r"this->send_udp_packet_\(packet\).*?"
                r"this->udp_state_sent_\+\+.*?"
                r"if \(this->use_espnow_transport_\(\)\).*?"
                r"this->send_espnow_packet_to_\(BROADCAST_MAC, packet\).*?"
                r"this->espnow_state_sent_\+\+",
                re.DOTALL,
            ),
        )

    def test_esp8266_controller_runtime_is_not_hidden_behind_esp32_guard(self):
        header = HEADER.read_text(encoding="utf-8")
        packet_header = PACKET_HEADER.read_text(encoding="utf-8")
        packet_source = PACKET_SOURCE.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("#ifdef USE_ESP32\n\n#include \"cfx_sync_color.h\"", header)
        self.assertIn("#if defined(USE_ESP32)", header)
        self.assertIn("#if defined(USE_ESP32) || defined(USE_ESP8266)", header)
        self.assertIn("#if defined(USE_ESP32) || defined(USE_ESP8266)", source)
        self.assertIn("#if defined(USE_ESP32) || defined(USE_ESP8266)", packet_header)
        self.assertIn("#if defined(USE_ESP32) || defined(USE_ESP8266)", packet_source)

    def test_esp8266_runtime_keeps_effect_bindings_esp32_only(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            header,
            re.compile(
                r"#if defined\(USE_ESP32\).*?"
                r'#include "cfx_sync_color.h".*?'
                r'#include "cfx_sync_effect.h".*?'
                r'#include "../cfx_button/cfx_button.h".*?'
                r"#endif",
                re.DOTALL,
            ),
        )
        self.assertIn("void add_light(light::LightState *light)", header)
        self.assertIn("#if defined(USE_ESP32)\n  void set_remote_input", header)
        self.assertIn("std::vector<light::LightState *> lights_;", header)
        self.assertIn("#if defined(USE_ESP32)\n  cfx_button::CFXButton *remote_input_", header)
        self.assertIn("#if defined(USE_ESP32)\nstatic CFXSyncTimingState", source)
        self.assertIn("#if defined(USE_ESP32)\n  void add_effect", header)
        self.assertIn("#if defined(USE_ESP32)\n  std::vector<std::vector<CFXSyncEffectEntry>> effect_catalogs_;", header)
        self.assertNotIn("\n#include <esp_err.h>\n#if defined(USE_ESP32)", source)
        self.assertRegex(
            source,
            re.compile(
                r"#if defined\(USE_ESP32\)\s*"
                r"#include <esp_err\.h>\s*"
                r"#include <esp_random\.h>",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            header,
            re.compile(
                r"#if defined\(USE_ESP32\).*?"
                r"void handle_send_result_\(esp_err_t result\);.*?"
                r"void handle_peer_send_result_\(PeerState &peer, esp_err_t result\);.*?"
                r"#endif",
                re.DOTALL,
            ),
        )
        self.assertIn("uint8_t fallback_channel_{6};", header)
        self.assertRegex(
            source,
            re.compile(
                r"#if defined\(USE_ESP32\).*?"
                r"light::LightState \*CFXSyncComponent::leader_light_"
                r".*?#endif",
                re.DOTALL,
            ),
        )

    def test_dump_config_reports_esp8266_udp_controller_transport(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            'ESP_LOGCONFIG(TAG, "  Transport: UDP (ESP8266 controller)");',
            source,
        )
        self.assertIn(
            'ESP_LOGI(TAG, "CFX Sync transport auto selected UDP");',
            source,
        )

    def test_udp_transport_uses_wifiudp_on_esp8266(self):
        header = UDP_HEADER.read_text(encoding="utf-8")
        source = UDP_SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            header,
            re.compile(
                r"#if defined\(USE_ESP8266\).*?"
                r"#include <WiFiUdp\.h>.*?"
                r"#endif",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            header,
            re.compile(
                r"#if defined\(USE_ESP8266\).*?"
                r"WiFiUDP udp_;.*?"
                r"#else.*?"
                r"int socket_fd_\{-1\};.*?"
                r"#endif",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"#if defined\(USE_ESP8266\).*?"
                r"this->udp_\.begin\(port\).*?"
                r"this->udp_\.parsePacket\(\).*?"
                r"this->udp_\.beginPacket\(broadcast, this->port_\)",
                re.DOTALL,
            ),
        )

    def test_esp8266_shared_parser_does_not_call_esp32_leader_helpers(self):
        source = SOURCE.read_text(encoding="utf-8")
        decoded_body = re.search(
            r"bool CFXSyncComponent::handle_decoded_packet_\(.*?"
            r"\n\}\n\nvoid CFXSyncComponent::on_local_light_update",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(decoded_body)
        decoded_source = decoded_body.group(0)
        self.assertRegex(
            decoded_source,
            re.compile(
                r"#if defined\(USE_ESP32\).*?"
                r"this->send_state_\(\);.*?"
                r"#endif.*?"
                r"if \(packet\.type == CFXSyncPacketType::SYNC_REQUEST\)",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            decoded_source,
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::INPUT_STATE\) \{"
                r"\s*#if defined\(USE_ESP32\).*?"
                r"this->handle_remote_input_\(.*?"
                r"#endif\s*return true;\s*\}",
                re.DOTALL,
            ),
        )
        pending_ack = re.search(
            r"uint8_t CFXSyncComponent::pending_ack_count_\(\) const.*?"
            r"\n\}\n\nuint32_t CFXSyncComponent::missed_ack_count_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(pending_ack)
        self.assertIn("#if defined(USE_ESP32)", pending_ack.group(0))
        self.assertIn("return 0;", pending_ack.group(0))

    def test_dump_config_reports_active_transport_selection(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("const char *transport_name_() const;", HEADER.read_text(encoding="utf-8"))
        self.assertIn("const char *CFXSyncComponent::transport_name_() const", source)
        self.assertIn('ESP_LOGCONFIG(TAG, "  Transport: %s", this->transport_name_());', source)
        self.assertIn('return "ESP-NOW + UDP bridge";', source)
        self.assertIn('return "ESP-NOW";', source)
        self.assertIn('return "UDP";', source)


class CFXSyncTransportBoundaryTests(unittest.TestCase):
    def test_transport_header_declares_shared_source_identity(self):
        self.assertTrue(
            TRANSPORT_HEADER.exists(),
            "cfx_sync_transport.h must expose transport-neutral source identity",
        )
        header = TRANSPORT_HEADER.read_text(encoding="utf-8")

        self.assertIn(
            "enum class CFXSyncTransportKind : uint8_t { ESPNOW = 0, UDP = 1 };",
            header,
        )
        self.assertIn("struct CFXSyncSource", header)
        self.assertIn(
            "CFXSyncTransportKind transport{CFXSyncTransportKind::ESPNOW};",
            header,
        )
        self.assertIn("std::array<uint8_t, 6> mac{{0, 0, 0, 0, 0, 0}};", header)
        self.assertIn("uint32_t ipv4{0};", header)
        self.assertIn("uint16_t port{0};", header)
        self.assertIn("bool identity_valid{false};", header)
        self.assertIn(
            "static CFXSyncSource from_espnow(const uint8_t *mac_addr)", header
        )
        self.assertIn(
            "static CFXSyncSource from_udp(uint32_t ipv4_addr, uint16_t udp_port)",
            header,
        )
        self.assertRegex(
            header,
            re.compile(
                r"static CFXSyncSource from_espnow\(const uint8_t \*mac_addr\)"
                r".*?if \(mac_addr != nullptr\).*?"
                r"source\.identity_valid = true;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            header,
            re.compile(
                r"static CFXSyncSource from_udp\(uint32_t ipv4_addr, "
                r"uint16_t udp_port\).*?"
                r"source\.identity_valid = true;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            header,
            re.compile(
                r"const uint8_t \*espnow_mac_or_null\(\) const.*?"
                r"if \(this->transport != CFXSyncTransportKind::ESPNOW \|\|"
                r"\s*!this->identity_valid\).*?"
                r"return nullptr;.*?"
                r"return this->mac\.data\(\);",
                re.DOTALL,
            ),
        )

    def test_runtime_header_uses_shared_transport_packet_signatures(self):
        header = HEADER.read_text(encoding="utf-8")

        self.assertIn('#include "cfx_sync_transport.h"', header)
        self.assertIn(
            "bool handle_packet_(const CFXSyncSource &source, const uint8_t *data,",
            header,
        )
        self.assertIn(
            "bool handle_decoded_packet_(const CFXSyncSource &source,",
            header,
        )
        self.assertNotIn(
            "bool handle_packet_(const espnow::ESPNowRecvInfo &info,",
            header,
        )
        self.assertNotIn(
            "bool handle_decoded_packet_(const espnow::ESPNowRecvInfo &info,",
            header,
        )

    def test_espnow_callbacks_wrap_source_before_packet_decode(self):
        source = BUS_SOURCE.read_text(encoding="utf-8")

        for callback in ("on_received", "on_receive", "on_broadcasted", "on_broadcast"):
            with self.subTest(callback=callback):
                self.assertRegex(
                    source,
                    re.compile(
                        rf"bool CFXSyncBus::{callback}\(.*?\).*?"
                        r"CFXSyncSource source = "
                        r"CFXSyncSource::from_espnow\(info\.src_addr\);.*?"
                        r"return this->dispatch_packet\(source, data, size\);",
                        re.DOTALL,
                    ),
                )
        self.assertIn(
            "return this->handle_decoded_packet_(source, packet);",
            SOURCE.read_text(encoding="utf-8"),
        )
        self.assertIn("CFXSyncSource::from_espnow(info.src_addr)", source)

    def test_decoded_packet_uses_transport_neutral_peer_identity(self):
        source = SOURCE.read_text(encoding="utf-8")
        header = HEADER.read_text(encoding="utf-8")

        self.assertIn("bool peer_matches_source_(const PeerState &peer,", header)
        self.assertIn("PeerState *find_peer_(const CFXSyncSource &source);", header)
        self.assertIn(
            "PeerState *find_or_add_peer_(const CFXSyncSource &source,",
            header,
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::handle_decoded_packet_\(.*?"
                r"if \(!source\.identity_valid\) \{"
                r"\s*this->log_rejection_"
                r"\(\"Ignoring packet without source identity\"\);"
                r"\s*return true;"
                r"\s*\}"
                r"\s*auto \*peer = this->find_peer_\(source\);",
                re.DOTALL,
            ),
        )
        self.assertNotIn("Ignoring packet without ESP-NOW source identity", source)


class ESPNowAPITests(unittest.TestCase):
    def _effect_header_text(self):
        self.assertTrue(
            EFFECT_HEADER.exists(),
            "cfx_sync_effect.h must define the effect identity model",
        )
        return EFFECT_HEADER.read_text(encoding="utf-8")

    def test_uses_version_conditional_receive_api(self):
        header = BUS_HEADER.read_text(encoding="utf-8")
        source = BUS_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "esphome/core/version.h"', header)
        self.assertIn(
            "#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)",
            header,
        )
        self.assertIn(
            "public espnow::ESPNowReceivedPacketHandler,\n"
            "      public espnow::ESPNowUnknownPeerHandler,\n"
            "#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)\n"
            "      public espnow::ESPNowBroadcastedHandler",
            header,
        )
        self.assertIn("bool on_received(", header)
        self.assertIn("bool on_receive(", header)
        self.assertIn("bool on_unknown_peer(", header)
        self.assertIn("bool dispatch_packet(", header)
        self.assertIn(
            "#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)",
            source,
        )
        self.assertIn("register_received_handler(this)", source)
        self.assertIn("register_receive_handler(this)", source)
        self.assertIn("register_unknown_peer_handler(this)", source)
        self.assertIn("CFXSyncBus::on_received(", source)
        self.assertIn("CFXSyncBus::on_receive(", source)
        self.assertIn("CFXSyncBus::on_unknown_peer(", source)
        self.assertIn("CFXSyncBus::dispatch_packet(", source)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncBus::on_received\(.*?\).*?"
                r"CFXSyncSource source = "
                r"CFXSyncSource::from_espnow\(info\.src_addr\);.*?"
                r"return this->dispatch_packet\(source, data, size\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncBus::on_receive\(.*?\).*?"
                r"CFXSyncSource source = "
                r"CFXSyncSource::from_espnow\(info\.src_addr\);.*?"
                r"return this->dispatch_packet\(source, data, size\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncBus::on_unknown_peer\(.*?\).*?"
                r"return this->dispatch_unknown_packet\(source, data, size\);",
                re.DOTALL,
            ),
        )

    def test_registers_version_conditional_broadcast_handler_for_discovery(self):
        header = BUS_HEADER.read_text(encoding="utf-8")
        source = BUS_SOURCE.read_text(encoding="utf-8")

        self.assertIn("public espnow::ESPNowBroadcastedHandler", header)
        self.assertIn("public espnow::ESPNowBroadcastHandler", header)
        self.assertIn("bool on_broadcasted(", header)
        self.assertIn("bool on_broadcast(", header)
        self.assertIn("register_broadcasted_handler(this)", source)
        self.assertIn("register_broadcast_handler(this)", source)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncBus::on_broadcasted\(.*?\).*?"
                r"CFXSyncSource source = "
                r"CFXSyncSource::from_espnow\(info\.src_addr\);.*?"
                r"return this->dispatch_packet\(source, data, size\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncBus::on_broadcast\(.*?\).*?"
                r"CFXSyncSource source = "
                r"CFXSyncSource::from_espnow\(info\.src_addr\);.*?"
                r"return this->dispatch_packet\(source, data, size\);",
                re.DOTALL,
            ),
        )

    def test_unknown_peers_are_authenticated_then_dispatched_directly(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool handle_unknown_packet_(const CFXSyncSource &source,", header)
        self.assertIn(
            "bool handle_decoded_packet_(const CFXSyncSource &source,",
            header,
        )

        admit_body = re.search(
            r"bool CFXSyncComponent::handle_unknown_packet_\(.*?"
            r"\n\}\n\nbool CFXSyncComponent::handle_packet_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(admit_body)
        self.assertNotIn(
            "this->find_or_add_peer_(info.src_addr",
            admit_body.group(0),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::handle_unknown_packet_\(.*?"
                r"CFXSyncPacketCodec::decode\(.*?"
                r"result == CFXSyncDecodeResult::NOT_CFX.*?"
                r"return false;.*?"
                r"result != CFXSyncDecodeResult::OK.*?"
                r"this->handle_decode_failure_\(result\);.*?"
                r"return true;.*?"
                r"packet\.type != CFXSyncPacketType::HELLO &&"
                r"\s*packet\.type != CFXSyncPacketType::SYNC_REQUEST.*?"
                r"authenticated non-discovery packet from unknown peer.*?"
                r"return this->handle_decoded_packet_\(source, packet\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::handle_packet_\(.*?"
                r"CFXSyncPacketCodec::decode\(.*?"
                r"if \(result != CFXSyncDecodeResult::OK\) \{"
                r"\s*this->handle_decode_failure_\(result\);"
                r"\s*return true;"
                r"\s*\}"
                r"\s*return this->handle_decoded_packet_\(source, packet\);"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_wrong_group_unknown_cfx_packets_are_logged_but_not_consumed(self):
        source = SOURCE.read_text(encoding="utf-8")
        admit_body = re.search(
            r"bool CFXSyncComponent::handle_unknown_packet_\(.*?"
            r"\n\}\n\nbool CFXSyncComponent::handle_packet_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(admit_body)
        self.assertNotIn(
            "result == CFXSyncDecodeResult::NOT_CFX ||",
            admit_body.group(0),
        )

        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::handle_unknown_packet_\(.*?"
                r"if \(result == CFXSyncDecodeResult::NOT_CFX\) \{"
                r"\s*return false;"
                r"\s*\}.*?"
                r"if \(result == CFXSyncDecodeResult::WRONG_GROUP\) \{"
                r"\s*this->handle_decode_failure_\(result\);"
                r"\s*return false;"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_schema_uses_singleton_espnow_and_controller_inputs(self):
        py_source = PY_COMPONENT.read_text(encoding="utf-8")

        self.assertIn(
            '"binary_sensor",',
            py_source,
        )
        self.assertIn(
            '"cfx_button",',
            py_source,
        )
        self.assertNotIn('"cfx_dimmer",', py_source)
        self.assertIn('return BASE_AUTO_LOAD + ["espnow"]', py_source)
        self.assertIn(
            '"light",',
            py_source,
        )
        self.assertIn(
            '"number",',
            py_source,
        )
        self.assertIn(
            '"select",',
            py_source,
        )
        self.assertIn(
            '"switch",',
            py_source,
        )
        self.assertIn("DEPENDENCIES = []", py_source)
        self.assertIn('cv.only_on(["esp32", "esp8266"])', py_source)
        self.assertIn("CONF_INTERNAL_ESPNOW_ID", py_source)
        self.assertRegex(
            py_source,
            re.compile(
                r"cv\.GenerateID\(CONF_INTERNAL_ESPNOW_ID\): "
                r"cv\.use_id\(\s*espnow\.ESPNowComponent\s*\)",
                re.DOTALL,
            ),
        )
        self.assertIn("cv.Optional(CONF_ESPNOW_ID): cv.invalid(", py_source)
        self.assertIn('cv.Optional(CONF_PEER): cv.invalid(', py_source)
        self.assertIn('CONF_LOCAL_INPUT = "local_input"', py_source)
        self.assertIn('CONF_REMOTE_INPUT = "remote_input"', py_source)
        self.assertIn('ROLE_CONTROLLER = "controller"', py_source)
        self.assertIn('ROLE_SATELLITE = "satellite"', py_source)
        self.assertIn(
            "cv.Optional(CONF_LOCAL_INPUT): cv.use_id(binary_sensor.BinarySensor)",
            py_source,
        )
        self.assertIn(
            "cv.Optional(CONF_REMOTE_INPUT): cv.use_id(cfx_button.CFXButton)",
            py_source,
        )
        self.assertIn(
            "ROLE_CONTROLLER: CFXSyncRole.CFX_SYNC_ROLE_CONTROLLER",
            py_source,
        )
        self.assertIn("ROLE_SATELLITE: CFXSyncRole.SATELLITE", py_source)
        self.assertNotIn("cv.Required(CONF_PEER)", py_source)

    def test_codegen_uses_singleton_espnow_and_input_bindings(self):
        py_source = PY_COMPONENT.read_text(encoding="utf-8")

        self.assertRegex(
            py_source,
            re.compile(
                r"espnow_var = await cg\.get_variable\(config\[CONF_INTERNAL_ESPNOW_ID\]\).*?"
                r'cg\.add_define\("USE_ESPNOW"\).*?'
                r"cg\.add\(espnow_var\.set_auto_add_peer\(False\)\).*?"
                r"cg\.add\(var\.set_espnow\(espnow_var\)\)",
                re.DOTALL,
            ),
        )
        self.assertNotIn("cg.new_Pvariable(config[CONF_ESPNOW_ID])", py_source)
        self.assertNotIn("await cg.register_component(espnow_var, {})", py_source)
        self.assertNotIn("require_wake_loop_threadsafe", py_source)
        self.assertNotIn("cg.add(var.set_peer", py_source)
        self.assertNotIn("espnow_var.add_peer(peer.parts)", py_source)
        self.assertIn(
            "local_input = await cg.get_variable(config[CONF_LOCAL_INPUT])",
            py_source,
        )
        self.assertIn("cg.add(var.set_local_input(local_input))", py_source)
        self.assertIn(
            "remote_input = await cg.get_variable(config[CONF_REMOTE_INPUT])",
            py_source,
        )
        self.assertIn("cg.add(var.set_remote_input(remote_input))", py_source)
        self.assertIn("CONF_FALLBACK_CHANNEL = \"fallback_channel\"", py_source)
        self.assertIn("CONF_INPUT_MODE = \"input_mode\"", py_source)
        self.assertIn(
            "CFXSyncInputMode = cfx_sync_ns.enum(\"CFXSyncInputMode\", is_class=True)",
            py_source,
        )
        self.assertIn("DEFAULT_FALLBACK_CHANNEL = 6", py_source)
        self.assertIn("INPUT_MODE_MOMENTARY = \"momentary\"", py_source)
        self.assertIn("INPUT_MODE_MAINTAINED = \"maintained\"", py_source)
        self.assertIn(
            "cv.Optional(CONF_FALLBACK_CHANNEL, default=DEFAULT_FALLBACK_CHANNEL)",
            py_source,
        )
        self.assertIn(
            "cv.Optional(CONF_INPUT_MODE, default=INPUT_MODE_MOMENTARY)",
            py_source,
        )
        self.assertIn(
            "cg.add(var.set_fallback_channel(config[CONF_FALLBACK_CHANNEL]))",
            py_source,
        )
        self.assertIn(
            "cg.add(var.set_input_mode(INPUT_MODE_MAP[config[CONF_INPUT_MODE]]))",
            py_source,
        )

    def test_followers_get_generated_enable_sync_switch(self):
        py_source = PY_COMPONENT.read_text(encoding="utf-8")
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("CFXSyncEnableSwitch", header)
        self.assertIn("void write_state(bool state) override;", header)
        self.assertIn("void on_sync_enabled_switch(bool enabled);", header)
        self.assertIn("bool sync_enabled_{true};", header)
        self.assertIn('CONF_SYNC_SWITCH_ID = "_sync_switch_id"', py_source)
        self.assertIn(
            "CFXSyncEnableSwitch = cfx_sync_ns.class_(\n"
            '    "CFXSyncEnableSwitch", switch.Switch\n'
            ")",
            py_source,
        )
        self.assertIn(
            "cv.GenerateID(CONF_SYNC_SWITCH_ID): cv.declare_id(\n"
            "                CFXSyncEnableSwitch\n"
            "            )",
            py_source,
        )
        self.assertRegex(
            py_source,
            re.compile(
                r"config\[CONF_ROLE\] in \(ROLE_FOLLOWER, ROLE_SATELLITE\)"
                r".*?CONF_SYNC_SWITCH_ID in config.*?"
                r"CONF_NAME: \"Enable Sync\".*?"
                r"CONF_DISABLED_BY_DEFAULT: False,.*?"
                r"CONF_INTERNAL: False,.*?"
                r"CONF_RESTORE_MODE: cg\.RawExpression"
                r"\(\s*\"switch_::SWITCH_RESTORE_DEFAULT_ON\"\s*\).*?"
                r"await switch\.register_switch\(sync_switch, switch_conf\).*?"
                r"cg\.add\(sync_switch\.set_parent\(var\)\).*?"
                r"cg\.add\(var\.set_sync_switch\(sync_switch\)\)",
                re.DOTALL,
            ),
        )
        self.assertIn(
            "void CFXSyncEnableSwitch::write_state(bool state)",
            source,
        )
        self.assertIn("this->parent_->on_sync_enabled_switch(state);", source)
        self.assertRegex(
            source,
            re.compile(
                r"if \(this->is_state_receiver_role_\(\) && "
                r"this->sync_switch_ != nullptr\) \{"
                r"\s*const auto initial_state ="
                r"\s*this->sync_switch_->get_initial_state_with_restore_mode\(\);"
                r"\s*this->sync_enabled_ = initial_state\.has_value\(\)"
                r"\s*\? initial_state\.value\(\) : true;"
                r"\s*this->sync_switch_->publish_state\(this->sync_enabled_\);"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_disabled_follower_ignores_state_and_resyncs_on_enable(self):
        source = SOURCE.read_text(encoding="utf-8")
        header = HEADER.read_text(encoding="utf-8")

        self.assertIn("void schedule_enable_resync_();", header)
        self.assertIn("void schedule_enable_resync_attempt_(", header)
        self.assertRegex(
            source,
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::STATE &&"
                r"\s*this->is_state_receiver_role_\(\) &&"
                r"\s*!this->sync_enabled_\) \{"
                r"\s*this->log_rejection_\(\"Ignoring STATE while sync is disabled\"\);"
                r"\s*return true;"
                r"\s*\}",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::on_sync_enabled_switch\(bool enabled\).*?"
                r"this->sync_enabled_ = enabled;.*?"
                r"this->has_valid_state_ = false;.*?"
                r"this->clear_warning_if_set_\(\);.*?"
                r"if \(enabled\).*?"
                r"this->schedule_enable_resync_\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_enable_resync_\(\).*?"
                r"this->send_sync_request_to_\(BROADCAST_MAC\);.*?"
                r"this->schedule_enable_resync_attempt_"
                r"\(\"enable-sync-1\", 1000\);.*?"
                r"this->schedule_enable_resync_attempt_"
                r"\(\"enable-sync-2\", 2000\);.*?"
                r"this->schedule_enable_resync_attempt_"
                r"\(\"enable-sync-4\", 4000\);",
                re.DOTALL,
            ),
        )

    def test_cfx_button_can_host_remote_input_without_local_binary_sensor(self):
        py_source = CFX_BUTTON_PY.read_text(encoding="utf-8")
        header = CFX_BUTTON_HEADER.read_text(encoding="utf-8")
        source = CFX_BUTTON_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "cv.Optional(CONF_BUTTON): cv.use_id(binary_sensor.BinarySensor)",
            py_source,
        )
        self.assertIn('AUTO_LOAD = ["binary_sensor"]', py_source)
        self.assertIn("void inject_remote_state(bool pressed)", header)
        self.assertNotIn("button_ == nullptr ||", source)
        self.assertRegex(
            source,
            re.compile(
                r"this->bind_button_\(\s*"
                r"this->button_, &this->state_,\s*"
                r"\[this\]\(bool pressed\) \{\s*"
                r"this->handle_state_\(pressed\);\s*"
                r"\}\);",
                re.DOTALL,
            ),
        )

    def test_remote_cfx_button_press_is_not_swallowed_by_startup_arming(self):
        header = CFX_BUTTON_HEADER.read_text(encoding="utf-8")
        source = CFX_BUTTON_SOURCE.read_text(encoding="utf-8")
        state_header = CFX_BUTTON_STATE_HEADER.read_text(encoding="utf-8")

        self.assertIn("void prime(bool pressed)", state_header)
        self.assertIn("CFXButtonState remote_state_;", header)
        self.assertIn("this->remote_input_->inject_remote_state(pressed);", SOURCE.read_text(encoding="utf-8"))
        self.assertRegex(
            source,
            re.compile(
                r"void CFXButton::inject_remote_state\(bool pressed\).*?"
                r"if \(!this->remote_state_\.is_armed\(\)\) \{\s*"
                r"this->remote_state_\.prime\(!pressed\);\s*"
                r"\}.*?"
                r"this->handle_state_\(pressed, &this->remote_state_\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXButton::handle_state_"
                r"\(bool pressed, CFXButtonState \*state\).*?"
                r"state->on_state\(pressed\)",
                re.DOTALL,
            ),
        )

    def test_controller_role_is_declared_in_runtime_contract(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("CFX_SYNC_ROLE_CONTROLLER", header)
        self.assertIn("CFX_SYNC_INPUT_MOMENTARY", header)
        self.assertIn("CFX_SYNC_INPUT_MAINTAINED", header)
        self.assertIn('#include "../cfx_button/cfx_button.h"', header)
        self.assertNotIn("esphome/components/cfx_button/cfx_button.h", header)
        self.assertIn("set_local_input(binary_sensor::BinarySensor *input)", header)
        self.assertIn("set_remote_input(cfx_button::CFXButton *input)", header)
        self.assertIn("void set_input_mode(CFXSyncInputMode mode)", header)
        self.assertIn("CFXSyncNodeRole::REMOTE", source)
        self.assertIn("CAP_BINARY_REMOTE", source)
        self.assertIn('return "controller"', source)

    def test_satellite_role_is_follower_plus_input_contract(self):
        header = HEADER.read_text(encoding="utf-8")
        packet_header = PACKET_HEADER.read_text(encoding="utf-8")
        packet_source = PACKET_SOURCE.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("SATELLITE = 3", header)
        self.assertIn("SATELLITE = 4", packet_header)
        self.assertIn("case CFXSyncNodeRole::SATELLITE:", packet_source)
        self.assertIn("CFXSyncNodeRole::SATELLITE", source)
        self.assertRegex(
            source,
            re.compile(
                r"this->role_ == CFXSyncRole::SATELLITE.*?"
                r"return CFXSyncNodeRole::SATELLITE;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"this->role_ == CFXSyncRole::SATELLITE.*?"
                r"CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER \|"
                r"\s*CFXSyncPacketCodec::CAP_BINARY_REMOTE",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"peer\.node_role == CFXSyncNodeRole::FOLLOWER \|\|"
                r"\s*peer\.node_role == CFXSyncNodeRole::SATELLITE",
                re.DOTALL,
            ),
        )
        self.assertIn(
            'this->role_ == CFXSyncRole::SATELLITE ? "Satellite" : "Controller"',
            source,
        )
        self.assertIn('ESP_LOGD(TAG, "Satellite applying leader state")', source)
        self.assertRegex(
            source,
            re.compile(
                r"if \(!this->is_input_sender_role_\(\)\) \{\s*"
                r"return false;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"light::LightState \*CFXSyncComponent::leader_light_\(\) const"
                r".*?this->role_ != CFXSyncRole::LEADER.*?"
                r"return nullptr;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"if \(this->role_ == CFXSyncRole::LEADER\) \{.*?"
                r"leader->add_remote_values_listener\(&this->light_listener_\);"
                r".*?\} else if \(this->is_state_receiver_role_\(\)\)",
                re.DOTALL,
            ),
        )

    def test_input_state_carries_maintained_mode_without_growing_packet(self):
        header = HEADER.read_text(encoding="utf-8")
        packet_header = PACKET_HEADER.read_text(encoding="utf-8")
        packet_source = PACKET_SOURCE.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool input_maintained{false};", packet_header)
        self.assertIn("bool input_toggle{false};", packet_header)
        self.assertIn("static constexpr uint8_t INPUT_FLAG_PRESSED = 0x01;", packet_header)
        self.assertIn("static constexpr uint8_t INPUT_FLAG_MAINTAINED = 0x02;", packet_header)
        self.assertIn("static constexpr uint8_t INPUT_FLAG_TOGGLE = 0x04;", packet_header)
        self.assertIn("static constexpr size_t INPUT_STATE_PAYLOAD_SIZE = 1;", packet_header)
        self.assertIn("static_assert(CFXSyncPacketCodec::INPUT_STATE_PACKET_SIZE == 39", packet_header)
        self.assertRegex(
            packet_header,
            re.compile(
                r"static bool encode_input_state\(uint32_t group_hash, uint32_t boot_id,"
                r"\s*uint32_t sequence, bool pressed,\s*bool maintained,"
                r"\s*bool toggle,"
                r"\s*const std::array<uint8_t, 32> &key,"
                r"\s*std::vector<uint8_t> &output\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            packet_source,
            re.compile(
                r"const uint8_t payload = \(pressed \? INPUT_FLAG_PRESSED : 0\) \|"
                r"\s*\(maintained \? INPUT_FLAG_MAINTAINED : 0\) \|"
                r"\s*\(toggle \? INPUT_FLAG_TOGGLE : 0\);.*?"
                r"payload\[0\] & ~\(INPUT_FLAG_PRESSED \| INPUT_FLAG_MAINTAINED \|"
                r"\s*INPUT_FLAG_TOGGLE\).*?"
                r"packet\.input_pressed = \(payload\[0\] & INPUT_FLAG_PRESSED\) != 0;.*?"
                r"packet\.input_maintained ="
                r"\s*\(payload\[0\] & INPUT_FLAG_MAINTAINED\) != 0;.*?"
                r"packet\.input_toggle ="
                r"\s*\(payload\[0\] & INPUT_FLAG_TOGGLE\) != 0;",
                re.DOTALL,
            ),
        )
        self.assertIn("CFXSyncInputMode input_mode_{CFXSyncInputMode::CFX_SYNC_INPUT_MOMENTARY};", header)
        self.assertIn("this->input_mode_ == CFXSyncInputMode::CFX_SYNC_INPUT_MAINTAINED", source)
        self.assertRegex(
            source,
            re.compile(
                r"this->send_input_state_\(pressed, maintained,\s*toggle\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"const bool applied = this->handle_remote_input_\(\s*"
                r"\*peer, packet\.input_pressed, packet\.input_maintained,\s*"
                r"packet\.input_toggle\);",
                re.DOTALL,
            ),
        )
        self.assertIn("INPUT_MAINTAINED_SETTLE_MS", header)
        self.assertIn("uint32_t local_input_maintained_generation_{0};", header)
        self.assertIn('this->set_timeout("input-maintained-settle"', source)
        self.assertIn("const uint32_t generation = ++this->local_input_maintained_generation_;", source)
        self.assertIn("[this, pressed, maintained, toggle, generation]", source)
        self.assertIn("void queue_input_state_(bool pressed, bool maintained, bool toggle);", header)
        self.assertIn("void flush_deferred_input_();", header)
        self.assertIn("PendingInputEvent pending_input_events_[PENDING_INPUT_QUEUE_SIZE];", header)
        self.assertIn("static constexpr uint8_t PENDING_INPUT_QUEUE_SIZE = 8;", header)
        self.assertIn("uint32_t local_input_repeat_generation_{0};", header)
        self.assertIn(
            "void schedule_local_input_hold_repeat_(uint32_t generation);",
            header,
        )
        self.assertRegex(
            header,
            re.compile(
                r"void schedule_local_input_release_repeat_"
                r"\(uint8_t remaining,\s*uint32_t generation\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_input_state_"
                r"\(bool pressed, bool maintained,\s*bool toggle\).*?"
                r"if \(this->send_pending_\) \{\s*"
                r"this->queue_input_state_\(pressed, maintained, toggle\);"
                r"\s*return false;\s*\}",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"const uint32_t repeat_generation ="
                r"\s*\+\+this->local_input_repeat_generation_;.*?"
                r"this->send_input_state_\(pressed, maintained, false\);.*?"
                r"if \(pressed\) \{\s*"
                r"this->schedule_local_input_hold_repeat_\(repeat_generation\);"
                r"\s*\} else \{\s*"
                r"this->schedule_local_input_release_repeat_"
                r"\(INPUT_RELEASE_REPEAT_COUNT,\s*repeat_generation\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_local_input_hold_repeat_"
                r"\(\s*uint32_t generation\).*?"
                r"generation != this->local_input_repeat_generation_.*?"
                r"this->schedule_local_input_hold_repeat_\(generation\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_local_input_release_repeat_"
                r"\(\s*uint8_t remaining,\s*uint32_t generation\).*?"
                r"generation != this->local_input_repeat_generation_.*?"
                r"this->local_input_pressed_.*?"
                r"this->schedule_local_input_release_repeat_"
                r"\(\s*remaining - 1,\s*generation\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"this->send_pending_ = false;\s*"
                r"this->handle_send_result_\(send_result\);.*?"
                r"this->flush_deferred_input_\(\);",
                re.DOTALL,
            ),
        )
        inject_remote_input = re.search(
            r"bool CFXSyncComponent::inject_remote_input_"
            r"\(bool pressed, bool maintained,\s*bool toggle\).*?"
            r"\nvoid CFXSyncComponent::apply_remote_power_input_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(inject_remote_input)
        inject_remote_input = inject_remote_input.group(0)
        self.assertRegex(
            inject_remote_input,
            re.compile(r"if \(toggle\) \{.*?return true;\s*\}", re.DOTALL),
        )
        self.assertRegex(
            inject_remote_input,
            re.compile(
                r"if \(pressed && !maintained\) \{\s*"
                r"this->schedule_remote_input_timeout_\(\);",
                re.DOTALL,
            ),
        )

    def test_udp_input_state_retries_same_packet_without_new_sequence(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("static constexpr uint32_t UDP_INPUT_RETRY_DELAY_MS = 25;", header)
        self.assertIn("static constexpr uint8_t UDP_INPUT_RETRY_COUNT = 1;", header)
        self.assertIn(
            "void schedule_udp_input_retry_(std::vector<uint8_t> packet, uint8_t remaining);",
            header,
        )
        send_input_state = re.search(
            r"bool CFXSyncComponent::send_input_state_"
            r"\(bool pressed, bool maintained,\s*bool toggle\).*?"
            r"\nvoid CFXSyncComponent::queue_input_state_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(send_input_state)
        send_input_state = send_input_state.group(0)
        self.assertIn("const uint32_t sequence = this->next_sequence_();", send_input_state)
        self.assertRegex(
            send_input_state,
            re.compile(
                r"CFXSyncPacketCodec::encode_input_state\(.*?"
                r"this->group_hash_, this->boot_id_, sequence,",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            send_input_state,
            re.compile(
                r"const bool sent = this->send_packet_to_"
                r"\(BROADCAST_MAC, packet\);.*?"
                r"if \(sent && this->use_udp_transport_\(\)\) \{.*?"
                r"this->schedule_udp_input_retry_\(packet, UDP_INPUT_RETRY_COUNT\);.*?"
                r"\}.*?return sent;",
                re.DOTALL,
            ),
        )

        retry = re.search(
            r"void CFXSyncComponent::schedule_udp_input_retry_"
            r"\(std::vector<uint8_t> packet,\s*uint8_t remaining\).*?"
            r"\nvoid CFXSyncComponent::queue_input_state_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(retry)
        retry = retry.group(0)
        self.assertIn('this->set_timeout("udp-input-retry"', retry)
        self.assertIn("UDP_INPUT_RETRY_DELAY_MS", retry)
        self.assertIn("[this, packet, remaining]() mutable", retry)
        self.assertIn("this->send_packet_to_(BROADCAST_MAC, packet)", retry)
        self.assertNotIn("this->next_sequence_()", retry)
        self.assertNotIn("encode_input_state", retry)

    def test_udp_input_latency_counters_are_reported(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        for field in (
            "uint32_t udp_input_sent_{0};",
            "uint32_t udp_input_retried_{0};",
            "uint32_t udp_input_received_{0};",
            "uint32_t udp_input_applied_{0};",
        ):
            self.assertIn(field, header)
        self.assertIn('"  UDP input: sent=%" PRIu32 " retried=%" PRIu32', source)
        self.assertIn('" received=%" PRIu32 " applied=%" PRIu32', source)
        self.assertIn("this->udp_input_sent_", source)
        self.assertIn("this->udp_input_retried_", source)
        self.assertIn("this->udp_input_received_", source)
        self.assertIn("this->udp_input_applied_", source)
        self.assertRegex(
            source,
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::INPUT_STATE\).*?"
                r"const bool applied = this->handle_remote_input_\(\s*"
                r"\*peer, packet\.input_pressed, packet\.input_maintained,\s*"
                r"packet\.input_toggle\);.*?"
                r"if \(source\.transport == CFXSyncTransportKind::UDP\) \{\s*"
                r"this->udp_input_received_\+\+;\s*"
                r"\}.*?"
                r"if \(source\.transport == CFXSyncTransportKind::UDP && applied\) \{\s*"
                r"this->udp_input_applied_\+\+;",
                re.DOTALL,
            ),
        )

    def test_remote_inputs_have_default_leader_light_actions(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("void apply_remote_power_input_(bool pressed);", header)
        self.assertIn("void apply_remote_toggle_input_();", header)
        self.assertNotIn(
            "Received CFX Sync remote input but no remote_input is ",
            source,
        )
        self.assertRegex(
            source,
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::INPUT_STATE\).*?"
                r"const bool applied = this->handle_remote_input_\(\s*"
                r"\*peer, packet\.input_pressed, packet\.input_maintained,\s*"
                r"packet\.input_toggle\);",
                re.DOTALL,
            ),
        )
        inject_remote_input = re.search(
            r"bool CFXSyncComponent::inject_remote_input_"
            r"\(bool pressed, bool maintained,\s*bool toggle\).*?"
            r"\nvoid CFXSyncComponent::apply_remote_power_input_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(inject_remote_input)
        inject_remote_input = inject_remote_input.group(0)
        self.assertRegex(
            inject_remote_input,
            re.compile(
                r"if \(toggle\) \{\s*"
                r"this->apply_remote_toggle_input_\(\);\s*"
                r"return true;\s*\}",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            inject_remote_input,
            re.compile(
                r"if \(maintained\) \{\s*"
                r"this->apply_remote_power_input_\(pressed\);\s*"
                r"return true;\s*\}",
                re.DOTALL,
            ),
        )
        self.assertIn("if (this->remote_input_ == nullptr)", inject_remote_input)
        self.assertIn("this->apply_remote_toggle_input_();", inject_remote_input)
        self.assertIn("this->remote_input_->inject_remote_state(pressed);", inject_remote_input)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::apply_remote_power_input_"
                r"\(bool pressed\).*?"
                r"auto \*leader = this->leader_light_\(\);.*?"
                r"auto call = leader->make_call\(\);.*?"
                r"call\.set_state\(pressed\);.*?"
                r"call\.perform\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::apply_remote_toggle_input_\(\).*?"
                r"auto \*leader = this->leader_light_\(\);.*?"
                r"auto call = leader->make_call\(\);.*?"
                r"call\.set_state\(!leader->remote_values\.is_on\(\)\);.*?"
                r"call\.perform\(\);",
                re.DOTALL,
            ),
        )

    def test_duplicate_remote_release_does_not_reenter_magic_button(self):
        source = SOURCE.read_text(encoding="utf-8")

        inject_remote_input = re.search(
            r"bool CFXSyncComponent::inject_remote_input_"
            r"\(bool pressed, bool maintained,\s*bool toggle\).*?"
            r"\nvoid CFXSyncComponent::apply_remote_power_input_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(inject_remote_input)
        inject_remote_input = inject_remote_input.group(0)
        self.assertRegex(
            inject_remote_input,
            re.compile(
                r"if \(!pressed && !this->remote_input_pressed_\) \{\s*"
                r"ESP_LOGD\(TAG, \"Ignoring duplicate CFX Sync remote release\"\);\s*"
                r"return false;\s*"
                r"\}.*?"
                r"this->remote_input_pressed_ = pressed;.*?"
                r"this->remote_input_->inject_remote_state\(pressed\);.*?"
                r"return true;",
                re.DOTALL,
            ),
        )

    def test_static_peer_fallback_is_disabled_without_configured_peer(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool has_static_peer_{false};", header)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::set_peer\(.*?"
                r"this->has_static_peer_ = true;.*?"
                r"this->find_or_add_peer_",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_sync_request_\(\) \{"
                r"\s*if \(!this->has_static_peer_\) \{"
                r"\s*return false;"
                r"\s*\}"
                r"\s*return this->send_sync_request_to_\(this->peer_\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_packet_"
                r"\(std::vector<uint8_t> &packet\) \{"
                r"\s*if \(!this->has_static_peer_\) \{"
                r"\s*return false;"
                r"\s*\}"
                r"\s*return this->send_packet_to_\(this->peer_, packet\);",
                re.DOTALL,
            ),
        )

    def test_setup_defers_initial_hello_until_boot_discovery_window(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("void schedule_boot_discovery_();", header)
        self.assertIn("void run_boot_discovery_();", header)
        self.assertIn("bool boot_radio_ready_() const;", header)
        self.assertIn("void schedule_follower_hello_();", header)
        self.assertRegex(
            source,
            re.compile(
                r"this->schedule_boot_discovery_\(\);"
                r"\s*if \(this->role_ != CFXSyncRole::LEADER\) \{"
                r"\s*this->schedule_follower_hello_\(\);"
                r"\s*\}",
                re.DOTALL,
            ),
        )
        setup_section = re.search(
            r"void CFXSyncComponent::setup\(\).*?^\}",
            source,
            re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(setup_section)
        self.assertNotIn("this->send_hello_();", setup_section.group(0))
        self.assertNotIn('set_interval("hello"', source)

    def test_boot_discovery_waits_for_wifi_channel_ready_before_first_hello(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("#include \"esphome/components/wifi/wifi_component.h\"", source)
        self.assertIn("BOOT_DISCOVERY_DELAY_MS", header)
        self.assertIn("BOOT_DISCOVERY_JITTER_SPREAD_MS", header)
        self.assertIn("BOOT_DISCOVERY_RETRY_MS", header)
        self.assertIn("BOOT_DISCOVERY_MAX_WAIT_MS", header)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::boot_radio_ready_"
                r"\(\) const \{.*?"
                r"#ifdef USE_WIFI.*?"
                r"wifi::global_wifi_component != nullptr.*?"
                r"return wifi::global_wifi_component->is_connected\(\);.*?"
                r"#endif.*?"
                r"return true;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_boot_discovery_\(\) \{.*?"
                r"BOOT_DISCOVERY_DELAY_MS \+"
                r"\s*\(esp_random\(\) % \(BOOT_DISCOVERY_JITTER_SPREAD_MS \+ 1\)\).*?"
                r"this->set_timeout\(\s*\"boot-discovery\".*?"
                r"this->run_boot_discovery_\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::run_boot_discovery_\(\) \{.*?"
                r"!this->boot_radio_ready_\(\).*?"
                r"millis\(\) - this->boot_discovery_started_ms_ <"
                r"\s*BOOT_DISCOVERY_MAX_WAIT_MS.*?"
                r"this->set_timeout\(\s*\"boot-discovery-retry\""
                r".*?BOOT_DISCOVERY_RETRY_MS.*?"
                r"this->run_boot_discovery_\(\);.*?"
                r"this->send_hello_\(\);",
                re.DOTALL,
            ),
        )

    def test_follower_hello_schedule_is_jittered(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("HELLO_JITTER_SPREAD_MS", header)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_follower_hello_\(\) \{"
                r"\s*if \(this->role_ == CFXSyncRole::LEADER\) \{"
                r"\s*return;"
                r"\s*\}.*?"
                r"HELLO_INTERVAL_MS \+"
                r"\s*\(esp_random\(\) % \(HELLO_JITTER_SPREAD_MS \+ 1\)\).*?"
                r"this->set_timeout\(\s*\"hello\".*?"
                r"this->send_hello_\(\);.*?"
                r"this->schedule_follower_hello_\(\);",
                re.DOTALL,
            ),
        )

    def test_startup_recovery_requests_are_jittered_and_not_back_to_back(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("RECOVERY_JITTER_SPREAD_MS", header)
        self.assertRegex(
            header,
            re.compile(
                r"void schedule_follower_recovery_attempt_"
                r"\(const char \*name,\s*uint32_t base_delay_ms\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_follower_recovery_"
                r"\(\).*?"
                r"this->schedule_follower_recovery_attempt_"
                r"\(\"sync-request-1\",\s*FOLLOWER_RECOVERY_FIRST_MS\);.*?"
                r"this->schedule_follower_recovery_attempt_"
                r"\(\"sync-request-2\",\s*FOLLOWER_RECOVERY_SECOND_MS\);.*?"
                r"this->schedule_follower_recovery_attempt_"
                r"\(\"sync-request-3\",\s*FOLLOWER_RECOVERY_THIRD_MS\);.*?"
                r"this->set_timeout\("
                r"\"sync-recovery-expired\",\s*FOLLOWER_RECOVERY_EXPIRE_MS",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_follower_recovery_attempt_"
                r"\(\s*const char \*name,\s*uint32_t base_delay_ms\).*?"
                r"base_delay_ms \+"
                r"\s*\(esp_random\(\) % \(RECOVERY_JITTER_SPREAD_MS \+ 1\)\).*?"
                r"this->set_timeout\(name, delay_ms.*?"
                r"!this->has_valid_state_ && this->boot_radio_ready_\(\).*?"
                r"this->send_sync_request_to_\(BROADCAST_MAC\);",
                re.DOTALL,
            ),
        )
        recovery_section = re.search(
            r"void CFXSyncComponent::schedule_follower_recovery_attempt_"
            r"\(.*?\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(recovery_section)
        self.assertNotIn("send_hello_();", recovery_section.group(0))
        self.assertNotIn("send_sync_request_();", recovery_section.group(0))

    def test_follower_keeps_requesting_state_after_recovery_expiry(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("FOLLOWER_RECOVERY_REPEAT_MS", header)
        self.assertIn("FOLLOWER_RECOVERY_REPEAT_JITTER_SPREAD_MS", header)
        self.assertIn("void schedule_follower_recovery_loop_();", header)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_follower_recovery_"
                r"\(\).*?"
                r"this->status_set_warning\(\);"
                r"\s*this->schedule_follower_recovery_loop_\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_follower_recovery_loop_"
                r"\(\) \{.*?"
                r"!this->is_state_receiver_role_\(\) \|\|"
                r"\s*!this->sync_enabled_ \|\|"
                r"\s*this->has_valid_state_.*?"
                r"FOLLOWER_RECOVERY_REPEAT_MS \+"
                r"\s*\(esp_random\(\) %"
                r"\s*\(FOLLOWER_RECOVERY_REPEAT_JITTER_SPREAD_MS \+ 1\)\).*?"
                r"this->set_timeout\("
                r"\"sync-recovery-loop\", delay_ms.*?"
                r"!this->is_state_receiver_role_\(\) \|\|"
                r"\s*!this->sync_enabled_ \|\|"
                r"\s*this->has_valid_state_.*?"
                r"if \(this->boot_radio_ready_\(\)\) \{"
                r"\s*this->send_sync_request_to_\(BROADCAST_MAC\);"
                r"\s*\}.*?"
                r"this->schedule_follower_recovery_loop_\(\);",
                re.DOTALL,
            ),
        )
        recovery_loop = re.search(
            r"void CFXSyncComponent::schedule_follower_recovery_loop_"
            r"\(.*?\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(recovery_loop)
        self.assertNotIn("send_hello_();", recovery_loop.group(0))

    def test_radio_rearm_follows_wifi_channel_changes_and_stuck_recovery(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("void loop() override;", header)
        self.assertIn("ESPNOW_REARM_DELAY_MS", header)
        self.assertIn("ESPNOW_REARM_MIN_INTERVAL_MS", header)
        self.assertIn("FOLLOWER_RECOVERY_REARM_MS", header)
        self.assertIn("uint8_t current_wifi_channel_() const;", header)
        self.assertIn("void schedule_espnow_rearm_(const char *reason);", header)
        self.assertIn("uint8_t last_wifi_channel_{0};", header)
        self.assertIn("uint8_t pending_wifi_channel_{0};", header)
        self.assertIn("uint32_t pending_wifi_channel_since_ms_{0};", header)
        self.assertIn("WIFI_CHANNEL_STABLE_MS", header)
        self.assertIn("bool last_wifi_connected_{false};", header)
        self.assertIn("bool espnow_rearm_scheduled_{false};", header)
        self.assertIn("uint8_t fallback_channel_{6};", header)
        self.assertIn("bool offline_fallback_active_{false};", header)
        self.assertIn("uint32_t wifi_disconnected_since_ms_{0};", header)
        self.assertIn("WIFI_OFFLINE_GRACE_MS", header)
        self.assertIn("void enter_offline_fallback_();", header)
        self.assertIn("void exit_offline_fallback_(uint8_t channel);", header)
        self.assertIn("bool apply_fallback_channel_();", header)
        self.assertIn("uint8_t active_sync_channel_() const;", header)
        self.assertIn("const char *sync_mode_name_() const;", header)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::loop\(\) \{.*?"
                r"const uint8_t channel = this->current_wifi_channel_\(\);.*?"
                r"const bool connected = channel != 0;.*?"
                r"this->exit_offline_fallback_\(channel\).*?"
                r"channel != this->last_wifi_channel_.*?"
                r"this->pending_wifi_channel_ != channel.*?"
                r"this->pending_wifi_channel_since_ms_ = now;.*?"
                r"now - this->pending_wifi_channel_since_ms_ <"
                r"\s*WIFI_CHANNEL_STABLE_MS.*?"
                r"this->last_wifi_connected_ = connected;.*?"
                r"this->last_wifi_channel_ = channel;.*?"
                r"this->schedule_espnow_rearm_\(\"wifi-channel\"\);.*?"
                r"this->enter_offline_fallback_\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_espnow_rearm_"
                r"\(const char \*reason\) \{.*?"
                r"this->espnow_rearm_scheduled_.*?"
                r"ESPNOW_REARM_MIN_INTERVAL_MS.*?"
                r"this->set_timeout\(\"espnow-rearm\", delay_ms.*?"
                r"this->send_pending_.*?"
                r"this->bus_->disable_espnow\(\);.*?"
                r"this->apply_fallback_channel_\(\);.*?"
                r"this->bus_->enable_espnow\(\);.*?"
                r"this->schedule_boot_discovery_\(\);.*?"
                r"this->schedule_follower_recovery_loop_\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_follower_recovery_loop_"
                r"\(\) \{.*?"
                r"FOLLOWER_RECOVERY_REARM_MS.*?"
                r"this->schedule_espnow_rearm_\(\"follower-recovery\"\);",
                re.DOTALL,
            ),
        )

    def test_offline_fallback_is_visible_and_does_not_override_wifi_policy(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include <esp_wifi.h>', source)
        self.assertIn("void set_fallback_channel(uint8_t channel)", header)
        self.assertIn("Sync mode: %s", source)
        self.assertIn("Fallback channel: %u", source)
        self.assertIn("Active sync channel: %u", source)
        self.assertIn('return this->offline_fallback_active_ ? "offline fallback" : "infrastructure";', source)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::enter_offline_fallback_\(\).*?"
                r"CFX Sync entering offline fallback on channel %u.*?"
                r"ESPHome Wi-Fi reboot policy still applies.*?"
                r"this->apply_fallback_channel_\(\);.*?"
                r"this->schedule_espnow_rearm_\(\"offline-fallback\"\);.*?"
                r"this->send_hello_\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::exit_offline_fallback_"
                r"\(uint8_t channel\).*?"
                r"CFX Sync exiting offline fallback; infrastructure channel %u restored.*?"
                r"this->schedule_espnow_rearm_\(\"wifi-restored\"\);.*?"
                r"this->send_hello_\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::apply_fallback_channel_\(\).*?"
                r"if \(!this->offline_fallback_active_ \|\|"
                r"\s*this->fallback_channel_ == 0\).*?"
                r"esp_wifi_set_promiscuous\(true\).*?"
                r"esp_wifi_set_channel\(this->fallback_channel_,"
                r"\s*WIFI_SECOND_CHAN_NONE\).*?"
                r"esp_wifi_set_promiscuous\(false\)",
                re.DOTALL,
            ),
        )
        self.assertNotIn("reboot_timeout", source)

    def test_leader_discovery_failure_logs_bssid_and_channel(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "void format_current_wifi_bssid_(char *buffer, size_t size) const;",
            header,
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::format_current_wifi_bssid_"
                r"\(\s*char \*buffer,\s*size_t size\) const \{.*?"
                r"wifi::global_wifi_component->wifi_bssid\(\).*?"
                r"format_mac_addr_upper\(bssid\.data\(\), buffer\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_follower_recovery_"
                r"\(\) \{.*?"
                r"char bssid_buf\[MAC_ADDRESS_PRETTY_BUFFER_SIZE\];.*?"
                r"this->format_current_wifi_bssid_"
                r"\(\s*bssid_buf,\s*sizeof\(bssid_buf\)\s*\);.*?"
                r"Discovered CFX Sync leader failed on BSSID %s.*?"
                r"channel %u",
                re.DOTALL,
            ),
        )

    def test_hello_only_resyncs_new_or_rebooted_followers(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "bool should_send_state_for_hello_(const PeerState &peer,\n"
            "                                    bool new_peer,\n"
            "                                    bool peer_rebooted) const;",
            header,
        )
        self.assertRegex(
            source,
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::HELLO\) \{"
                r".*?"
                r"\s*const bool new_peer = peer == nullptr;"
                r"\s*const bool peer_rebooted ="
                r"\s*peer != nullptr && peer->has_rx_sequence &&"
                r"\s*peer->rx_boot_id != packet\.boot_id;.*?"
                r"this->should_send_state_for_hello_"
                r"\(\*peer, new_peer,\s*peer_rebooted\).*?"
                r"this->send_state_\(\);",
                re.DOTALL,
            ),
        )
        hello_body = re.search(
            r"if \(packet\.type == CFXSyncPacketType::HELLO\) \{.*?"
            r"\n  \}\n\n  if \(packet\.type == CFXSyncPacketType::SYNC_REQUEST\)",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(hello_body)
        self.assertNotIn("this->check_ack_health_();", hello_body.group(0))
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::should_send_state_for_hello_"
                r"\(\s*const PeerState &peer, bool new_peer, "
                r"bool peer_rebooted\) const \{"
                r"\s*#if defined\(USE_ESP32\)"
                r"\s*if \(this->role_ != CFXSyncRole::LEADER \|\|"
                r"\s*!this->peer_accepts_leader_state_\(peer\)\) \{"
                r"\s*return false;"
                r"\s*\}"
                r"\s*return new_peer \|\| peer_rebooted \|\|"
                r"\s*peer\.last_state_sent_sequence == 0 \|\|"
                r"\s*this->has_pending_ack_\(peer\);",
                re.DOTALL,
            ),
        )

    def test_heartbeat_broadcasts_current_state_snapshot(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool send_heartbeat_state_();", header)
        self.assertRegex(
            source,
            re.compile(
                r"this->set_interval\(\"heartbeat\", this->heartbeat_ms_,"
                r"\s*\[this\]\(\) \{ this->send_heartbeat_state_\(\); \}\);",
                re.DOTALL,
            ),
        )
        self.assertNotIn('[this]() { this->send_state_(); });', source)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_heartbeat_state_\(\) \{"
                r"\s*return this->send_state_\(\);"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_wrong_group_packets_are_not_consumed(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"if \(result == CFXSyncDecodeResult::WRONG_GROUP\) \{"
                r"\s*this->handle_decode_failure_\(result\);"
                r"\s*return false;"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_packet_codec_exposes_full_v1_state(self):
        header = PACKET_HEADER.read_text(encoding="utf-8")
        source = PACKET_SOURCE.read_text(encoding="utf-8")

        self.assertIn("FIELD_BRIGHTNESS = 0x00000002UL", header)
        self.assertIn("FIELD_COLOR = 0x00000004UL", header)
        self.assertIn("FIELD_COLOR_BRIGHTNESS = 0x00000008UL", header)
        self.assertIn("FULL_STATE_PAYLOAD_SIZE = 12", header)
        self.assertIn("packet.has_brightness = true", source)
        self.assertIn("packet.has_color = true", source)
        self.assertIn("packet.has_color_brightness = true", source)

    def test_packet_codec_exposes_transition_and_ramp_timing(self):
        header = PACKET_HEADER.read_text(encoding="utf-8")
        source = PACKET_SOURCE.read_text(encoding="utf-8")

        self.assertIn("FIELD_TRANSITION = 0x00000040UL", header)
        self.assertIn("FIELD_RAMP = 0x00000080UL", header)
        self.assertIn("MAX_TIMING_VALUE_SIZE = 4", header)
        self.assertIn("MAX_STATE_PACKET_SIZE == 132", header)
        self.assertIn("bool has_transition{false};", header)
        self.assertIn("uint16_t transition_ms{0};", header)
        self.assertIn("bool has_ramp{false};", header)
        self.assertIn("uint16_t ramp_ms{0};", header)
        self.assertIn("struct CFXSyncTimingState", header)
        self.assertIn("const CFXSyncTimingState &timing", header)
        self.assertIn("field_mask |= FIELD_TRANSITION;", source)
        self.assertIn("field_mask |= FIELD_RAMP;", source)
        self.assertIn("append_u16_(payload, timing.transition_ms);", source)
        self.assertIn("append_u16_(payload, timing.ramp_ms);", source)
        self.assertIn("packet.has_transition = true;", source)
        self.assertIn("packet.transition_ms = read_u16_", source)
        self.assertIn("packet.has_ramp = true;", source)
        self.assertIn("packet.ramp_ms = read_u16_", source)
        self.assertRegex(
            source,
            re.compile(
                r"FIELD_EFFECT \| FIELD_CONTROLS \|"
                r"\s*FIELD_TRANSITION \| FIELD_RAMP"
            ),
        )

    def test_packet_codec_exposes_optional_effect_wire_state(self):
        header = PACKET_HEADER.read_text(encoding="utf-8")
        source = PACKET_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "cfx_sync_effect.h"', header)
        self.assertIn("FIELD_EFFECT = 0x00000010UL", header)
        self.assertIn("MAX_EFFECT_NAME_BYTES = 64", header)
        self.assertIn("MAX_EFFECT_VALUE_SIZE = 67", header)
        self.assertIn("MAX_STATE_PACKET_SIZE", header)
        self.assertIn("117", header)
        self.assertIn("bool has_effect{false};", header)
        self.assertIn("CFXSyncEffectState effect;", header)
        self.assertIn("bool has_effect,", header)
        self.assertIn(
            "const CFXSyncEffectState &effect,", header
        )
        self.assertIn("packet.has_effect = true", source)
        self.assertIn("CFXSyncEffectKind::NONE", source)
        self.assertIn("CFXSyncEffectKind::CHIMERAFX", source)
        self.assertIn("CFXSyncEffectKind::UNSUPPORTED", source)

    def test_packet_effect_codec_has_strict_utf8_without_renderer(self):
        header = PACKET_HEADER.read_text(encoding="utf-8")
        source = PACKET_SOURCE.read_text(encoding="utf-8")
        combined = (header + source).lower()

        self.assertIn("is_valid_utf8_", header)
        self.assertIn("is_valid_utf8_", source)
        self.assertNotIn("cfx_light", combined)
        self.assertNotIn("cfx_effect/", combined)
        self.assertNotIn("cfxrunner", combined)
        self.assertNotIn("renderer", combined)

    def test_packet_codec_uses_explicit_byte_order_without_packing(self):
        header = PACKET_HEADER.read_text(encoding="utf-8")
        source = PACKET_SOURCE.read_text(encoding="utf-8")
        combined = (header + source).lower()

        self.assertIn("append_u16_", source)
        self.assertIn("append_u32_", source)
        self.assertIn("read_u16_", source)
        self.assertIn("read_u32_", source)
        self.assertNotIn("#pragma pack", combined)
        self.assertNotIn("__attribute__((packed))", combined)

    def test_color_helper_is_renderer_independent(self):
        text = COLOR_HEADER.read_text(encoding="utf-8")

        self.assertIn("struct CFXSyncLightSnapshot", text)
        self.assertIn("convert_color_for_follower", text)
        self.assertIn(
            "if (snapshot.has_white == follower_has_white) {\n"
            "    return snapshot;\n  }",
            text,
        )
        self.assertIn("light::LightState &state", text)
        self.assertNotIn("const light::LightState &state", text)
        self.assertNotIn("cfx_light", text)
        self.assertNotIn("cfx_effect", text)
        self.assertNotIn("runner", text)

    def test_effect_helper_is_renderer_independent(self):
        text = self._effect_header_text()
        lowered = text.lower()

        self.assertIn(
            "enum class CFXSyncEffectKind : uint8_t", text
        )
        self.assertIn("NONE = 0", text)
        self.assertIn("CHIMERAFX = 1", text)
        self.assertIn("UNSUPPORTED = 2", text)
        self.assertIn("struct CFXSyncEffectState", text)
        self.assertIn(
            "CFXSyncEffectKind kind{CFXSyncEffectKind::NONE};", text
        )
        self.assertIn("uint8_t effect_id{0};", text)
        self.assertIn("std::string name;", text)
        self.assertRegex(
            text,
            re.compile(
                r"struct CFXSyncEffectEntry\s*\{.*?"
                r"uint8_t effect_id\{0\};.*?"
                r"std::string name;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            text,
            re.compile(
                r"bool operator==\(.*?\)\s+const\s*\{.*?"
                r"this->kind == other\.kind &&.*?"
                r"this->effect_id == other\.effect_id &&.*?"
                r"this->name == other\.name;",
                re.DOTALL,
            ),
        )
        self.assertIn("operator!=", text)
        for forbidden in ("cfx_effect", "cfxrunner", "runner", "renderer"):
            self.assertNotIn(forbidden, lowered)

    def test_effect_lookup_requires_exact_id_and_name(self):
        text = self._effect_header_text()

        self.assertIn("find_effect_entry(", text)
        self.assertIn(
            "entry.effect_id == effect_id && entry.name == name",
            text,
        )
        self.assertNotIn("strcasecmp", text)
        self.assertNotIn("casefold", text)

    def test_effect_capture_uses_public_light_state_api(self):
        text = self._effect_header_text()

        self.assertIn("capture_effect_state(", text)
        self.assertIn("light::LightState *state", text)
        self.assertIn("state->get_effect_name()", text)
        self.assertIn('effect_name == "None"', text)
        self.assertIn("if (entry.name == effect_name)", text)
        self.assertIn("entry.effect_id", text)
        self.assertIn("entry.name", text)
        self.assertIn("CFXSyncEffectKind::CHIMERAFX", text)
        self.assertIn("CFXSyncEffectKind::UNSUPPORTED", text)
        self.assertIn("effect_name,", text)
        self.assertNotIn("static_cast", text)
        self.assertNotIn("dynamic_cast", text)

    def test_runtime_tracks_complete_snapshots(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("CFXSyncLightSnapshot observed_state_", header)
        self.assertIn("capture_light_snapshot(*leader)", source)
        self.assertIn("snapshot.color_brightness", source)
        self.assertNotIn("observed_power_", header)

    def test_leader_setup_captures_effect_baseline_defensively(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("CFXSyncEffectState observed_effect_{};", header)
        self.assertRegex(
            source,
            re.compile(
                r"if \(this->role_ == CFXSyncRole::LEADER\) \{.*?"
                r"this->effect_catalogs_\.empty\(\).*?"
                r"capture_light_snapshot\(\*leader\).*?"
                r"this->observed_effect_ = capture_effect_state\("
                r"\s*this->lights_\[0\], this->effect_catalogs_\[0\]\);",
                re.DOTALL,
            ),
        )

    def test_effect_only_leader_change_is_actionable(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"const auto effect = capture_effect_state\("
                r"\s*leader, this->effect_catalogs_\[0\]\);.*?"
                r"const auto controls = this->capture_control_state_\(0\);.*?"
                r"snapshot == this->observed_state_ &&\s*"
                r"effect == this->observed_effect_ &&\s*"
                r"controls == this->observed_controls_.*?"
                r"this->observed_state_ = snapshot;.*?"
                r"this->observed_effect_ = effect;.*?"
                r"this->observed_controls_ = controls;.*?"
                r"this->send_state_\(snapshot, effect, controls, true\);",
                re.DOTALL,
            ),
        )

    def test_local_leader_events_emit_default_transition_timing(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool include_default_transition", header)
        self.assertIn(
            "bool state_send_deferred_with_transition_{false};",
            header,
        )
        self.assertRegex(
            source,
            re.compile(
                r"static CFXSyncTimingState capture_sync_timing_state"
                r"\(.*?const CFXSyncLightSnapshot &snapshot,\s*"
                r"const CFXSyncEffectState &effect,\s*"
                r"bool include_default_transition.*?"
                r"cfx_dimmer::capture_light_timing_hint.*?"
                r"if \(hint\.has_ramp\).*?return timing;.*?"
                r"const bool should_include_default_transition =.*?"
                r"effect\.kind == CFXSyncEffectKind::NONE \|\| !snapshot\.power"
                r".*?"
                r"if \(include_default_transition &&.*?"
                r"should_include_default_transition.*?"
                r"leader->get_default_transition_length\(\).*?"
                r"timing\.has_transition = true;.*?"
                r"timing\.transition_ms =",
                re.DOTALL,
            ),
        )
        self.assertIn(
            "this->send_state_(snapshot, effect, controls, true);",
            source,
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_heartbeat_state_\(\) \{"
                r"\s*return this->send_state_\(\);"
                r"\s*\}",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::flush_deferred_state_\(\).*?"
                r"const bool include_default_transition ="
                r"\s*this->state_send_deferred_with_transition_;.*?"
                r"this->state_send_deferred_with_transition_ = false;.*?"
                r"this->send_state_\(include_default_transition\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"send_state_to_peer_\(.*?"
                r"const auto timing = capture_sync_timing_state"
                r"\(leader, snapshot, effect, false\);",
                re.DOTALL,
            ),
        )

    def test_all_leader_state_send_paths_include_effect(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "const CFXSyncEffectState &effect", header
        )
        self.assertIn(
            "const CFXSyncControlState &controls", header
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_\(\s*"
                r"bool include_default_transition\) \{.*?"
                r"const auto snapshot = capture_light_snapshot\(\*leader\);.*?"
                r"const auto effect = capture_effect_state\(leader, this->effect_catalogs_\[0\]\);.*?"
                r"const auto controls = this->capture_control_state_\(0\);.*?"
                r"return this->send_state_to_followers_"
                r"\(snapshot, effect, controls,\s*"
                r"include_default_transition\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_\(\s*"
                r"const CFXSyncLightSnapshot &snapshot,\s*"
                r"const CFXSyncEffectState &effect,\s*"
                r"const CFXSyncControlState &controls,\s*"
                r"bool include_default_transition\).*?"
                r"this->send_state_to_followers_"
                r"\(snapshot, effect, controls,\s*"
                r"include_default_transition\);.*?"
                r"bool CFXSyncComponent::send_state_to_followers_\(.*?"
                r"snapshot\.has_white,\s*true,\s*effect,\s*"
                r"controls\.has_any\(\),\s*controls,\s*timing,\s*"
                r"this->key_,\s*packet",
                re.DOTALL,
            ),
        )
        self.assertIn(
            "if (packet.type == CFXSyncPacketType::SYNC_REQUEST)", source
        )
        self.assertIn("this->send_state_();", source)
        self.assertIn(
            "this->send_state_(snapshot, effect, controls, true);",
            source,
        )

    def test_leader_state_send_paths_broadcast_to_group(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool send_state_to_followers_();", header)
        self.assertIn("bool peer_accepts_leader_state_(const PeerState &peer) const;", header)
        self.assertIn("void mark_state_sent_to_followers_(uint32_t sequence);", header)
        self.assertNotIn("uint8_t fanout_cursor_{0};", header)
        self.assertNotIn("uint8_t fanout_remaining_{0};", header)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_\(\s*"
                r"bool include_default_transition\) \{.*?"
                r"const auto snapshot = capture_light_snapshot\(\*leader\);.*?"
                r"const auto effect = capture_effect_state\(leader, this->effect_catalogs_\[0\]\);.*?"
                r"const auto controls = this->capture_control_state_\(0\);.*?"
                r"return this->send_state_to_followers_"
                r"\(snapshot, effect, controls,\s*"
                r"include_default_transition\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_\(\s*"
                r"const CFXSyncLightSnapshot &snapshot,\s*"
                r"const CFXSyncEffectState &effect,\s*"
                r"const CFXSyncControlState &controls,\s*"
                r"bool include_default_transition\).*?"
                r"return this->send_state_to_followers_"
                r"\(snapshot, effect, controls,\s*"
                r"include_default_transition\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_to_followers_\(\s*"
                r"const CFXSyncLightSnapshot &snapshot,\s*"
                r"const CFXSyncEffectState &effect,\s*"
                r"const CFXSyncControlState &controls,\s*"
                r"bool include_default_transition\).*?"
                r"const uint32_t sequence = this->next_sequence_\(\);.*?"
                r"CFXSyncPacketCodec::encode_state\(.*?"
                r"this->send_state_packet_to_followers_\(packet\).*?"
                r"this->mark_state_sent_to_followers_\(sequence\);",
                re.DOTALL,
            ),
        )
        self.assertNotIn("this->send_state_to_peer_(*peer);", source)

    def test_hello_and_sync_request_use_broadcast_state_response(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::HELLO\).*?"
                r"this->should_send_state_for_hello_"
                r"\(\*peer, new_peer,\s*peer_rebooted\).*?"
                r"this->send_state_\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::SYNC_REQUEST\).*?"
                r"this->peer_accepts_leader_state_\(\*peer\).*?"
                r"this->send_state_\(\);",
                re.DOTALL,
            ),
        )
        self.assertNotIn("this->send_state_to_peer_(*peer);", source)

    def test_leader_state_target_selection_requires_registered_light_followers(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::peer_accepts_leader_state_"
                r"\(\s*const PeerState &peer\) const \{.*?"
                r"peer\.active.*?"
                r"peer\.registered.*?"
                r"peer\.node_role == CFXSyncNodeRole::FOLLOWER.*?"
                r"peer\.node_role == CFXSyncNodeRole::SATELLITE.*?"
                r"peer\.capabilities & CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER",
                re.DOTALL,
            ),
        )
        self.assertIn("void mark_state_sent_to_followers_(uint32_t sequence);", header)
        self.assertIn("peer.last_state_sent_boot_id = this->boot_id_;", source)
        self.assertIn("peer.last_state_sent_sequence = sequence;", source)
        self.assertIn("CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER", source)

    def test_broadcast_state_send_health_is_tracked_per_known_follower(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool send_pending_{false};", header)
        self.assertIn("uint32_t last_broadcast_state_sequence_{0};", header)
        self.assertIn("uint32_t last_broadcast_state_ms_{0};", header)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::mark_state_sent_to_followers_"
                r"\(uint32_t sequence\).*?"
                r"this->last_broadcast_state_boot_id_ = this->boot_id_;.*?"
                r"this->last_broadcast_state_sequence_ = sequence;.*?"
                r"this->last_broadcast_state_ms_ = now;.*?"
                r"for \(auto &peer : this->peers_\).*?"
                r"this->peer_accepts_leader_state_\(peer\).*?"
                r"peer\.last_state_sent_boot_id = this->boot_id_;.*?"
                r"peer\.last_state_sent_sequence = sequence;.*?"
                r"peer\.last_state_sent_ms = now;",
                re.DOTALL,
            ),
        )

    def test_authenticated_broadcast_state_discovers_unknown_leader(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"if \(peer == nullptr &&"
                r"\s*packet\.type == CFXSyncPacketType::STATE &&"
                r"\s*this->is_state_receiver_role_\(\)\).*?"
                r"this->find_or_add_peer_\(source,"
                r"\s*CFXSyncNodeRole::LEADER,"
                r"\s*CFXSyncPacketCodec::CAP_LIGHT_LEADER\);",
                re.DOTALL,
            ),
        )

    def test_authenticated_state_ack_discovers_unknown_follower(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"if \(peer == nullptr &&"
                r"\s*packet\.type == CFXSyncPacketType::STATE_ACK &&"
                r"\s*this->role_ == CFXSyncRole::LEADER &&"
                r"\s*this->is_current_broadcast_ack_\(packet\)\).*?"
                r"this->find_or_add_peer_\(source,"
                r"\s*CFXSyncNodeRole::FOLLOWER,"
                r"\s*CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER\);.*?"
                r"this->seed_peer_sent_state_from_ack_\(.*?packet\);",
                re.DOTALL,
            ),
        )

    def test_direct_send_is_backpressured_until_callback(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool send_pending_{false};", header)
        self.assertIn("bool state_send_deferred_{false};", header)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_espnow_packet_to_"
                r"\(\s*const std::array<uint8_t, 6> &mac,\s*"
                r"std::vector<uint8_t> &packet\).*?"
                r"if \(this->send_pending_\) \{.*?"
                r"return false;.*?"
                r"this->send_pending_ = true;.*?"
                r"\[this\]\(esp_err_t send_result\).*?"
                r"this->send_pending_ = false;.*?"
                r"this->handle_send_result_\(send_result\).*?"
                r"if \(send_result == ESP_OK\) \{.*?"
                r"this->flush_deferred_state_\(\);.*?"
                r"if \(result != ESP_OK\) \{.*?"
                r"this->send_pending_ = false;.*?"
                r"this->handle_send_result_\(result\);",
                re.DOTALL,
            ),
        )

    def test_leader_broadcast_state_is_deferred_under_send_backpressure(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("void flush_deferred_state_();", header)
        self.assertIn("bool state_send_deferred_{false};", header)
        self.assertNotIn("uint8_t fanout_remaining_{0};", header)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_to_followers_"
                r"\(.*?if \(this->send_pending_\) \{"
                r"\s*this->state_send_deferred_ = true;"
                r"\s*this->state_send_deferred_with_transition_ ="
                r"\s*this->state_send_deferred_with_transition_ \|\|"
                r"\s*include_default_transition;"
                r"\s*return false;"
                r"\s*\}",
                re.DOTALL,
            ),
        )
        broadcast_body = re.search(
            r"bool CFXSyncComponent::send_state_to_followers_"
            r"\(\s*const CFXSyncLightSnapshot &snapshot,.*?"
            r"\n\}\n\nvoid CFXSyncComponent::mark_state_sent_to_followers_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(broadcast_body)
        pre_send_pending = broadcast_body.group(0).split(
            "if (this->send_pending_) {", 1
        )[0]
        self.assertNotIn("this->check_ack_health_();", pre_send_pending)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::flush_deferred_state_\(\) \{"
                r"\s*if \(!this->state_send_deferred_ \|\| "
                r"this->send_pending_ \|\|"
                r"\s*this->role_ != CFXSyncRole::LEADER\) \{"
                r"\s*return;"
                r"\s*\}"
                r"\s*const bool include_default_transition ="
                r"\s*this->state_send_deferred_with_transition_;"
                r"\s*this->state_send_deferred_ = false;"
                r"\s*this->state_send_deferred_with_transition_ = false;"
                r"\s*this->send_state_\(include_default_transition\);",
                re.DOTALL,
            ),
        )

    def test_broadcast_state_return_value_reflects_successfully_queued_send(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_to_followers_\(.*?"
                r"if \(!this->send_state_packet_to_followers_\(packet\)\) \{"
                r"\s*return false;"
                r"\s*\}"
                r".*?"
                r"\s*this->mark_state_sent_to_followers_\(sequence\);"
                r"\s*this->schedule_state_retry_\(\);"
                r"\s*return true;",
                re.DOTALL,
            ),
        )

    def test_follower_state_ack_is_scheduled_after_remote_apply(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "void schedule_state_ack_(const uint8_t *destination,\n"
            "                           const CFXSyncPacket &packet,\n"
            "                           CFXSyncAckResult result);",
            header,
        )
        self.assertIn("ACK_JITTER_MIN_MS", header)
        self.assertIn("ACK_JITTER_SPREAD_MS", header)
        self.assertRegex(
            source,
            re.compile(
                r"packet\.type == CFXSyncPacketType::STATE &&.*?"
                r"const bool applied = this->apply_remote_state_\(packet\);"
                r".*?"
                r"\s*this->schedule_state_ack_"
                r"\(source\.espnow_mac_or_null\(\), packet,\s*"
                r"CFXSyncAckResult::APPLIED\);",
                re.DOTALL,
            ),
        )

    def test_state_ack_is_broadcast_after_jitter(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_state_ack_"
                r"\(\s*const uint8_t \*destination,\s*"
                r"const CFXSyncPacket &packet,\s*"
                r"CFXSyncAckResult result\).*?"
                r"\(void\) destination;.*?"
                r"const uint32_t delay_ms\s*=\s*ACK_JITTER_MIN_MS \+"
                r"\s*\(esp_random\(\) % \(ACK_JITTER_SPREAD_MS \+ 1\)\);.*?"
                r"this->set_timeout\(\s*\"state-ack\",\s*delay_ms,\s*"
                r"\[this, acked_boot_id, acked_sequence, result\]\(\) \{.*?"
                r"CFXSyncPacketCodec::encode_state_ack\("
                r"\s*this->group_hash_,\s*this->boot_id_,\s*"
                r"this->next_sequence_\(\),\s*"
                r"acked_boot_id,\s*acked_sequence,\s*result,\s*"
                r"this->key_,\s*ack\).*?"
                r"this->send_packet_to_\(BROADCAST_MAC, ack\);",
                re.DOTALL,
            ),
        )
        ack_body = re.search(
            r"void CFXSyncComponent::schedule_state_ack_.*?"
            r"\n\}\n\nbool CFXSyncComponent::send_sync_request_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(ack_body)
        self.assertNotIn("this->send_packet_to_(mac, ack);", ack_body.group(0))

    def test_broadcast_state_is_retried_until_current_state_is_acked(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("static constexpr uint32_t STATE_RETRY_DELAY_MS", header)
        self.assertIn("static constexpr uint8_t STATE_RETRY_MAX_ATTEMPTS", header)
        self.assertIn("void schedule_state_retry_();", header)
        self.assertIn("uint8_t state_retry_attempts_{0};", header)
        self.assertIn("bool state_retry_active_{false};", header)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_to_followers_\(.*?"
                r"this->mark_state_sent_to_followers_\(sequence\);"
                r"\s*this->schedule_state_retry_\(\);"
                r"\s*return true;",
                re.DOTALL,
            ),
        )
        retry_body = re.search(
            r"void CFXSyncComponent::schedule_state_retry_\(\).*?"
            r"\n\}\n#endif\n\nvoid CFXSyncComponent::handle_decode_failure_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(retry_body)
        retry_text = retry_body.group(0)
        self.assertIn("this->role_ != CFXSyncRole::LEADER", retry_text)
        self.assertIn("this->state_retry_scheduled_", retry_text)
        self.assertIn("this->pending_ack_count_() == 0", retry_text)
        self.assertIn(
            'this->set_timeout("state-retry", STATE_RETRY_DELAY_MS, [this]() {',
            retry_text,
        )
        self.assertIn(
            "this->state_retry_attempts_ >= STATE_RETRY_MAX_ATTEMPTS",
            retry_text,
        )
        self.assertIn("this->send_pending_", retry_text)
        self.assertIn("this->state_retry_attempts_++;", retry_text)
        self.assertIn("this->state_retry_active_ = true;", retry_text)
        self.assertIn("this->send_state_();", retry_text)
        self.assertIn("this->state_retry_active_ = false;", retry_text)
        self.assertRegex(
            retry_text,
            re.compile(
                r"if \(this->state_retry_attempts_ >= "
                r"STATE_RETRY_MAX_ATTEMPTS\) \{"
                r"\s*this->check_ack_health_\(\);"
                r"\s*return;"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_ack_warning_waits_for_retry_exhaustion(self):
        source = SOURCE.read_text(encoding="utf-8")
        decoded_body = re.search(
            r"bool CFXSyncComponent::handle_decoded_packet_\(.*?"
            r"\n\}\n\nvoid CFXSyncComponent::on_local_light_update",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(decoded_body)
        self.assertNotIn("this->check_ack_health_();", decoded_body.group(0))

        broadcast_body = re.search(
            r"bool CFXSyncComponent::send_state_to_followers_"
            r"\(\s*const CFXSyncLightSnapshot &snapshot,.*?"
            r"\n\}\n\nvoid CFXSyncComponent::mark_state_sent_to_followers_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(broadcast_body)
        self.assertNotIn("this->check_ack_health_();", broadcast_body.group(0))

    def test_hello_discovery_ignores_incompatible_roles(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        decoded_body = re.search(
            r"bool CFXSyncComponent::handle_decoded_packet_\(.*?"
            r"\n\}\n\nvoid CFXSyncComponent::on_local_light_update",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(decoded_body)

        self.assertIn("bool accepts_peer_role_(CFXSyncNodeRole role) const;", header)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::accepts_peer_role_"
                r"\(\s*CFXSyncNodeRole role\) const \{.*?"
                r"if \(this->role_ == CFXSyncRole::LEADER\).*?"
                r"role == CFXSyncNodeRole::FOLLOWER.*?"
                r"role == CFXSyncNodeRole::SATELLITE.*?"
                r"return role == CFXSyncNodeRole::LEADER;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            decoded_body.group(0),
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::SYNC_REQUEST\) \{"
                r"\s*#if defined\(USE_ESP32\)"
                r"\s*if \(this->role_ != CFXSyncRole::LEADER\) \{"
                r"\s*this->log_rejection_\("
                r"\"Ignoring SYNC_REQUEST for incompatible role\"\);"
                r"\s*return true;"
                r"\s*\}",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            decoded_body.group(0),
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::HELLO\) \{"
                r".*?if \(!this->accepts_peer_role_\(packet\.node_role\)\) \{"
                r"\s*this->log_rejection_\("
                r"\"Ignoring HELLO from incompatible role\"\);"
                r"\s*return true;"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_remote_input_hold_is_owned_by_one_controller(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        decoded_body = re.search(
            r"bool CFXSyncComponent::handle_decoded_packet_\(.*?"
            r"\n\}\n\nvoid CFXSyncComponent::on_local_light_update",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(decoded_body)

        self.assertRegex(
            header,
            re.compile(
                r"bool handle_remote_input_"
                r"\(PeerState &peer, bool pressed, bool maintained,\s*"
                r"bool toggle\);",
                re.DOTALL,
            ),
        )
        self.assertIn("PeerState *remote_input_owner_{nullptr};", header)
        self.assertIn("void clear_remote_input_owner_();", header)
        self.assertRegex(
            decoded_body.group(0),
            re.compile(
                r"this->handle_remote_input_\(\s*"
                r"\*peer, packet\.input_pressed, packet\.input_maintained,\s*"
                r"packet\.input_toggle\)",
                re.DOTALL,
            ),
        )
        handler_body = re.search(
            r"bool CFXSyncComponent::handle_remote_input_"
            r"\(PeerState &peer, bool pressed,\s*"
            r"bool maintained, bool toggle\).*?"
            r"\n\}\n\nvoid CFXSyncComponent::clear_remote_input_owner_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(handler_body)
        handler_source = handler_body.group(0)
        self.assertIn("if (toggle || maintained)", handler_source)
        self.assertIn(
            "return this->inject_remote_input_(pressed, maintained, toggle);",
            handler_source,
        )
        self.assertIn("if (pressed)", handler_source)
        self.assertIn("this->remote_input_owner_ = &peer;", handler_source)
        self.assertIn("this->remote_input_owner_ != &peer", handler_source)
        self.assertIn(
            "Ignoring remote input while another controller is active",
            handler_source,
        )
        self.assertIn("const bool applied = this->inject_remote_input_", handler_source)
        self.assertIn("this->clear_remote_input_owner_();", handler_source)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::clear_remote_input_owner_\(\)"
                r".*?this->remote_input_owner_ = nullptr;",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_remote_input_timeout_\(\)"
                r".*?this->clear_remote_input_owner_\(\);",
                re.DOTALL,
            ),
        )

    def test_follower_silently_consumes_other_follower_hello(self):
        source = SOURCE.read_text(encoding="utf-8")
        decoded_body = re.search(
            r"bool CFXSyncComponent::handle_decoded_packet_\(.*?"
            r"\n\}\n\nvoid CFXSyncComponent::on_local_light_update",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(decoded_body)
        hello_index = decoded_body.group(0).find(
            "if (packet.type == CFXSyncPacketType::HELLO) {"
        )
        self.assertGreaterEqual(hello_index, 0)
        same_follower_index = decoded_body.group(0).find(
            "this->is_state_receiver_role_() &&\n"
            "        (packet.node_role == CFXSyncNodeRole::FOLLOWER ||\n"
            "         packet.node_role == CFXSyncNodeRole::SATELLITE)",
            hello_index,
        )
        rejection_index = decoded_body.group(0).find(
            'this->log_rejection_("Ignoring HELLO from incompatible role");',
            hello_index,
        )
        self.assertGreaterEqual(same_follower_index, 0)
        self.assertGreaterEqual(rejection_index, 0)
        self.assertLess(same_follower_index, rejection_index)

    def test_peer_registration_failure_is_logged(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::register_peer_"
                r"\(\s*PeerState &peer\).*?"
                r"if \(!this->bus_->add_espnow_peer"
                r"\(peer\.mac\.data\(\)\)\) \{"
                r".*?format_mac_addr_upper\(peer\.mac\.data\(\), peer_buf\);"
                r".*?ESP_LOGW\(TAG,"
                r"\s*\"CFX Sync failed to register peer %s\""
                r".*?return false;"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_state_ack_path_is_leader_only(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "void handle_state_ack_(PeerState &peer, const CFXSyncPacket &packet);",
            header,
        )
        self.assertRegex(
            source,
            re.compile(
                r"if \(packet\.type == CFXSyncPacketType::STATE_ACK\) \{"
                r"\s*#if defined\(USE_ESP32\)"
                r"\s*if \(this->role_ == CFXSyncRole::LEADER\) \{"
                r"\s*this->handle_state_ack_\(\*peer, packet\);"
                r"\s*\}"
                r"\s*#endif"
                r"\s*return true;"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_follower_silently_consumes_other_follower_state_ack(self):
        source = SOURCE.read_text(encoding="utf-8")
        decoded_body = re.search(
            r"bool CFXSyncComponent::handle_decoded_packet_\(.*?"
            r"\n\}\n\nvoid CFXSyncComponent::on_local_light_update",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(decoded_body)
        unknown_index = decoded_body.group(0).find(
            'this->log_rejection_("Ignoring authenticated packet from unknown peer");'
        )
        self.assertGreaterEqual(unknown_index, 0)
        ignore_index = decoded_body.group(0).find(
            "packet.type == CFXSyncPacketType::STATE_ACK &&\n"
            "      this->role_ != CFXSyncRole::LEADER"
        )
        self.assertGreaterEqual(ignore_index, 0)
        self.assertLess(ignore_index, unknown_index)

    def test_handle_state_ack_matches_last_sent_before_clearing(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool has_pending_ack_(const PeerState &peer) const;", header)
        self.assertIn("void clear_warning_if_set_();", header)
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::handle_state_ack_"
                r"\(\s*PeerState &peer,\s*const CFXSyncPacket &packet\).*?"
                r"packet\.acked_boot_id != peer\.last_state_sent_boot_id \|\|"
                r"\s*packet\.acked_sequence != peer\.last_state_sent_sequence.*?"
                r"return;.*?"
                r"peer\.last_ack_boot_id = packet\.acked_boot_id;.*?"
                r"peer\.last_ack_sequence = packet\.acked_sequence;.*?"
                r"peer\.last_ack_ms = millis\(\);.*?"
                r"peer\.missed_acks = 0;.*?"
                r"!this->has_peer_send_warning_\(\).*?"
                r"!this->has_pending_ack_\(.*?"
                r"this->clear_warning_if_set_\(\);",
                re.DOTALL,
            ),
        )

    def test_warning_clear_is_guarded_to_avoid_startup_noise(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::clear_warning_if_set_\(\) \{"
                r"\s*if \(this->status_has_warning\(\)\) \{"
                r"\s*this->status_clear_warning\(\);"
                r"\s*\}"
                r"\s*\}",
                re.DOTALL,
            ),
        )
        self.assertEqual(source.count("status_clear_warning()"), 1)

    def test_mismatched_state_ack_is_logged_without_clearing_pending_ack(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::handle_state_ack_"
                r"\(\s*PeerState &peer,\s*const CFXSyncPacket &packet\).*?"
                r"if \(packet\.acked_boot_id != peer\.last_state_sent_boot_id \|\|"
                r"\s*packet\.acked_sequence != peer\.last_state_sent_sequence\) \{"
                r"\s*this->log_rejection_\("
                r"\"Ignoring stale or mismatched STATE_ACK\"\);"
                r"\s*return;"
                r"\s*\}.*?"
                r"peer\.last_ack_boot_id = packet\.acked_boot_id;.*?"
                r"this->clear_warning_if_set_\(\);",
                re.DOTALL,
            ),
        )

    def test_ack_health_uses_pending_ack_and_warning_age(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("void check_ack_health_();", header)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::has_pending_ack_"
                r"\(\s*const PeerState &peer\) const \{.*?"
                r"peer\.last_state_sent_sequence != 0.*?"
                r"peer\.last_ack_boot_id != peer\.last_state_sent_boot_id \|\|"
                r"\s*peer\.last_ack_sequence != peer\.last_state_sent_sequence",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::check_ack_health_\(\).*?"
                r"this->peer_accepts_leader_state_\(peer\).*?"
                r"this->has_pending_ack_\(peer\).*?"
                r"now - peer\.last_state_sent_ms >= ACK_WARNING_MS.*?"
                r"peer\.missed_acks\+\+.*?"
                r"ESP_LOGW\(TAG, \"CFX Sync follower ACK missing.*?"
                r"this->status_set_warning\(\)",
                re.DOTALL,
            ),
        )

    def test_runtime_stores_ordered_light_collection(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "void add_light(light::LightState *light)", header
        )
        self.assertIn(
            "std::vector<light::LightState *> lights_;", header
        )
        self.assertNotIn("void set_light(", header)
        self.assertNotIn("light::LightState *light_{", header)
        self.assertIn("this->lights_[0]", source)

    def test_add_light_keeps_effect_vectors_aligned(self):
        header = HEADER.read_text(encoding="utf-8")

        self.assertIn('#include "cfx_sync_effect.h"', header)
        self.assertIn("this->lights_.push_back(light);", header)
        self.assertIn("this->effect_catalogs_.emplace_back();", header)
        self.assertIn("this->effect_log_states_.emplace_back();", header)
        self.assertIn("this->control_bindings_.emplace_back();", header)
        self.assertIn(
            "std::vector<std::vector<CFXSyncEffectEntry>> "
            "effect_catalogs_;",
            header,
        )
        self.assertIn(
            "std::vector<EffectLogState> effect_log_states_;", header
        )
        self.assertIn(
            "std::vector<ControlBinding> control_bindings_;", header
        )

    def test_add_effect_exposes_bounds_checked_codegen_contract(self):
        header = HEADER.read_text(encoding="utf-8")

        self.assertIn(
            "void add_effect(size_t light_index, uint8_t effect_id,",
            header,
        )
        self.assertIn(
            "if (light_index >= this->effect_catalogs_.size())", header
        )
        self.assertIn(
            "this->effect_catalogs_[light_index].push_back(", header
        )
        self.assertIn("CFXSyncEffectEntry{effect_id, name}", header)

    def test_effect_log_state_has_required_groundwork_fields(self):
        header = HEADER.read_text(encoding="utf-8")

        self.assertIn("struct EffectLogState", header)
        self.assertIn("bool valid{false};", header)
        self.assertIn(
            "CFXSyncEffectKind kind{CFXSyncEffectKind::NONE};", header
        )
        self.assertIn("uint8_t effect_id{0};", header)
        self.assertNotIn("uint8_t id{0};", header)
        self.assertIn("std::string name;", header)
        self.assertIn("uint32_t last_log_ms{0};", header)

    def test_effect_only_follower_state_is_actionable(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"packet\.has_power \|\| packet\.has_brightness \|\| "
                r"packet\.has_color \|\|\s*"
                r"packet\.has_color_brightness \|\| packet\.has_effect \|\|\s*"
                r"packet\.has_controls"
            ),
        )

    def test_follower_folds_effect_into_existing_light_call(self):
        source = SOURCE.read_text(encoding="utf-8")
        apply_light_body = re.search(
            r"bool CFXSyncComponent::apply_remote_state_to_light_"
            r"\(.*?\n\}\n\n#if defined\(USE_ESP32\)\nvoid CFXSyncComponent::apply_remote_controls_to_light_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(apply_light_body)
        apply_light_source = apply_light_body.group(0)

        self.assertIn("apply_remote_state_", source)
        self.assertIn("if (packet.has_power)", apply_light_source)
        self.assertIn(
            "if (packet.has_brightness && apply_visual_state)",
            apply_light_source,
        )
        self.assertIn(
            "if (packet.has_color && apply_visual_state)",
            apply_light_source,
        )
        self.assertIn("call.set_brightness(", apply_light_source)
        self.assertIn("call.set_color_brightness(", apply_light_source)
        self.assertIn("call.set_rgb(", apply_light_source)
        self.assertIn("call.set_white(", apply_light_source)
        self.assertIn("call.set_effect(", apply_light_source)
        esp32_apply_branch = apply_light_source.split("#else", 1)[1]
        self.assertEqual(esp32_apply_branch.count("light->make_call()"), 1)
        self.assertEqual(esp32_apply_branch.count("call.perform();"), 1)

    def test_follower_applies_ramp_or_transition_before_perform(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"if \(packet\.has_ramp && packet\.has_brightness\) \{"
                r"\s*call\.set_transition_length\(packet\.ramp_ms\);"
                r"\s*\} else if \(packet\.has_transition &&"
                r"\s*light->get_default_transition_length\(\) == 0\) \{"
                r"\s*call\.set_transition_length\(packet\.transition_ms\);"
                r"\s*\}.*?"
                r"call\.perform\(\);",
                re.DOTALL,
            ),
        )

    def test_follower_does_not_repaint_static_state_while_turning_off(self):
        source = SOURCE.read_text(encoding="utf-8")
        apply_light_body = re.search(
            r"bool CFXSyncComponent::apply_remote_state_to_light_"
            r"\(.*?\n\}\n\n#if defined\(USE_ESP32\)\nvoid CFXSyncComponent::apply_remote_controls_to_light_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(apply_light_body)
        apply_light_source = apply_light_body.group(0)

        self.assertRegex(
            apply_light_source,
            re.compile(
                r"const bool turning_off =\s*"
                r"packet\.has_power && !packet\.power &&"
                r"\s*light->remote_values\.is_on\(\);"
                r".*?const bool apply_visual_state = !turning_off;",
                re.DOTALL,
            ),
        )
        self.assertIn(
            "if (packet.has_brightness && apply_visual_state)",
            apply_light_source,
        )
        self.assertIn(
            "if (packet.has_color && apply_visual_state)",
            apply_light_source,
        )
        self.assertIn(
            "} else if (packet.has_color_brightness && apply_visual_state)",
            apply_light_source,
        )
        self.assertIn(
            "const bool may_select_effect =",
            apply_light_source,
        )
        self.assertIn(
            "apply_visual_state && (!packet.has_power || packet.power);",
            apply_light_source,
        )

    def test_follower_bypasses_native_transition_when_turning_off_effect(self):
        source = SOURCE.read_text(encoding="utf-8")
        apply_light_body = re.search(
            r"bool CFXSyncComponent::apply_remote_state_to_light_"
            r"\(.*?\n\}\n\n#if defined\(USE_ESP32\)\nvoid CFXSyncComponent::apply_remote_controls_to_light_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(apply_light_body)
        apply_light_source = apply_light_body.group(0)

        self.assertIn(
            'const bool turning_off_effect =\n'
            '      turning_off && light->get_effect_name() != "None";',
            apply_light_source,
        )
        self.assertRegex(
            apply_light_source,
            re.compile(
                r"const bool use_remote_effect_off_transition ="
                r"\s*turning_off_effect && packet\.has_transition &&"
                r"\s*light->get_default_transition_length\(\) == 0;.*?"
                r"const uint32_t saved_default_transition ="
                r"\s*use_remote_effect_off_transition"
                r"\s*\? light->get_default_transition_length\(\)"
                r"\s*: 0;.*?"
                r"if \(turning_off_effect\) \{"
                r"\s*if \(use_remote_effect_off_transition\) \{"
                r"\s*light->set_default_transition_length\(packet\.transition_ms\);"
                r"\s*\}"
                r"\s*call\.set_transition_length\(0\);"
                r"\s*\} else if \(packet\.has_ramp && packet\.has_brightness\) \{"
                r"\s*call\.set_transition_length\(packet\.ramp_ms\);"
                r"\s*\} else if \(packet\.has_transition &&"
                r"\s*light->get_default_transition_length\(\) == 0\) \{"
                r"\s*call\.set_transition_length\(packet\.transition_ms\);"
                r"\s*\}.*?"
                r"call\.perform\(\);"
                r"\s*if \(use_remote_effect_off_transition\) \{"
                r"\s*light->set_default_transition_length"
                r"\(saved_default_transition\);",
                re.DOTALL,
            ),
        )

    def test_cfx_button_helpers_publish_sync_ramp_timing_hints(self):
        self.assertTrue(
            CFX_DIMMER_TIMING_HEADER.exists(),
            "cfx_dimmer_timing.h must expose the shared button timing hint",
        )
        timing_header = CFX_DIMMER_TIMING_HEADER.read_text(encoding="utf-8")
        dimmer_source = CFX_DIMMER_SOURCE.read_text(encoding="utf-8")
        cct_source = CFX_CCT_SWEEPER_SOURCE.read_text(encoding="utf-8")
        hue_source = CFX_HUE_CYCLER_SOURCE.read_text(encoding="utf-8")
        sync_source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("struct CFXDimmerTimingHint", timing_header)
        self.assertIn("publish_light_ramp_hint", timing_header)
        self.assertIn("publish_light_ramp_duration_hint", timing_header)
        self.assertIn("bool has_ramp_duration{false};", timing_header)
        self.assertIn("capture_light_timing_hint", timing_header)
        self.assertIn("clear_light_timing_hint", timing_header)
        self.assertIn('#include "cfx_dimmer_timing.h"', dimmer_source)
        self.assertIn(
            "publish_light_ramp_hint(state, now + duration);",
            dimmer_source,
            "plain dimmer holds must publish one native ramp timeline",
        )
        self.assertIn("clear_light_timing_hint(state);", dimmer_source)
        self.assertRegex(
            dimmer_source,
            re.compile(
                r"if \(transition_ms == 0\) \{\s*"
                r"clear_light_timing_hint\(state\);\s*"
                r"publish_light_ramp_duration_hint\(state, transition_ms\);"
                r"\s*\}",
                re.DOTALL,
            ),
            "only immediate dimmer freezes should cancel follower ramps",
        )
        self.assertIn('#include "cfx_dimmer_timing.h"', cct_source)
        self.assertRegex(
            cct_source,
            re.compile(
                r"effective_transition_ms > 0.*?"
                r"cfx_dimmer::publish_light_ramp_hint"
                r"\(state,\s*millis\(\) \+ effective_transition_ms\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            cct_source,
            re.compile(
                r"effective_transition_ms > 0.*?"
                r"else \{\s*cfx_dimmer::clear_light_timing_hint\(state\);",
                re.DOTALL,
            ),
        )
        self.assertIn("cfx_dimmer::clear_light_timing_hint(state);", cct_source)
        self.assertIn('#include "cfx_dimmer_timing.h"', hue_source)
        self.assertRegex(
            hue_source,
            re.compile(
                r"transition_ms > 0.*?"
                r"cfx_dimmer::publish_light_ramp_hint"
                r"\(state, millis\(\) \+ transition_ms\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            hue_source,
            re.compile(
                r"transition_ms > 0.*?"
                r"else \{\s*cfx_dimmer::clear_light_timing_hint\(state\);",
                re.DOTALL,
            ),
        )
        self.assertIn("cfx_dimmer::clear_light_timing_hint(state);", hue_source)
        self.assertIn(
            '#include "../cfx_button/cfx_dimmer_timing.h"',
            sync_source,
        )
        self.assertIn("capture_light_timing_hint(leader, millis())", sync_source)

    def test_cfx_dimmer_uses_native_ramps_for_plain_lights(self):
        dimmer_header = (
            ROOT / "components" / "cfx_button" / "cfx_dimmer.h"
        ).read_text(encoding="utf-8")
        dimmer_source = CFX_DIMMER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "float freeze_brightness_(light::LightState *state,",
            dimmer_header,
        )
        self.assertNotIn("RAMP_STEP_TRANSITION_MS", dimmer_header)
        self.assertIn(
            "bool target_has_effect_(light::LightState *state) const;",
            dimmer_header,
        )
        self.assertIn(
            "const bool manual = this->target_has_effect_(state);",
            dimmer_source,
        )
        self.assertIn("this->ramp_manual_.push_back(manual);", dimmer_source)
        self.assertIn("publish_light_ramp_hint(state, now + duration);", dimmer_source)
        self.assertRegex(
            dimmer_source,
            re.compile(
                r"this->apply_brightness_\(state, manual \? start : target,"
                r"\s*manual \? 0 : duration\);.*?"
                r"this->service_manual_ramp_\(now\);",
                re.DOTALL,
            ),
        )
        self.assertIn(
            "this->apply_brightness_(this->lights_[i],",
            dimmer_source,
        )
        self.assertRegex(
            dimmer_source,
            re.compile(
                r"this->apply_brightness_\(this->lights_\[i\],"
                r"\s*this->ramp_current_brightness_\(i, now\), 0\);",
                re.DOTALL,
            ),
            "only effect-backed manual ramps should send repeated zero-transition steps",
        )
        self.assertIn(
            "bool measured_ramp_brightness_(light::LightState *state,",
            dimmer_header,
        )
        self.assertRegex(
            dimmer_source,
            re.compile(
                r"float CFXDimmer::freeze_brightness_"
                r"\(.*?float measured = 0.0f;.*?"
                r"this->measured_ramp_brightness_"
                r"\(state, index, now, measured\).*?"
                r"return measured;.*?"
                r"return this->ramp_current_brightness_\(index, now\);",
                re.DOTALL,
            ),
            "native dimmer release should prefer plausible measured output, "
            "then fall back to the commanded ramp timeline",
        )
        measured_body = re.search(
            r"bool CFXDimmer::measured_ramp_brightness_\(.*?\n\}\n\nfloat CFXDimmer::freeze_brightness_",
            dimmer_source,
            re.DOTALL,
        )
        self.assertIsNotNone(measured_body)
        body = measured_body.group(0)
        self.assertIn("state->current_values.get_brightness()", body)
        self.assertIn("state->current_values.get_state()", body)
        self.assertIn("std::abs(measured - estimated)", body)
        self.assertIn("RAMP_MEASURED_MAX_DRIFT", body)
        self.assertIn(
            "RAMP_MEASURED_MAX_DRIFT = 0.15f",
            dimmer_header,
            "native ESPHome transitions can legitimately lead or lag the "
            "linear estimate by about 10% on release; prefer a sane measured "
            "output over a visible snap back to the estimate",
        )
        self.assertIn("RAMP_MEASURED_EDGE_EPSILON", body)
        self.assertIn(
            "progress < RAMP_MEASURED_EDGE_PROGRESS",
            body,
            "premature measured edge targets must still be rejected",
        )
        self.assertIn(
            "float freeze_settle_brightness_(size_t index, float sampled,"
            "\n                                  uint32_t transition_ms,"
            "\n                                  bool project) const;",
            dimmer_header,
        )
        self.assertRegex(
            dimmer_source,
            re.compile(
                r"void CFXDimmer::freeze_ramp_\(uint32_t now\).*?"
                r"const float sampled = measured_ok \? measured : estimated;.*?"
                r"const uint32_t freeze_transition_ms =\s*measured_ok \? "
                r"RAMP_FREEZE_TRANSITION_MS : 0;.*?"
                r"const float current =\s*this->freeze_settle_brightness_"
                r"\(i, sampled,\s*freeze_transition_ms,\s*"
                r"measured_ok\);",
                re.DOTALL,
            ),
            "estimated-only segment releases must not run the short local "
            "freeze transition from stale current_values; measured releases "
            "can still use the gentle settle window",
        )
        settle_body = re.search(
            r"float CFXDimmer::freeze_settle_brightness_"
            r"\(size_t index, float sampled,\s*uint32_t transition_ms,"
            r"\s*bool project\).*?"
            r"\n\}\n\nfloat CFXDimmer::freeze_brightness_",
            dimmer_source,
            re.DOTALL,
        )
        self.assertIsNotNone(settle_body)
        settle = settle_body.group(0)
        self.assertIn("!project", settle)
        self.assertIn(
            "const float slope = (target - start) / static_cast<float>(duration);",
            settle,
        )
        self.assertIn(
            "sampled + (slope * static_cast<float>(transition_ms))",
            settle,
        )
        self.assertIn("return this->clamp_brightness_", settle)

    def test_cfx_dimmer_release_freeze_uses_short_settle_transition(self):
        dimmer_header = (
            ROOT / "components" / "cfx_button" / "cfx_dimmer.h"
        ).read_text(encoding="utf-8")
        dimmer_source = CFX_DIMMER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("RAMP_FREEZE_TRANSITION_MS", dimmer_header)
        self.assertRegex(
            dimmer_source,
            re.compile(
                r"void CFXDimmer::freeze_ramp_\(uint32_t now\).*?"
                r"const uint32_t freeze_transition_ms =\s*"
                r"measured_ok \? RAMP_FREEZE_TRANSITION_MS : 0;.*?"
                r"publish_light_ramp_duration_hint"
                r"\(state, 0\);.*?"
                r"this->apply_brightness_"
                r"\(state, current, freeze_transition_ms\);",
                re.DOTALL,
            ),
            "release freeze should gently settle measured native-ramp "
            "mismatch, but estimated-only releases must stop immediately so "
            "stale current_values cannot animate a visible bump",
        )

    def test_cfx_dimmer_brightness_updates_preserve_current_color_mode(self):
        dimmer_source = CFX_DIMMER_SOURCE.read_text(encoding="utf-8")
        apply_body = re.search(
            r"void CFXDimmer::apply_brightness_\(.*?\n\}\n\nvoid CFXDimmer::turn_on_targets_",
            dimmer_source,
            re.DOTALL,
        )
        self.assertIsNotNone(apply_body)
        source = apply_body.group(0)

        from_values = source.find(
            "this->apply_color_values_(call, state, state->remote_values);"
        )
        set_brightness = source.find("call.set_brightness(")
        self.assertNotEqual(
            from_values,
            -1,
            "dimming must preserve the current RGB/RGBW/white payload",
        )
        self.assertNotEqual(set_brightness, -1)
        self.assertLess(
            from_values,
            set_brightness,
            "dimming must copy the current light values before overriding brightness",
        )

    def test_cfx_dimmer_color_preserve_sets_explicit_color_mode(self):
        dimmer_header = (
            ROOT / "components" / "cfx_button" / "cfx_dimmer.h"
        ).read_text(encoding="utf-8")
        dimmer_source = CFX_DIMMER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "void apply_color_values_(light::LightCall &call,",
            dimmer_header,
        )
        self.assertIn("values.get_color_mode()", dimmer_source)
        self.assertIn("light::ColorMode::UNKNOWN", dimmer_source)
        self.assertIn("call.set_color_mode(light::ColorMode::RGB_WHITE);", dimmer_source)
        self.assertIn("call.set_color_mode(light::ColorMode::RGB);", dimmer_source)
        self.assertIn("call.set_color_mode(light::ColorMode::WHITE);", dimmer_source)
        self.assertIn("call.set_color_brightness(", dimmer_source)
        self.assertIn("call.set_rgb(", dimmer_source)
        self.assertIn("call.set_white(", dimmer_source)

    def test_cfx_dimmer_turn_on_fallback_preserves_current_color_mode(self):
        dimmer_source = CFX_DIMMER_SOURCE.read_text(encoding="utf-8")
        turn_on_body = re.search(
            r"void CFXDimmer::turn_on_targets_\(.*?\n\}\n\nvoid CFXDimmer::turn_off_targets_",
            dimmer_source,
            re.DOTALL,
        )
        self.assertIsNotNone(turn_on_body)
        source = turn_on_body.group(0)

        fallback = re.search(
            r"if \(i >= this->saved_states_\.size\(\) \|\|.*?"
            r"!this->saved_states_\[i\]\.valid\) \{(?P<body>.*?)continue;",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(fallback)
        body = fallback.group("body")
        from_values = body.find(
            "this->apply_color_values_(call, state, state->remote_values);"
        )
        set_state = body.find("call.set_state(true);")
        self.assertNotEqual(
            from_values,
            -1,
            "first dimmer ON must not let ESPHome choose White mode",
        )
        self.assertNotEqual(set_state, -1)
        self.assertLess(
            from_values,
            set_state,
            "first dimmer ON must copy the current color values before state ON",
        )

    def test_follower_fans_out_with_independent_light_calls(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        apply_light_body = re.search(
            r"bool CFXSyncComponent::apply_remote_state_to_light_"
            r"\(.*?\n\}\n\n#if defined\(USE_ESP32\)\nvoid CFXSyncComponent::apply_remote_controls_to_light_",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(apply_light_body)
        apply_light_source = apply_light_body.group(0)

        self.assertIn(
            "bool apply_remote_state_to_light_(", header
        )
        self.assertIn(
            "for (size_t i = 0; i < aligned_light_count; i++)", source
        )
        self.assertIn("auto *light = this->lights_[i];", source)
        self.assertIn(
            "applied |= this->apply_remote_state_to_light_(packet, i);",
            source,
        )
        self.assertIn("this->lights_[light_index]", apply_light_source)
        self.assertIn("this->effect_catalogs_[light_index]", apply_light_source)
        self.assertIn("this->effect_log_states_[light_index]", apply_light_source)
        self.assertIn("auto call = light->make_call();", apply_light_source)
        self.assertIn("light_supports_rgb_white(*light)", apply_light_source)
        self.assertIn("light_supports_rgb(*light)", apply_light_source)
        esp32_apply_branch = apply_light_source.split("#else", 1)[1]
        self.assertEqual(esp32_apply_branch.count("call.perform();"), 1)

    def test_follower_effect_lookup_is_exact_and_name_sensitive(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "find_effect_entry(catalog, packet.effect.effect_id,",
            source,
        )
        self.assertIn("packet.effect.name", source)
        self.assertIn("desired_effect = entry->name;", source)
        self.assertNotIn("strcasecmp", source)
        self.assertNotIn("casefold", source)

    def test_follower_only_sets_effect_when_name_changes(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"light->get_effect_name\(\) != desired_effect.*?"
                r"call\.set_effect\(desired_effect\);",
                re.DOTALL,
            ),
        )
        self.assertIn('std::string desired_effect{"None"};', source)

    def test_follower_does_not_select_effect_while_turning_off(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "apply_visual_state && (!packet.has_power || packet.power);",
            source,
        )
        self.assertIn(
            "if (packet.has_effect && may_select_effect)", source
        )

    def test_missing_and_unsupported_effects_fall_back_with_logi(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("CFXSyncEffectKind::CHIMERAFX", source)
        self.assertIn("CFXSyncEffectKind::UNSUPPORTED", source)
        self.assertGreaterEqual(source.count("ESP_LOGI("), 2)
        self.assertIn("packet.effect.effect_id", source)
        self.assertIn("packet.effect.name.c_str()", source)
        self.assertIn("light->get_name().c_str()", source)
        self.assertIn("static_cast<unsigned>(light_index)", source)
        self.assertIn("EFFECT_FALLBACK_LOG_INTERVAL_MS", source)
        self.assertIn(
            "EFFECT_FALLBACK_LOG_INTERVAL_MS = 30000", header
        )
        self.assertRegex(
            source,
            re.compile(
                r"now - log_state\.last_log_ms\s*>=\s*"
                r"EFFECT_FALLBACK_LOG_INTERVAL_MS"
            ),
        )

    def test_effect_fallback_throttle_tracks_identity_per_light(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "auto &log_state = "
            "this->effect_log_states_[light_index];",
            source,
        )
        self.assertIn("log_state.kind != packet.effect.kind", source)
        self.assertIn(
            "log_state.effect_id != packet.effect.effect_id", source
        )
        self.assertIn("log_state.name != packet.effect.name", source)
        self.assertIn("log_state.valid = true;", source)
        self.assertIn("log_state.kind = packet.effect.kind;", source)
        self.assertIn(
            "log_state.effect_id = packet.effect.effect_id;", source
        )
        self.assertIn("log_state.name = packet.effect.name;", source)

    def test_dump_config_lists_all_bound_lights(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn('"  Discovery: group authenticated\\n"', source)
        self.assertIn('"  Peers: active=%u followers=%u remotes=%u\\n"', source)
        self.assertIn('"  ACK: pending=%u missed=%"', source)
        self.assertIn("this->active_peer_count_()", source)
        self.assertIn("this->follower_peer_count_()", source)
        self.assertIn("this->remote_peer_count_()", source)
        self.assertIn("this->pending_ack_count_()", source)
        self.assertIn("this->missed_ack_count_()", source)
        self.assertIn('"  Lights: %u"', source)
        self.assertIn(
            "for (size_t i = 0; i < this->lights_.size(); i++)",
            source,
        )
        self.assertIn('"    [%u] %s"', source)

    def test_runtime_reports_discovered_peer_health(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        for helper in (
            "uint8_t active_peer_count_() const;",
            "uint8_t follower_peer_count_() const;",
            "uint8_t remote_peer_count_() const;",
            "uint8_t pending_ack_count_() const;",
            "uint32_t missed_ack_count_() const;",
        ):
            self.assertIn(helper, header)

        self.assertIn(
            'ESP_LOGI(TAG, "Discovered CFX Sync %s peer %s in group %08" PRIX32',
            source,
        )
        self.assertIn(
            'ESP_LOGW(TAG, "CFX Sync peer table full; ignoring %s", peer_buf);',
            source,
        )

    def test_sync_component_has_no_renderer_or_effect_dependency(self):
        component_dir = ROOT / "components" / "cfx_sync"
        source_files = list(component_dir.glob("*.h")) + list(
            component_dir.glob("*.cpp")
        )
        texts = "\n".join(
            path.read_text(encoding="utf-8") for path in source_files
        ).lower()

        for forbidden in (
            "cfx_light/",
            "cfx_effect",
            "cfxrunner",
            "cfx_runner",
            "renderer",
        ):
            self.assertNotIn(forbidden, texts)


if __name__ == "__main__":
    unittest.main()
