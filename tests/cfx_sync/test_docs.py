from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CFX_SYNC_DOC = ROOT / "docs" / "cfx_sync.md"


class CFXSyncDocsTests(unittest.TestCase):
    def test_docs_describe_lights_only_canonical_sync_contract(self):
        text = CFX_SYNC_DOC.read_text(encoding="utf-8")

        self.assertIn("ESPHome's normal light state", text)
        self.assertIn("standard ESPHome lights can follow power, brightness", text)
        self.assertIn("Color temperature when both lights support it.", text)
        self.assertIn(
            "Cold white and warm white channels when both lights support them.",
            text,
        )
        self.assertIn("## Normal ESPHome Lights", text)
        self.assertIn("PWM, Tuya, or monochrome ESPHome light", text)
        self.assertIn(
            "The follower keeps its current color, but it still follows ON/OFF and brightness changes from the leader.",
            text,
        )
        self.assertIn("## Multiple Groups", text)
        self.assertIn("more than one `cfx_sync` block", text)
        self.assertNotIn("Multiple sync groups on the same device.", text)
        self.assertNotIn("Mimic", text)
        self.assertNotIn("valve", text.lower())
        self.assertNotIn("fan controller", text.lower())

    def test_docs_keep_esp8266_satellite_boundary_clear(self):
        text = CFX_SYNC_DOC.read_text(encoding="utf-8")

        self.assertIn(
            "ESP8266 followers and satellites use UDP and can follow normal ESPHome light state.",
            text,
        )
        self.assertIn(
            "They do not run ChimeraFX effects or apply ChimeraFX controls.",
            text,
        )

    def test_docs_explain_tuya_mcu_satellite_light_input(self):
        text = CFX_SYNC_DOC.read_text(encoding="utf-8")

        self.assertIn("## Tuya MCU Dimmers With Hidden Buttons", text)
        self.assertIn("local_light_input: true", text)
        self.assertIn("Tuya MCU", text)
        self.assertIn("not exposed as ESPHome binary sensors", text)
        self.assertIn("The satellite must have exactly one light", text)
        self.assertIn("The remote button type must match the leader `remote_input`", text)
        self.assertIn(
            "On ESP8266, the default `transport: auto` uses UDP automatically.",
            text,
        )
        tuya_section = text.split("## Tuya MCU Dimmers With Hidden Buttons", 1)[1]
        tuya_section = tuya_section.split("## What Gets Copied", 1)[0]
        self.assertNotIn("transport: udp", tuya_section)


if __name__ == "__main__":
    unittest.main()
