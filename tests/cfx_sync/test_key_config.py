import unittest

from components.cfx_sync import _derive_key, _validate_key
from esphome import config_validation as cv


class KeyConfigTests(unittest.TestCase):
    def test_accepts_eight_character_key(self):
        self.assertEqual(_validate_key("12345678"), "12345678")

    def test_rejects_short_key(self):
        with self.assertRaisesRegex(cv.Invalid, "at least 8 characters"):
            _validate_key("1234567")

    def test_rejects_key_over_sixty_four_utf8_bytes(self):
        with self.assertRaisesRegex(cv.Invalid, "at most 64 UTF-8 bytes"):
            _validate_key("a" * 65)

    def test_derivation_is_stable_and_domain_separated(self):
        self.assertEqual(
            _derive_key("living-room-2026").hex(),
            "0f7b3c11c75c594fcaf1137b3e1cd127"
            "946027f745a1608e8687987abc64f76a",
        )
        self.assertNotEqual(
            _derive_key("living-room-2026"),
            _derive_key("Living-room-2026"),
        )


if __name__ == "__main__":
    unittest.main()
