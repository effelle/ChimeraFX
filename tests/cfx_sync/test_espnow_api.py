from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
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
            "#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)\n"
            "                         public espnow::ESPNowBroadcastedHandler",
            header,
        )
        self.assertIn("bool on_received(", header)
        self.assertIn("bool on_receive(", header)
        self.assertIn("bool handle_packet_(", header)
        self.assertIn(
            "#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)",
            source,
        )
        self.assertIn("register_received_handler(this)", source)
        self.assertIn("register_receive_handler(this)", source)
        self.assertIn("CFXSyncComponent::on_received(", source)
        self.assertIn("CFXSyncComponent::on_receive(", source)
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
                r"bool CFXSyncComponent::send_state_to_peer_\(.*?"
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

    def test_leader_state_send_paths_fan_out_to_discovered_followers(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool send_state_to_followers_();", header)
        self.assertIn("bool peer_accepts_leader_state_(const PeerState &peer) const;", header)
        self.assertIn(
            "bool send_state_to_peer_(PeerState &peer,\n"
            "                           const CFXSyncLightSnapshot &snapshot,\n"
            "                           const CFXSyncEffectState &effect,\n"
            "                           const CFXSyncControlState &controls);",
            header,
        )
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
                r"for \(auto &peer : this->peers_\).*?"
                r"this->peer_accepts_leader_state_\(peer\).*?"
                r"this->send_state_to_peer_\(peer, snapshot, effect, controls\)",
                re.DOTALL,
            ),
        )

    def test_leader_state_target_selection_requires_registered_light_followers(self):
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
        self.assertIn("peer.last_state_sent_boot_id = this->boot_id_;", source)
        self.assertIn("peer.last_state_sent_sequence = sequence;", source)
        self.assertIn("CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER", source)

    def test_fanout_state_send_health_is_tracked_per_peer(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("uint8_t consecutive_send_failures{0};", header)
        self.assertIn("uint32_t send_failures{0};", header)
        self.assertIn("uint32_t last_send_failure_log_ms{0};", header)
        self.assertIn("bool has_peer_send_warning_() const;", header)
        self.assertIn("void handle_peer_send_result_(PeerState &peer, esp_err_t result);", header)
        self.assertIn(
            "bool send_packet_to_peer_(PeerState &peer,\n"
            "                            std::vector<uint8_t> &packet);",
            header,
        )
        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_packet_to_peer_"
                r"\(\s*PeerState &peer,\s*std::vector<uint8_t> &packet\).*?"
                r"\[this, &peer\]\(esp_err_t send_result\).*?"
                r"this->handle_peer_send_result_\(peer, send_result\)",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            source,
            re.compile(
                r"void CFXSyncComponent::handle_peer_send_result_"
                r"\(\s*PeerState &peer,\s*esp_err_t result\).*?"
                r"peer\.consecutive_send_failures = 0;.*?"
                r"!this->has_peer_send_warning_\(\).*?"
                r"this->status_clear_warning\(\).*?"
                r"peer\.send_failures\+\+;.*?"
                r"peer\.consecutive_send_failures\+\+;.*?"
                r"format_mac_addr_upper\(peer\.mac\.data\(\), peer_buf\).*?"
                r"peer\.consecutive_send_failures >=\s*"
                r"MAX_CONSECUTIVE_SEND_FAILURES.*?"
                r"this->status_set_warning\(\)",
                re.DOTALL,
            ),
        )

    def test_fanout_return_value_reflects_successfully_queued_peer_send(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertRegex(
            source,
            re.compile(
                r"bool CFXSyncComponent::send_state_to_followers_\(.*?"
                r"bool sent = false;.*?"
                r"if \(this->send_state_to_peer_"
                r"\(peer, snapshot, effect, controls\)\) \{.*?"
                r"sent = true;.*?"
                r"return sent;",
                re.DOTALL,
            ),
        )
        self.assertIn("this->send_packet_to_peer_(peer, packet)", source)
        self.assertNotIn("attempted = true;", source)

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

        self.assertIn('"  Lights: %u"', source)
        self.assertIn(
            "for (size_t i = 0; i < this->lights_.size(); i++)",
            source,
        )
        self.assertIn('"    [%u] %s"', source)

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
