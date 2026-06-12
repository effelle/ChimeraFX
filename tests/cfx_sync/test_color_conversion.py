import unittest


def quantize(value):
    return max(0, min(255, int(value * 255.0 + 0.5)))


def scale_channel(channel, color_brightness):
    return (channel * color_brightness + 127) // 255


def normalize_effective_rgb(red, green, blue):
    color_brightness = max(red, green, blue)
    if color_brightness == 0:
        return 255, 255, 255, 0
    return (
        (red * 255 + color_brightness // 2) // color_brightness,
        (green * 255 + color_brightness // 2) // color_brightness,
        (blue * 255 + color_brightness // 2) // color_brightness,
        color_brightness,
    )


def to_follower_channels(
    red,
    green,
    blue,
    white,
    color_brightness,
    source_has_white,
    follower_has_white,
):
    if source_has_white == follower_has_white:
        return red, green, blue, white, color_brightness

    effective_red = scale_channel(red, color_brightness)
    effective_green = scale_channel(green, color_brightness)
    effective_blue = scale_channel(blue, color_brightness)

    if follower_has_white:
        neutral = min(effective_red, effective_green, effective_blue)
        converted = normalize_effective_rgb(
            effective_red - neutral,
            effective_green - neutral,
            effective_blue - neutral,
        )
        return converted[0], converted[1], converted[2], neutral, converted[3]

    converted = normalize_effective_rgb(
        min(255, effective_red + white),
        min(255, effective_green + white),
        min(255, effective_blue + white),
    )
    return converted[0], converted[1], converted[2], 0, converted[3]


class ColorConversionTests(unittest.TestCase):
    def test_rgb_to_rgb_is_unchanged(self):
        self.assertEqual(
            to_follower_channels(10, 20, 30, 0, 128, False, False),
            (10, 20, 30, 0, 128),
        )

    def test_rgbw_white_is_folded_into_rgb(self):
        self.assertEqual(
            to_follower_channels(10, 20, 30, 40, 128, True, False),
            (209, 232, 255, 0, 55),
        )

    def test_rgb_fallback_clamps_channels(self):
        self.assertEqual(
            to_follower_channels(240, 100, 10, 30, 255, True, False),
            (255, 130, 40, 0, 255),
        )

    def test_neutral_rgb_moves_to_dedicated_white(self):
        self.assertEqual(
            to_follower_channels(80, 80, 80, 0, 128, False, True),
            (255, 255, 255, 40, 0),
        )

    def test_rgb_remainder_is_preserved_after_white_extraction(self):
        self.assertEqual(
            to_follower_channels(100, 60, 20, 0, 128, False, True),
            (255, 128, 0, 10, 40),
        )

    def test_rgbw_to_rgbw_preserves_native_channels(self):
        self.assertEqual(
            to_follower_channels(38, 49, 255, 125, 158, True, True),
            (38, 49, 255, 125, 158),
        )

    def test_quantization_rounds_and_clamps(self):
        self.assertEqual(quantize(0.0), 0)
        self.assertEqual(quantize(1.0), 255)
        self.assertEqual(quantize(0.5), 128)
        self.assertEqual(quantize(-0.1), 0)
        self.assertEqual(quantize(1.1), 255)


if __name__ == "__main__":
    unittest.main()
