from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
PY_COMPONENT = ROOT / "components" / "cfx_sync" / "__init__.py"
HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync.h"
SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync.cpp"
PACKET_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_packet.h"
PACKET_SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync_packet.cpp"
COLOR_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_color.h"
EFFECT_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_effect.h"


class ESPNowAPITests(unittest.TestCase):
    def _effect_header_text(self):
        self.assertTrue(
            EFFECT_HEADER.exists(),
            "cfx_sync_effect.h must define the effect identity model",
        )
        return EFFECT_HEADER.read_text(encoding="utf-8")

    def test_uses_version_conditional_receive_api(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "esphome/core/version.h"', header)
        self.assertIn(
            "#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)",
            header,
        )
        self.assertIn(
            "public espnow::ESPNowReceivedPacketHandler,\n"
            "                         public espnow::ESPNowUnknownPeerHandler,\n"
            "#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)\n"
            "                         public espnow::ESPNowBroadcastedHandler",
            header,
        )
        self.assertIn("bool on_received(", header)
        self.assertIn("bool on_receive(", header)
        self.assertIn("bool on_unknown_peer(", header)
        self.assertIn("bool handle_packet_(", header)
        self.assertIn(
            "#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)",
            source,
        )
        self.assertIn("register_received_handler(this)", source)
        self.assertIn("register_receive_handler(this)", source)
        self.assertIn("register_unknown_peer_handler(this)", source)
        self.assertIn("CFXSyncComponent::on_received(", source)
        self.assertIn("CFXSyncComponent::on_receive(", source)
        self.assertIn("CFXSyncComponent::on_unknown_peer(", source)
        self.assertIn("CFXSyncComponent::handle_packet_(", source)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::on_received\(.*?\).*?"
                r"return this->handle_packet_\(info, data, size\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::on_receive\(.*?\).*?"
                r"return this->handle_packet_\(info, data, size\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::on_unknown_peer\(.*?\).*?"
                r"return this->admit_unknown_peer_\(info, data, size\);",
                re.DOTALL,
            ),
        )

    def test_registers_version_conditional_broadcast_handler_for_discovery(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("public espnow::ESPNowBroadcastedHandler", header)
        self.assertIn("public espnow::ESPNowBroadcastHandler", header)
        self.assertIn("bool on_broadcasted(", header)
        self.assertIn("bool on_broadcast(", header)
        self.assertIn("register_broadcasted_handler(this)", source)
        self.assertIn("register_broadcast_handler(this)", source)
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::on_broadcasted\(.*?\).*?"
                r"return this->handle_packet_\(info, data, size\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::on_broadcast\(.*?\).*?"
                r"return this->handle_packet_\(info, data, size\);",
                re.DOTALL,
            ),
        )

    def test_unknown_peers_are_authenticated_before_discovery_admission(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "bool admit_unknown_peer_(const espnow::ESPNowRecvInfo &info,",
            header,
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::admit_unknown_peer_\(.*?"
                r"CFXSyncPacketCodec::decode\(.*?"
                r"result == CFXSyncDecodeResult::NOT_CFX.*?"
                r"return false;.*?"
                r"result != CFXSyncDecodeResult::OK.*?"
                r"this->handle_decode_failure_\(result\);.*?"
                r"return true;.*?"
                r"packet\.type != CFXSyncPacketType::HELLO &&"
                r"\s*packet\.type != CFXSyncPacketType::SYNC_REQUEST.*?"
                r"authenticated non-discovery packet from unknown peer.*?"
                r"this->find_or_add_peer_\(info\.src_addr, peer_role, "
                r"capabilities\).*?"
                r"this->register_peer_\(\*peer\).*?"
                r"return false;",
                re.DOTALL,
            ),
        )

    def test_schema_requires_public_espnow_and_hides_peer_config(self):
        py_source = PY_COMPONENT.read_text(encoding="utf-8")

        self.assertIn('AUTO_LOAD = ["cfx_effect_registry", "hmac_sha256"]', py_source)
        self.assertIn('DEPENDENCIES = ["esp32", "espnow", "light"]', py_source)
        self.assertIn(
            'cv.Required(CONF_ESPNOW_ID): cv.use_id(\n'
            '                espnow.ESPNowComponent\n'
            '            )',
            py_source,
        )
        self.assertIn('cv.Optional(CONF_PEER): cv.invalid(', py_source)
        self.assertNotIn("cv.Required(CONF_PEER)", py_source)

    def test_codegen_uses_public_espnow_component(self):
        py_source = PY_COMPONENT.read_text(encoding="utf-8")

        self.assertRegex(
            py_source,
            re.compile(
                r"espnow_var = await cg\.get_variable\(config\[CONF_ESPNOW_ID\]\).*?"
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

    def test_leader_does_not_schedule_periodic_hello_transmit(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("void schedule_follower_hello_();", header)
        self.assertRegex(
            source,
            re.compile(
                r"this->send_hello_\(\);"
                r"\s*if \(this->role_ != CFXSyncRole::LEADER\) \{"
                r"\s*this->schedule_follower_hello_\(\);"
                r"\s*\}",
                re.DOTALL,
            ),
        )
        self.assertNotIn('set_interval("hello"', source)

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
                r"\(\"sync-request-1\", 1000\);.*?"
                r"this->schedule_follower_recovery_attempt_"
                r"\(\"sync-request-2\", 2000\);.*?"
                r"this->schedule_follower_recovery_attempt_"
                r"\(\"sync-request-3\", 4000\);",
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
                r"\s*const bool new_peer = peer == nullptr;"
                r"\s*const bool peer_rebooted ="
                r"\s*peer != nullptr && peer->has_rx_sequence &&"
                r"\s*peer->rx_boot_id != packet\.boot_id;.*?"
                r"this->should_send_state_for_hello_"
                r"\(\*peer, new_peer,\s*peer_rebooted\).*?"
                r"this->send_state_\(\);.*?"
                r"this->check_ack_health_\(\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::should_send_state_for_hello_"
                r"\(\s*const PeerState &peer, bool new_peer, "
                r"bool peer_rebooted\) const \{"
                r"\s*if \(this->role_ != CFXSyncRole::LEADER \|\|"
                r"\s*!this->peer_accepts_leader_state_\(peer\)\) \{"
                r"\s*return false;"
                r"\s*\}"
                r"\s*return new_peer \|\| peer_rebooted \|\|"
                r"\s*peer\.last_state_sent_sequence == 0;",
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
                r"this->send_state_\(snapshot, effect, controls\);",
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
                r"bool CFXSyncComponent::send_state_\(\) \{.*?"
                r"const auto snapshot = capture_light_snapshot\(\*leader\);.*?"
                r"const auto effect = capture_effect_state\(leader, this->effect_catalogs_\[0\]\);.*?"
                r"const auto controls = this->capture_control_state_\(0\);.*?"
                r"this->send_state_to_followers_\(snapshot, effect, controls\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_\(\s*"
                r"const CFXSyncLightSnapshot &snapshot,\s*"
                r"const CFXSyncEffectState &effect,\s*"
                r"const CFXSyncControlState &controls\).*?"
                r"this->send_state_to_followers_\(snapshot, effect, controls\);.*?"
                r"bool CFXSyncComponent::send_state_to_followers_\(.*?"
                r"snapshot\.has_white,\s*true,\s*effect,\s*"
                r"controls\.has_any\(\),\s*controls,\s*this->key_, packet",
                re.DOTALL,
            ),
        )
        self.assertIn(
            "if (packet.type == CFXSyncPacketType::SYNC_REQUEST)", source
        )
        self.assertIn("this->send_state_();", source)
        self.assertIn("this->send_state_(snapshot, effect, controls);", source)

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
                r"bool CFXSyncComponent::send_state_\(\) \{.*?"
                r"const auto snapshot = capture_light_snapshot\(\*leader\);.*?"
                r"const auto effect = capture_effect_state\(leader, this->effect_catalogs_\[0\]\);.*?"
                r"const auto controls = this->capture_control_state_\(0\);.*?"
                r"return this->send_state_to_followers_\(snapshot, effect, controls\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_\(\s*"
                r"const CFXSyncLightSnapshot &snapshot,\s*"
                r"const CFXSyncEffectState &effect,\s*"
                r"const CFXSyncControlState &controls\).*?"
                r"return this->send_state_to_followers_\(snapshot, effect, controls\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_to_followers_\(\s*"
                r"const CFXSyncLightSnapshot &snapshot,\s*"
                r"const CFXSyncEffectState &effect,\s*"
                r"const CFXSyncControlState &controls\).*?"
                r"const uint32_t sequence = this->next_sequence_\(\);.*?"
                r"CFXSyncPacketCodec::encode_state\(.*?"
                r"this->send_packet_to_\(BROADCAST_MAC, packet\).*?"
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
                r"\s*this->role_ == CFXSyncRole::FOLLOWER\).*?"
                r"this->find_or_add_peer_\(info\.src_addr,"
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
                r"this->find_or_add_peer_\(info\.src_addr,"
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
                r"bool CFXSyncComponent::send_packet_to_"
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
                r"\(.*?this->check_ack_health_\(\);"
                r"\s*if \(this->send_pending_\) \{"
                r"\s*this->state_send_deferred_ = true;"
                r"\s*return false;"
                r"\s*\}",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::flush_deferred_state_\(\) \{"
                r"\s*if \(!this->state_send_deferred_ \|\| "
                r"this->send_pending_ \|\|"
                r"\s*this->role_ != CFXSyncRole::LEADER\) \{"
                r"\s*return;"
                r"\s*\}"
                r"\s*this->state_send_deferred_ = false;"
                r"\s*this->send_state_\(\);",
                re.DOTALL,
            ),
        )

    def test_broadcast_state_return_value_reflects_successfully_queued_send(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_to_followers_\(.*?"
                r"if \(!this->send_packet_to_\(BROADCAST_MAC, packet\)\) \{"
                r"\s*return false;"
                r"\s*\}"
                r"\s*this->mark_state_sent_to_followers_\(sequence\);"
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
                r"this->apply_remote_state_\(packet\);"
                r"\s*this->schedule_state_ack_\(info\.src_addr, packet,\s*"
                r"CFXSyncAckResult::APPLIED\);",
                re.DOTALL,
            ),
        )

    def test_state_ack_is_encoded_to_destination_mac_after_jitter(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::schedule_state_ack_"
                r"\(\s*const uint8_t \*destination,\s*"
                r"const CFXSyncPacket &packet,\s*"
                r"CFXSyncAckResult result\).*?"
                r"std::array<uint8_t, 6> mac\{\};.*?"
                r"memcpy\(mac\.data\(\), destination, mac\.size\(\)\);.*?"
                r"const uint32_t delay_ms\s*=\s*ACK_JITTER_MIN_MS \+"
                r"\s*\(esp_random\(\) % \(ACK_JITTER_SPREAD_MS \+ 1\)\);.*?"
                r"this->set_timeout\(\s*\"state-ack\",\s*delay_ms,\s*"
                r"\[this, mac, acked_boot_id, acked_sequence, result\]\(\) \{.*?"
                r"CFXSyncPacketCodec::encode_state_ack\("
                r"\s*this->group_hash_,\s*this->boot_id_,\s*"
                r"this->next_sequence_\(\),\s*"
                r"acked_boot_id,\s*acked_sequence,\s*result,\s*"
                r"this->key_,\s*ack\).*?"
                r"this->send_packet_to_\(mac, ack\);",
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
                r"\s*if \(this->role_ == CFXSyncRole::LEADER\) \{"
                r"\s*this->handle_state_ack_\(\*peer, packet\);"
                r"\s*\}"
                r"\s*return true;"
                r"\s*\}",
                re.DOTALL,
            ),
        )

    def test_handle_state_ack_matches_last_sent_before_clearing(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool has_pending_ack_(const PeerState &peer) const;", header)
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
                r"this->status_clear_warning\(\);",
                re.DOTALL,
            ),
        )

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
                r"this->status_clear_warning\(\);",
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

        self.assertIn("apply_remote_state_", source)
        self.assertIn("if (packet.has_power)", source)
        self.assertIn("if (packet.has_brightness)", source)
        self.assertIn("if (packet.has_color)", source)
        self.assertIn("call.set_brightness(", source)
        self.assertIn("call.set_color_brightness(", source)
        self.assertIn("call.set_rgb(", source)
        self.assertIn("call.set_white(", source)
        self.assertIn("call.set_effect(", source)
        self.assertEqual(source.count("light->make_call()"), 1)
        self.assertEqual(source.count("\n  call.perform();\n}"), 1)

    def test_follower_fans_out_with_independent_light_calls(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "void apply_remote_state_to_light_(", header
        )
        self.assertIn(
            "for (size_t i = 0; i < aligned_light_count; i++)", source
        )
        self.assertIn("auto *light = this->lights_[i];", source)
        self.assertIn(
            "this->apply_remote_state_to_light_(packet, i);",
            source,
        )
        self.assertIn("this->lights_[light_index]", source)
        self.assertIn("this->effect_catalogs_[light_index]", source)
        self.assertIn("this->effect_log_states_[light_index]", source)
        self.assertIn("auto call = light->make_call();", source)
        self.assertIn("light_supports_rgb_white(*light)", source)
        self.assertIn("light_supports_rgb(*light)", source)
        self.assertEqual(source.count("\n  call.perform();\n}"), 1)

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
            "const bool may_select_effect = "
            "!packet.has_power || packet.power;",
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
