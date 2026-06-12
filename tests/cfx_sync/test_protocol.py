import hashlib
import hmac
import struct
import unittest


MAGIC = b"CFXS"
VERSION = 1
TYPE_STATE = 1
TYPE_SYNC_REQUEST = 2
HEADER_SIZE = 22
TAG_SIZE = 16
FIELD_POWER = 0x00000001
FIELD_BRIGHTNESS = 0x00000002
FIELD_COLOR = 0x00000004
FIELD_COLOR_BRIGHTNESS = 0x00000008
COLOR_CAP_WHITE = 0x01
FULL_STATE_MASK = (
    FIELD_POWER | FIELD_BRIGHTNESS | FIELD_COLOR | FIELD_COLOR_BRIGHTNESS
)

KEY = bytes(range(32))
GROUP_HASH = 0x12345678
BOOT_ID = 0xA1B2C3D4
SEQUENCE = 0x01020304


def encode(
    packet_type,
    payload=b"",
    *,
    group_hash=GROUP_HASH,
    boot_id=BOOT_ID,
    sequence=SEQUENCE,
    version=VERSION,
):
    header = (
        MAGIC
        + bytes((version, packet_type, 0, HEADER_SIZE))
        + struct.pack(
            ">HIII", len(payload), group_hash, boot_id, sequence
        )
    )
    authenticated = header + payload
    tag = hmac.new(KEY, authenticated, hashlib.sha256).digest()[:TAG_SIZE]
    return authenticated + tag


def full_state_payload(
    power,
    brightness,
    red,
    green,
    blue,
    white,
    has_white,
    color_brightness,
):
    return (
        struct.pack(">I", FULL_STATE_MASK)
        + bytes(
            (
                int(power),
                brightness,
                COLOR_CAP_WHITE if has_white else 0,
                red,
                green,
                blue,
                white,
                color_brightness,
            )
        )
    )


def decode(packet, expected_group_hash=GROUP_HASH):
    if len(packet) < HEADER_SIZE + TAG_SIZE:
        raise ValueError("malformed")
    if packet[:4] != MAGIC:
        raise ValueError("not-cfx")
    if packet[4] != VERSION:
        raise ValueError("version")
    if packet[7] != HEADER_SIZE:
        raise ValueError("header")

    payload_size, group_hash, boot_id, sequence = struct.unpack(
        ">HIII", packet[8:HEADER_SIZE]
    )
    authenticated_size = HEADER_SIZE + payload_size
    if len(packet) != authenticated_size + TAG_SIZE:
        raise ValueError("length")
    if group_hash != expected_group_hash:
        raise ValueError("group")
    expected = hmac.new(
        KEY, packet[:authenticated_size], hashlib.sha256
    ).digest()[:TAG_SIZE]
    if not hmac.compare_digest(expected, packet[authenticated_size:]):
        raise ValueError("auth")

    result = {
        "type": packet[5],
        "group_hash": group_hash,
        "boot_id": boot_id,
        "sequence": sequence,
        "has_power": False,
        "has_brightness": False,
        "has_color": False,
        "has_color_brightness": False,
    }
    if not boot_id or not sequence:
        raise ValueError("replay-fields")
    if packet[5] == TYPE_STATE:
        if payload_size < 4:
            raise ValueError("state-length")
        payload = packet[HEADER_SIZE:authenticated_size]
        result["field_mask"] = struct.unpack(">I", payload[:4])[0]
        offset = 4
        if result["field_mask"] & FIELD_POWER:
            if offset + 1 > payload_size or payload[offset] > 1:
                raise ValueError("power")
            result["has_power"] = True
            result["power"] = bool(payload[offset])
            offset += 1
        if result["field_mask"] & FIELD_BRIGHTNESS:
            if offset + 1 > payload_size:
                raise ValueError("brightness")
            result["has_brightness"] = True
            result["brightness"] = payload[offset]
            offset += 1
        if result["field_mask"] & FIELD_COLOR:
            if offset + 5 > payload_size:
                raise ValueError("color")
            capabilities = payload[offset]
            if capabilities & ~COLOR_CAP_WHITE:
                raise ValueError("color-capabilities")
            result["has_color"] = True
            result["source_has_white"] = bool(
                capabilities & COLOR_CAP_WHITE
            )
            result["red"] = payload[offset + 1]
            result["green"] = payload[offset + 2]
            result["blue"] = payload[offset + 3]
            result["white"] = payload[offset + 4]
            offset += 5
        if result["field_mask"] & FIELD_COLOR_BRIGHTNESS:
            if offset + 1 > payload_size:
                raise ValueError("color-brightness")
            result["has_color_brightness"] = True
            result["color_brightness"] = payload[offset]
            offset += 1
        known_fields = (
            FIELD_POWER
            | FIELD_BRIGHTNESS
            | FIELD_COLOR
            | FIELD_COLOR_BRIGHTNESS
        )
        if not result["field_mask"] & ~known_fields and offset != payload_size:
            raise ValueError("state-length")
    elif packet[5] == TYPE_SYNC_REQUEST:
        if payload_size:
            raise ValueError("request-length")
    else:
        raise ValueError("type")
    return result


