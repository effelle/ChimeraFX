from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "components" / "cfx_sync" / "cfx_sync.h"
SOURCE = ROOT / "components" / "cfx_sync" / "cfx_sync.cpp"


class ESPNowAPITests(unittest.TestCase):
    def test_uses_esphome_2026_5_receive_api(self):
        header = HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool on_receive(", header)
        self.assertIn("register_receive_handler(this)", source)
        self.assertIn("CFXSyncComponent::on_receive(", source)
        self.assertNotIn("on_received(", header)
        self.assertNotIn("register_received_handler", source)


if __name__ == "__main__":
    unittest.main()
