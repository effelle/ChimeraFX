from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync.h"
SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync.cpp"
PACKET_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_packet.h"
PACKET_SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync_packet.cpp"
COLOR_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_color.h"


class ESPNowAPITests(unittest.TestCase):
    def test_uses_esphome_2026_5_receive_api(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool on_receive(", header)
        self.assertIn("register_receive_handler(this)", source)
        self.assertIn("CFXSyncComponent::on_receive(", source)
        self.assertNotIn("on_received(", header)
        self.assertNotIn("register_received_handler", source)

    def test_packet_codec_exposes_full_v1_state(self):
        header = PACKET_HEADER.read_text(encoding="utf-8")
        source = PACKET_SOURCE.read_text(encoding="utf-8")

        self.assertIn("FIELD_BRIGHTNESS = 0x00000002UL", header)
        self.assertIn("FIELD_COLOR = 0x00000004UL", header)
        self.assertIn("FULL_STATE_PAYLOAD_SIZE = 11", header)
        self.assertIn("packet.has_brightness = true", source)
        self.assertIn("packet.has_color = true", source)

    def test_color_helper_is_renderer_independent(self):
        text = COLOR_HEADER.read_text(encoding="utf-8")

        self.assertIn("struct CFXSyncLightSnapshot", text)
        self.assertIn("convert_color_for_follower", text)
        self.assertIn("light::LightState &state", text)
        self.assertNotIn("const light::LightState &state", text)
        self.assertNotIn("cfx_light", text)
        self.assertNotIn("cfx_effect", text)
        self.assertNotIn("runner", text)

    def test_runtime_tracks_complete_snapshots(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("CFXSyncLightSnapshot observed_state_", header)
        self.assertIn("capture_light_snapshot(*this->light_)", source)
        self.assertNotIn("observed_power_", header)

    def test_follower_applies_one_call_without_effect_changes(self):
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("apply_remote_state_", source)
        self.assertIn("if (packet.has_power)", source)
        self.assertIn("if (packet.has_brightness)", source)
        self.assertIn("if (packet.has_color)", source)
        self.assertIn("call.set_brightness(", source)
        self.assertIn("call.set_rgb(", source)
        self.assertIn("call.set_white(", source)
        self.assertNotIn("call.set_effect(", source)

    def test_sync_component_has_no_renderer_or_effect_dependency(self):
        component_dir = ROOT / "components" / "cfx_sync"
        source_files = list(component_dir.glob("*.h")) + list(
            component_dir.glob("*.cpp")
        )
        texts = "\n".join(
            path.read_text(encoding="utf-8") for path in source_files
        )

        for forbidden in (
            "cfx_light/",
            "cfx_effect",
            "cfx_runner",
            "set_effect(",
        ):
            self.assertNotIn(forbidden, texts)


if __name__ == "__main__":
    unittest.main()