class ReplayState:
    def __init__(self):
        self.boot_id = None
        self.sequence = None

    def accept(self, boot_id, sequence):
        if self.boot_id != boot_id:
            self.boot_id = boot_id
            self.sequence = sequence
            return True
        if sequence <= self.sequence:
            return False
        self.sequence = sequence
        return True


class ProtocolTests(unittest.TestCase):
    def test_state_vector_is_stable(self):
        packet = encode(
            TYPE_STATE, struct.pack(">I", FIELD_POWER) + b"\x01"
        )
        self.assertEqual(len(packet), 43)
        self.assertEqual(
            packet.hex(),
            "4346585301010016000512345678a1b2c3d401020304"
            "0000000101"
            "22325e1f713ce65bf5c08f0f222ec028",
        )
        decoded = decode(packet)
        self.assertTrue(decoded["has_power"])
        self.assertTrue(decoded["power"])

    def test_request_vector_is_stable(self):
        packet = encode(TYPE_SYNC_REQUEST)
        self.assertEqual(len(packet), 38)
        self.assertEqual(
            packet.hex(),
            "4346585301020016000012345678a1b2c3d401020304"
            "1a6058ae1b6e1fdf01549973448a58ed",
        )
        self.assertEqual(decode(packet)["type"], TYPE_SYNC_REQUEST)

    def test_full_state_vector_is_stable(self):
        packet = encode(
            TYPE_STATE,
            full_state_payload(True, 128, 10, 20, 30, 40, True, 158),
        )
        self.assertEqual(len(packet), 50)
        self.assertEqual(
            packet.hex(),
            "4346585301010016000c12345678a1b2c3d401020304"
            "0000000f0180010a141e289e"
            "5985e262cdd939013415824feb9eb01b",
        )
        decoded = decode(packet)
        self.assertTrue(decoded["power"])
        self.assertEqual(decoded["brightness"], 128)
        self.assertEqual(decoded["color_brightness"], 158)
        self.assertTrue(decoded["source_has_white"])
        self.assertEqual(
            (
                decoded["red"],
                decoded["green"],
                decoded["blue"],
                decoded["white"],
            ),
            (10, 20, 30, 40),
        )

    def test_power_only_v1_packet_remains_valid(self):
        decoded = decode(
            encode(TYPE_STATE, struct.pack(">I", FIELD_POWER) + b"\x01")
        )
        self.assertTrue(decoded["power"])
        self.assertFalse(decoded["has_brightness"])
        self.assertFalse(decoded["has_color"])
        self.assertFalse(decoded["has_color_brightness"])

    def test_legacy_full_state_without_color_brightness_remains_valid(self):
        legacy_mask = FIELD_POWER | FIELD_BRIGHTNESS | FIELD_COLOR
        payload = (
            struct.pack(">I", legacy_mask)
            + bytes((1, 128, COLOR_CAP_WHITE, 10, 20, 30, 40))
        )
        decoded = decode(encode(TYPE_STATE, payload))
        self.assertTrue(decoded["has_color"])
        self.assertFalse(decoded["has_color_brightness"])

    def test_color_brightness_is_appended_after_legacy_state_fields(self):
        payload = full_state_payload(
            True, 128, 10, 20, 30, 40, True, 158
        )
        self.assertEqual(
            payload[4:11],
            bytes((1, 128, COLOR_CAP_WHITE, 10, 20, 30, 40)),
        )
        self.assertEqual(payload[11], 158)

    def test_bad_authentication_is_rejected(self):
        packet = bytearray(
            encode(TYPE_STATE, struct.pack(">I", FIELD_POWER) + b"\x00")
        )
        packet[-1] ^= 0x01
        with self.assertRaisesRegex(ValueError, "auth"):
            decode(bytes(packet))

    def test_malformed_length_is_rejected(self):
        packet = encode(
            TYPE_STATE, struct.pack(">I", FIELD_POWER) + b"\x01"
        )
        with self.assertRaisesRegex(ValueError, "length"):
            decode(packet[:-1])

    def test_unknown_field_bits_do_not_hide_power(self):
        payload = struct.pack(">I", FIELD_POWER | 0x80000000) + b"\x00\xaa"
        decoded = decode(encode(TYPE_STATE, payload))
        self.assertTrue(decoded["has_power"])
        self.assertFalse(decoded["power"])

    def test_unknown_fields_after_known_values_are_ignored(self):
        payload = (
            full_state_payload(False, 64, 1, 2, 3, 4, False, 128)
            + b"\xaa\xbb"
        )
        payload = (
            struct.pack(">I", FULL_STATE_MASK | 0x80000000) + payload[4:]
        )
        decoded = decode(encode(TYPE_STATE, payload))
        self.assertEqual(decoded["brightness"], 64)
        self.assertEqual(decoded["blue"], 3)

    def test_state_without_power_is_valid_but_not_actionable(self):
        decoded = decode(
            encode(TYPE_STATE, struct.pack(">I", 0x80000000) + b"\xaa")
        )
        self.assertFalse(decoded["has_power"])

    def test_zero_boot_id_and_sequence_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "replay-fields"):
            decode(encode(TYPE_SYNC_REQUEST, boot_id=0))
        with self.assertRaisesRegex(ValueError, "replay-fields"):
            decode(encode(TYPE_SYNC_REQUEST, sequence=0))

    def test_truncated_brightness_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "brightness"):
            decode(
                encode(TYPE_STATE, struct.pack(">I", FIELD_BRIGHTNESS))
            )

    def test_truncated_color_is_rejected(self):
        payload = struct.pack(">I", FIELD_COLOR) + b"\x01\x10\x20"
        with self.assertRaisesRegex(ValueError, "color"):
            decode(encode(TYPE_STATE, payload))

    def test_truncated_color_brightness_is_rejected(self):
        payload = struct.pack(">I", FIELD_COLOR_BRIGHTNESS)
        with self.assertRaisesRegex(ValueError, "color-brightness"):
            decode(encode(TYPE_STATE, payload))

    def test_unknown_color_capability_bits_are_rejected(self):
        payload = (
            struct.pack(">I", FIELD_COLOR) + b"\x02\x00\x00\x00\x00"
        )
        with self.assertRaisesRegex(ValueError, "color-capabilities"):
            decode(encode(TYPE_STATE, payload))

    def test_wrong_group_and_version_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "version"):
            decode(encode(TYPE_SYNC_REQUEST, version=2))
        with self.assertRaisesRegex(ValueError, "group"):
            decode(encode(TYPE_SYNC_REQUEST, group_hash=0x87654321))

    def test_duplicate_and_stale_sequences_are_rejected(self):
        replay = ReplayState()
        self.assertTrue(replay.accept(BOOT_ID, 10))
        self.assertFalse(replay.accept(BOOT_ID, 10))
        self.assertFalse(replay.accept(BOOT_ID, 9))
        self.assertTrue(replay.accept(BOOT_ID, 11))

    def test_new_boot_id_resets_replay_state(self):
        replay = ReplayState()
        self.assertTrue(replay.accept(BOOT_ID, 100))
        self.assertTrue(replay.accept(BOOT_ID + 1, 1))


if __name__ == "__main__":
    unittest.main()
