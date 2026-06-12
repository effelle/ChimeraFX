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
    }
    if not boot_id or not sequence:
        raise ValueError("replay-fields")
    if packet[5] == TYPE_STATE:
        if payload_size < 4:
            raise ValueError("state-length")
        payload = packet[HEADER_SIZE:authenticated_size]
        result["field_mask"] = struct.unpack(">I", payload[:4])[0]
        if result["field_mask"] & FIELD_POWER:
            if payload_size < 5 or payload[4] > 1:
                raise ValueError("power")
            result["has_power"] = True
            result["power"] = bool(payload[4])
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
