from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SEQUENCE_SOURCE = ROOT / "components" / "cfx_sequence" / "cfx_sequence.cpp"
CONTROL_HEADER = ROOT / "components" / "cfx_effect" / "cfx_control.h"
EFFECT_SOURCE = (
    ROOT / "components" / "cfx_effect" / "cfx_addressable_light_effect.cpp"
)
SYNC_BUS_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_bus.h"
SYNC_BUS_SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync_bus.cpp"
SYNC_SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync.cpp"
INSTALL_DOCS = ROOT / "docs" / "Installation.md"
README = ROOT / "README.md"


class ESPHome20267CompatibilityTests(unittest.TestCase):
    def test_sequence_select_uses_current_select_api(self):
        source = SEQUENCE_SOURCE.read_text(encoding="utf-8")
        self.assertNotIn("->has_state()", source)
        self.assertIn("->active_index()", source)
        self.assertIn("->current_option()", source)

    def test_effect_selects_use_current_select_api(self):
        header = CONTROL_HEADER.read_text(encoding="utf-8")
        source = EFFECT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("value->active_index().has_value()", header)
        self.assertNotIn("palette_->has_state()", header)
        self.assertNotIn("palette_sel->has_state()", source)
        self.assertNotIn("intro_sel->has_state()", source)
        self.assertNotIn("out_eff->has_state()", source)

    def test_espnow_uses_2026_7_handler_names_and_packet_size(self):
        header = SYNC_BUS_HEADER.read_text(encoding="utf-8")
        source = SYNC_BUS_SOURCE.read_text(encoding="utf-8")
        self.assertIn("using CFXSyncESPNowPacketSize = uint16_t;", header)
        self.assertIn("register_receive_handler(this)", source)
        self.assertIn("register_broadcast_handler(this)", source)
        self.assertNotIn("register_received_handler", source)
        self.assertNotIn("register_broadcasted_handler", source)

    def test_off_state_does_not_apply_synced_brightness(self):
        source = SYNC_SOURCE.read_text(encoding="utf-8")
        self.assertIn(
            "const bool apply_visual_state = "
            "!(packet.has_power && !packet.power);",
            source,
        )
        self.assertIn("const bool apply_visual_state = !turning_off;", source)
        self.assertIn(
            "packet.has_brightness && supports_brightness && "
            "apply_visual_state",
            source,
        )
        self.assertIn(
            "packet.has_brightness && apply_visual_state &&",
            source,
        )

    def test_documented_minimum_version_is_2026_7(self):
        installation = INSTALL_DOCS.read_text(encoding="utf-8")
        readme = README.read_text(encoding="utf-8")
        self.assertIn(
            "minimum version to run ChimeraFX for ESPHome is **2026.7.0**",
            installation,
        )
        self.assertIn("requires ESPHome **2026.7.0 or later**", readme)

    def test_2026_7_migration_notes_are_documented(self):
        installation = INSTALL_DOCS.read_text(encoding="utf-8")
        self.assertIn("native ESP-IDF build toolchain by default", installation)
        self.assertIn("Do not add a `toolchain:` option", installation)
        self.assertIn("explicit brightness of `0` as OFF", installation)


if __name__ == "__main__":
    unittest.main()
