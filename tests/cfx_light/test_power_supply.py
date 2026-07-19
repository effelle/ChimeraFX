from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
PYTHON_COMPONENT = ROOT / "components" / "cfx_light" / "light.py"
CPP_HEADER = ROOT / "components" / "cfx_light" / "cfx_light.h"
CPP_SOURCE = ROOT / "components" / "cfx_light" / "cfx_light.cpp"
DOCS = ROOT / "docs" / "cfx_light.md"


class CFXLightPowerSupplyTests(unittest.TestCase):
    def test_schema_uses_standard_esphome_power_supply(self):
        source = PYTHON_COMPONENT.read_text(encoding="utf-8")
        self.assertIn("cv.use_id(power_supply.PowerSupply)", source)
        self.assertIn("cg.add(var.set_power_supply(supply))", source)

    def test_requester_is_owned_by_physical_output(self):
        header = CPP_HEADER.read_text(encoding="utf-8")
        self.assertIn("power_supply::PowerSupplyRequester", header)
        self.assertIn("void set_power_supply(power_supply::PowerSupply *supply)", header)
        self.assertEqual(header.count("power_supply::PowerSupplyRequester"), 1)

    def test_release_waits_for_transport_completion(self):
        source = CPP_SOURCE.read_text(encoding="utf-8")
        self.assertIn("power_transmit_in_flight_()", source)
        self.assertIn("group->tx_in_flight_count > 0", source)
        self.assertIn("this->power_supply_requester_.unrequest();", source)

    def test_documentation_contains_complete_example(self):
        docs = DOCS.read_text(encoding="utf-8")
        self.assertIn("power_supply:\n  - id: led_power", docs)
        self.assertIn("power_supply: led_power", docs)
        self.assertIn("Segments share the single power request", docs)


if __name__ == "__main__":
    unittest.main()
