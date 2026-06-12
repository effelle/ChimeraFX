import unittest


def quantize(value):
    return max(0, min(255, int(value * 255.0 + 0.5)))


def to_follower_channels(red, green, blue, white, follower_has_white):
    if follower_has_white:
        neutral = min(red, green, blue)
        return (
            red - neutral,
            green - neutral,
            blue - neutral,
            min(255, white + neutral),
        )
    return (
        min(255, red + white),
        min(255, green + white),
        min(255, blue + white),
        0,
    )


class ColorConversionTests(unittest.TestCase):
    def test_rgb_to_rgb_is_unchanged(self):
        self.assertEqual(
            to_follower_channels(10, 20, 30, 0, False),
            (10, 20, 30, 0),
        )

    def test_rgbw_white_is_folded_into_rgb(self):
        self.assertEqual(
            to_follower_channels(10, 20, 30, 40, False),
            (50, 60, 70, 0),
        )

    def test_rgb_fallback_clamps_channels(self):
        self.assertEqual(
            to_follower_channels(240, 100, 10, 30, False),
            (255, 130, 40, 0),
        )

    def test_neutral_rgb_moves_to_dedicated_white(self):
        self.assertEqual(
            to_follower_channels(80, 80, 80, 0, True),
            (0, 0, 0, 80),
        )

    def test_rgb_remainder_is_preserved_after_white_extraction(self):
        self.assertEqual(
            to_follower_channels(100, 60, 20, 0, True),
            (80, 40, 0, 20),
        )

    def test_existing_white_combines_with_extracted_neutral(self):
        self.assertEqual(
            to_follower_channels(100, 60, 20, 50, True),
            (80, 40, 0, 70),
        )

    def test_combined_white_clamps(self):
        self.assertEqual(
            to_follower_channels(200, 200, 200, 100, True),
            (0, 0, 0, 255),
        )

    def test_quantization_rounds_and_clamps(self):
        self.assertEqual(quantize(0.0), 0)
        self.assertEqual(quantize(1.0), 255)
        self.assertEqual(quantize(0.5), 128)
        self.assertEqual(quantize(-0.1), 0)
        self.assertEqual(quantize(1.1), 255)


if __name__ == "__main__":
    unittest.main()
