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
        self.assertIn("## Multiple Groups", text)
        self.assertIn("more than one `cfx_sync` block", text)
        self.assertNotIn("Multiple sync groups on the same device.", text)
        self.assertNotIn("Mimic", text)
        self.assertNotIn("valve", text.lower())
        self.assertNotIn("fan controller", text.lower())

    def test_docs_keep_esp8266_satellite_boundary_clear(self):
        text = CFX_SYNC_DOC.read_text(encoding="utf-8")

        self.assertIn(
            "ESP8266 satellites are basic UDP light followers for ON/OFF and brightness only.",
            text,
        )
        self.assertIn(
            "does not apply synced RGB/RGBW color, color temperature, cold/warm white",
            text,
        )


if __name__ == "__main__":
    unittest.main()
