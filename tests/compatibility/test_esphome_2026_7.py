from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SEQUENCE_SOURCE = ROOT / "components" / "cfx_sequence" / "cfx_sequence.cpp"
CONTROL_HEADER = ROOT / "components" / "cfx_effect" / "cfx_control.h"
EFFECT_SOURCE = (
    ROOT / "components" / "cfx_effect" / "cfx_addressable_light_effect.cpp"
)
CCT_SOURCE = ROOT / "components" / "cfx_button" / "cfx_cct_sweeper.cpp"
SCHEDULER_SOURCE = ROOT / "components" / "cfx_effect" / "cfx_scheduler.cpp"
RUNNER_SOURCE = ROOT / "components" / "cfx_effect" / "CFXRunner.cpp"
SYNC_BUS_HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync_bus.h"
SYNC_BUS_SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync_bus.cpp"
SYNC_SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync.cpp"
UTILS_HEADER = ROOT / "components" / "cfx_effect" / "cfx_utils.h"
LIGHT_SOURCE = ROOT / "components" / "cfx_light" / "cfx_light.cpp"
VIRTUAL_SEGMENT_HEADER = (
    ROOT / "components" / "cfx_light" / "cfx_virtual_segment_light.h"
)
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

    def test_diagnostic_integer_formats_are_width_portable(self):
        utils = UTILS_HEADER.read_text(encoding="utf-8")
        light = LIGHT_SOURCE.read_text(encoding="utf-8")
        segment = VIRTUAL_SEGMENT_HEADER.read_text(encoding="utf-8")
        self.assertNotIn("Heap: %ukB", utils)
        self.assertNotIn("heap=%ukB", light)
        self.assertIn('Heap: %" PRIu32 "kB', utils)
        self.assertIn('(%" PRId32', segment)

    def test_native_toolchain_warning_regressions(self):
        cct = CCT_SOURCE.read_text(encoding="utf-8")
        scheduler = SCHEDULER_SOURCE.read_text(encoding="utf-8")
        runner = RUNNER_SOURCE.read_text(encoding="utf-8")
        light = LIGHT_SOURCE.read_text(encoding="utf-8")
        effect = EFFECT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('transition=%" PRIu32 "ms', cct)
        self.assertNotIn("transition=%ums", cct)
        self.assertNotIn("dispatch_us=%u", scheduler)
        self.assertIn('dispatch_us=%" PRIu32', scheduler)
        self.assertIn('Entropy=%" PRIu32', runner)
        self.assertNotIn("pixel_count >= 0", runner)
        self.assertNotIn("filledPixels >= 0", runner)
        self.assertGreaterEqual(runner.count("[[maybe_unused]]"), 6)
        self.assertNotIn('(% " PRIu32', light)
        self.assertNotIn("rmt_symbols=%u", light)
        self.assertIn('idx=%" PRId32', light)
        self.assertGreaterEqual(light.count("[[maybe_unused]]"), 9)
        self.assertIn('Heap too low to start effect (%" PRIu32', effect)
        self.assertIn('update_interval=%" PRIu32', effect)
        self.assertIn('dt=%" PRIu32', effect)
        self.assertNotIn("brightness > 255u", effect)
        self.assertNotIn("(int)brightness > 255", effect)
        self.assertNotIn("(int)brightness < 0", effect)
        self.assertIn("std::min<uint32_t>(ramp_brightness, 255u)", effect)
        self.assertIn("std::clamp(pulse_brightness, 0, 255)", effect)

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
