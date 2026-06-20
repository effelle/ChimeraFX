import hashlib
import hmac
import struct
import unittest


MAGIC = b"CFXS"
VERSION = 1
TYPE_STATE = 1
TYPE_SYNC_REQUEST = 2
TYPE_HELLO = 3
TYPE_STATE_ACK = 4
TYPE_INPUT_STATE = 5
HEADER_SIZE = 22
TAG_SIZE = 16
FIELD_POWER = 0x00000001
FIELD_BRIGHTNESS = 0x00000002
FIELD_COLOR = 0x00000004
FIELD_COLOR_BRIGHTNESS = 0x00000008
FIELD_EFFECT = 0x00000010
FIELD_CONTROLS = 0x00000020
COLOR_CAP_WHITE = 0x01
EFFECT_NONE = 0
EFFECT_CHIMERAFX = 1
EFFECT_UNSUPPORTED = 2
CONTROL_FORCE_WHITE = 0x0001
CONTROL_INTRO = 0x0002
CONTROL_OUTRO = 0x0004
CONTROL_INOUT_DURATION = 0x0008
CONTROL_SPEED = 0x0010
CONTROL_INTENSITY = 0x0020
CONTROL_MIRROR = 0x0040
CONTROL_PALETTE = 0x0080
ROLE_LEADER = 1
ROLE_FOLLOWER = 2
ROLE_REMOTE = 3
ACK_APPLIED = 0
ACK_IGNORED_UNSUPPORTED = 1
ACK_APPLY_FAILED = 2
CAP_LIGHT_LEADER = 0x0001
CAP_LIGHT_FOLLOWER = 0x0002
CAP_BINARY_REMOTE = 0x0004
MAX_EFFECT_NAME_BYTES = 64
MAX_EFFECT_VALUE_SIZE = 67
MAX_CONTROLS_VALUE_SIZE = 11
MAX_EFFECT_STATE_PACKET_SIZE = 117
MAX_STATE_PACKET_SIZE = 128
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
    key=KEY,
):
    header = (
        MAGIC
        + bytes((version, packet_type, 0, HEADER_SIZE))
        + struct.pack(
            ">HIII", len(payload), group_hash, boot_id, sequence
        )
    )
    authenticated = header + payload
    tag = hmac.new(key, authenticated, hashlib.sha256).digest()[:TAG_SIZE]
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


def effect_value(kind, *, effect_id=0, name=""):
    if kind == EFFECT_NONE:
        return bytes((EFFECT_NONE,))
    if kind not in (EFFECT_CHIMERAFX, EFFECT_UNSUPPORTED):
        raise ValueError("effect-kind")

    encoded_name = name.encode("utf-8")
    if not encoded_name:
        raise ValueError("effect-name-empty")
    if len(encoded_name) > MAX_EFFECT_NAME_BYTES:
        raise ValueError("effect-name-size")

    if kind == EFFECT_CHIMERAFX:
        return bytes((kind, effect_id, len(encoded_name))) + encoded_name
    return bytes((kind, len(encoded_name))) + encoded_name


def controls_value(
    *,
    force_white=None,
    intro=None,
    outro=None,
    inout_duration_ds=None,
    speed=None,
    intensity=None,
    mirror=None,
    palette=None,
):
    control_mask = 0
    values = bytearray()
    if force_white is not None:
        control_mask |= CONTROL_FORCE_WHITE
        values.append(1 if force_white else 0)
    if intro is not None:
        control_mask |= CONTROL_INTRO
        values.append(intro)
    if outro is not None:
        control_mask |= CONTROL_OUTRO
        values.append(outro)
    if inout_duration_ds is not None:
        control_mask |= CONTROL_INOUT_DURATION
        values.extend(struct.pack(">H", inout_duration_ds))
    if speed is not None:
        control_mask |= CONTROL_SPEED
        values.append(speed)
    if intensity is not None:
        control_mask |= CONTROL_INTENSITY
        values.append(intensity)
    if mirror is not None:
        control_mask |= CONTROL_MIRROR
        values.append(1 if mirror else 0)
    if palette is not None:
        control_mask |= CONTROL_PALETTE
        values.append(palette)
    return struct.pack(">H", control_mask) + bytes(values)


def full_state_effect_payload(
    kind,
    *,
    effect_id=0,
    name="",
):
    base = full_state_payload(
        True, 128, 10, 20, 30, 40, True, 158
    )
    return (
        struct.pack(">I", FULL_STATE_MASK | FIELD_EFFECT)
        + base[4:]
        + effect_value(kind, effect_id=effect_id, name=name)
    )


def full_state_effect_controls_payload(
    kind,
    *,
    effect_id=0,
    name="",
    force_white=None,
    intro=None,
    outro=None,
    inout_duration_ds=None,
    speed=None,
    intensity=None,
    mirror=None,
    palette=None,
):
    base = full_state_payload(
        True, 128, 10, 20, 30, 40, True, 158
    )
    return (
        struct.pack(
            ">I", FULL_STATE_MASK | FIELD_EFFECT | FIELD_CONTROLS
        )
        + base[4:]
        + effect_value(kind, effect_id=effect_id, name=name)
        + controls_value(
            force_white=force_white,
            intro=intro,
            outro=outro,
            inout_duration_ds=inout_duration_ds,
            speed=speed,
            intensity=intensity,
            mirror=mirror,
            palette=palette,
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
        "has_effect": False,
        "has_controls": False,
        "has_force_white": False,
        "has_intro": False,
        "has_outro": False,
        "has_inout_duration": False,
        "has_speed": False,
        "has_intensity": False,
        "has_mirror": False,
        "has_palette": False,
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
        if result["field_mask"] & FIELD_EFFECT:
            if offset + 1 > payload_size:
                raise ValueError("effect-kind")
            kind = payload[offset]
            offset += 1
            result["has_effect"] = True
            result["effect_kind"] = kind
            result["effect_id"] = 0
            result["effect_name"] = ""

            if kind == EFFECT_NONE:
                pass
            elif kind == EFFECT_CHIMERAFX:
                if offset + 2 > payload_size:
                    raise ValueError("effect-length")
                result["effect_id"] = payload[offset]
                name_length = payload[offset + 1]
                offset += 2
                if not name_length:
                    raise ValueError("effect-name-empty")
                if name_length > MAX_EFFECT_NAME_BYTES:
                    raise ValueError("effect-name-size")
                if offset + name_length > payload_size:
                    raise ValueError("effect-length")
                try:
                    result["effect_name"] = payload[
                        offset : offset + name_length
                    ].decode("utf-8", errors="strict")
                except UnicodeDecodeError as err:
                    raise ValueError("effect-utf8") from err
                offset += name_length
            elif kind == EFFECT_UNSUPPORTED:
                if offset + 1 > payload_size:
                    raise ValueError("effect-length")
                name_length = payload[offset]
                offset += 1
                if not name_length:
                    raise ValueError("effect-name-empty")
                if name_length > MAX_EFFECT_NAME_BYTES:
                    raise ValueError("effect-name-size")
                if offset + name_length > payload_size:
                    raise ValueError("effect-length")
                try:
                    result["effect_name"] = payload[
                        offset : offset + name_length
                    ].decode("utf-8", errors="strict")
                except UnicodeDecodeError as err:
                    raise ValueError("effect-utf8") from err
                offset += name_length
            else:
                raise ValueError("effect-kind")
        if result["field_mask"] & FIELD_CONTROLS:
            if offset + 2 > payload_size:
                raise ValueError("controls-length")
            control_mask = struct.unpack(">H", payload[offset:offset + 2])[0]
            offset += 2
            result["has_controls"] = True
            result["control_mask"] = control_mask

            known_controls = (
                CONTROL_FORCE_WHITE
                | CONTROL_INTRO
                | CONTROL_OUTRO
                | CONTROL_INOUT_DURATION
                | CONTROL_SPEED
                | CONTROL_INTENSITY
                | CONTROL_MIRROR
                | CONTROL_PALETTE
            )
            if control_mask & ~known_controls:
                raise ValueError("controls-mask")
            if control_mask & CONTROL_FORCE_WHITE:
                if offset + 1 > payload_size:
                    raise ValueError("controls-force-white")
                if payload[offset] > 1:
                    raise ValueError("controls-force-white")
                result["has_force_white"] = True
                result["force_white"] = bool(payload[offset])
                offset += 1
            if control_mask & CONTROL_INTRO:
                if offset + 1 > payload_size:
                    raise ValueError("controls-intro")
                result["has_intro"] = True
                result["intro"] = payload[offset]
                offset += 1
            if control_mask & CONTROL_OUTRO:
                if offset + 1 > payload_size:
                    raise ValueError("controls-outro")
                result["has_outro"] = True
                result["outro"] = payload[offset]
                offset += 1
            if control_mask & CONTROL_INOUT_DURATION:
                if offset + 2 > payload_size:
                    raise ValueError("controls-inout-duration")
                result["has_inout_duration"] = True
                result["inout_duration_ds"] = struct.unpack(
                    ">H", payload[offset:offset + 2]
                )[0]
                offset += 2
            if control_mask & CONTROL_SPEED:
                if offset + 1 > payload_size:
                    raise ValueError("controls-speed")
                result["has_speed"] = True
                result["speed"] = payload[offset]
                offset += 1
            if control_mask & CONTROL_INTENSITY:
                if offset + 1 > payload_size:
                    raise ValueError("controls-intensity")
                result["has_intensity"] = True
                result["intensity"] = payload[offset]
                offset += 1
            if control_mask & CONTROL_MIRROR:
                if offset + 1 > payload_size:
                    raise ValueError("controls-mirror")
                if payload[offset] > 1:
                    raise ValueError("controls-mirror")
                result["has_mirror"] = True
                result["mirror"] = bool(payload[offset])
                offset += 1
            if control_mask & CONTROL_PALETTE:
                if offset + 1 > payload_size:
                    raise ValueError("controls-palette")
                result["has_palette"] = True
                result["palette"] = payload[offset]
                offset += 1
        known_fields = (
            FIELD_POWER
            | FIELD_BRIGHTNESS
            | FIELD_COLOR
            | FIELD_COLOR_BRIGHTNESS
            | FIELD_EFFECT
            | FIELD_CONTROLS
        )
        if not result["field_mask"] & ~known_fields and offset != payload_size:
            raise ValueError("state-length")
    elif packet[5] == TYPE_SYNC_REQUEST:
        if payload_size:
            raise ValueError("request-length")
    elif packet[5] == TYPE_HELLO:
        if payload_size != 3:
            raise ValueError("hello-length")
        payload = packet[HEADER_SIZE:authenticated_size]
        role = payload[0]
        if role not in (ROLE_LEADER, ROLE_FOLLOWER, ROLE_REMOTE):
            raise ValueError("hello-role")
        result["node_role"] = role
        result["capabilities"] = struct.unpack(">H", payload[1:3])[0]
    elif packet[5] == TYPE_STATE_ACK:
        if payload_size != 9:
            raise ValueError("ack-length")
        payload = packet[HEADER_SIZE:authenticated_size]
        acked_boot_id, acked_sequence = struct.unpack(">II", payload[:8])
        ack_result = payload[8]
        if not acked_boot_id or not acked_sequence:
            raise ValueError("ack-replay-fields")
        if ack_result not in (
            ACK_APPLIED,
            ACK_IGNORED_UNSUPPORTED,
            ACK_APPLY_FAILED,
        ):
            raise ValueError("ack-result")
        result["acked_boot_id"] = acked_boot_id
        result["acked_sequence"] = acked_sequence
        result["ack_result"] = ack_result
    elif packet[5] == TYPE_INPUT_STATE:
        if payload_size != 1:
            raise ValueError("input-length")
        payload = packet[HEADER_SIZE:authenticated_size]
        if payload[0] > 1:
            raise ValueError("input-state")
        result["input_pressed"] = bool(payload[0])
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
    def test_effect_protocol_size_constants_are_stable(self):
        self.assertEqual(MAX_EFFECT_VALUE_SIZE, 67)
        self.assertEqual(
            HEADER_SIZE
            + len(full_state_payload(
                True, 128, 10, 20, 30, 40, True, 158
            ))
            + MAX_EFFECT_VALUE_SIZE
            + TAG_SIZE,
            MAX_EFFECT_STATE_PACKET_SIZE,
        )
        self.assertEqual(
            MAX_EFFECT_STATE_PACKET_SIZE + MAX_CONTROLS_VALUE_SIZE,
            MAX_STATE_PACKET_SIZE,
        )

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

    def test_input_state_vector_is_stable(self):
        pressed = encode(TYPE_INPUT_STATE, b"\x01")
        self.assertEqual(len(pressed), 39)
        decoded = decode(pressed)
        self.assertEqual(decoded["type"], TYPE_INPUT_STATE)
        self.assertTrue(decoded["input_pressed"])

        released = decode(encode(TYPE_INPUT_STATE, b"\x00"))
        self.assertFalse(released["input_pressed"])

    def test_malformed_input_state_is_rejected(self):
        for payload in (b"", b"\x00\x00", b"\x02"):
            with self.subTest(payload=payload):
                with self.assertRaisesRegex(ValueError, "input"):
                    decode(encode(TYPE_INPUT_STATE, payload))

    def test_hello_round_trips_role_and_capabilities(self):
        payload = bytes((ROLE_LEADER,)) + struct.pack(
            ">H", CAP_LIGHT_LEADER | CAP_BINARY_REMOTE
        )
        packet = encode(TYPE_HELLO, payload)
        self.assertEqual(len(packet), 41)
        decoded = decode(packet)
        self.assertEqual(decoded["type"], TYPE_HELLO)
        self.assertEqual(decoded["node_role"], ROLE_LEADER)
        self.assertEqual(
            decoded["capabilities"], CAP_LIGHT_LEADER | CAP_BINARY_REMOTE
        )

    def test_state_ack_round_trips_acked_state_and_result(self):
        payload = struct.pack(
            ">IIB", 0x11223344, 0x55667788, ACK_IGNORED_UNSUPPORTED
        )
        packet = encode(TYPE_STATE_ACK, payload)
        self.assertEqual(len(packet), 47)
        decoded = decode(packet)
        self.assertEqual(decoded["type"], TYPE_STATE_ACK)
        self.assertEqual(decoded["acked_boot_id"], 0x11223344)
        self.assertEqual(decoded["acked_sequence"], 0x55667788)
        self.assertEqual(decoded["ack_result"], ACK_IGNORED_UNSUPPORTED)

    def test_hello_wrong_key_is_rejected(self):
        payload = bytes((ROLE_FOLLOWER,)) + struct.pack(
            ">H", CAP_LIGHT_FOLLOWER
        )
        with self.assertRaisesRegex(ValueError, "auth"):
            decode(encode(TYPE_HELLO, payload, key=bytes(reversed(KEY))))

    def test_hello_wrong_group_is_rejected(self):
        payload = bytes((ROLE_REMOTE,)) + struct.pack(
            ">H", CAP_BINARY_REMOTE
        )
        with self.assertRaisesRegex(ValueError, "group"):
            decode(encode(TYPE_HELLO, payload, group_hash=0x87654321))

    def test_malformed_hello_payload_size_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "hello-length"):
            decode(encode(TYPE_HELLO, b"\x01\x00"))

    def test_malformed_hello_role_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "hello-role"):
            decode(encode(TYPE_HELLO, b"\x04\x00\x01"))

    def test_malformed_ack_payload_size_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "ack-length"):
            decode(encode(TYPE_STATE_ACK, b"\x11\x22\x33\x44"))

    def test_malformed_ack_result_is_rejected(self):
        payload = struct.pack(">IIB", 0x11223344, 0x55667788, 3)
        with self.assertRaisesRegex(ValueError, "ack-result"):
            decode(encode(TYPE_STATE_ACK, payload))

    def test_zero_acked_boot_id_and_sequence_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "ack-replay-fields"):
            decode(encode(TYPE_STATE_ACK, struct.pack(">IIB", 0, 1, 0)))
        with self.assertRaisesRegex(ValueError, "ack-replay-fields"):
            decode(encode(TYPE_STATE_ACK, struct.pack(">IIB", 1, 0, 0)))

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

    def test_chimerafx_effect_vector_is_stable(self):
        packet = encode(
            TYPE_STATE,
            full_state_effect_payload(
                EFFECT_CHIMERAFX, effect_id=3, name="Wipe"
            ),
        )
        self.assertEqual(len(packet), 57)
        self.assertEqual(
            packet.hex(),
            "4346585301010016001312345678a1b2c3d401020304"
            "0000001f0180010a141e289e01030457697065"
            "a95b6db57839e0d2c9d85172c4b9d52b",
        )
        decoded = decode(packet)
        self.assertTrue(decoded["has_effect"])
        self.assertEqual(decoded["effect_kind"], EFFECT_CHIMERAFX)
        self.assertEqual(decoded["effect_id"], 3)
        self.assertEqual(decoded["effect_name"], "Wipe")

    def test_controls_vector_is_stable(self):
        packet = encode(
            TYPE_STATE,
            full_state_effect_controls_payload(
                EFFECT_CHIMERAFX,
                effect_id=3,
                name="Wipe",
                force_white=True,
                intro=2,
                outro=4,
                inout_duration_ds=25,
            ),
        )
        self.assertEqual(len(packet), 64)
        self.assertEqual(
            packet.hex(),
            "4346585301010016001a12345678a1b2c3d401020304"
            "0000003f0180010a141e289e01030457697065000f0102040019"
            "b55681c6c282db952f89fd1acfea76fe",
        )
        decoded = decode(packet)
        self.assertTrue(decoded["has_controls"])
        self.assertTrue(decoded["has_force_white"])
        self.assertTrue(decoded["force_white"])
        self.assertEqual(decoded["intro"], 2)
        self.assertEqual(decoded["outro"], 4)
        self.assertEqual(decoded["inout_duration_ds"], 25)

    def test_live_effect_controls_vector_is_stable(self):
        packet = encode(
            TYPE_STATE,
            full_state_effect_controls_payload(
                EFFECT_CHIMERAFX,
                effect_id=3,
                name="Wipe",
                force_white=True,
                intro=2,
                outro=4,
                inout_duration_ds=25,
                speed=33,
                intensity=144,
                mirror=True,
                palette=7,
            ),
        )
        self.assertEqual(len(packet), 68)
        self.assertEqual(
            packet.hex(),
            "4346585301010016001e12345678a1b2c3d401020304"
            "0000003f0180010a141e289e0103045769706500ff010204001921900107"
            "45d7f4a6405a7709a6132b585f5f0b67",
        )
        decoded = decode(packet)
        self.assertEqual(decoded["speed"], 33)
        self.assertEqual(decoded["intensity"], 144)
        self.assertTrue(decoded["mirror"])
        self.assertEqual(decoded["palette"], 7)

    def test_none_effect_vector_is_stable(self):
        packet = encode(
            TYPE_STATE,
            full_state_effect_payload(
                EFFECT_NONE, effect_id=255, name="ignored"
            ),
        )
        self.assertEqual(len(packet), 51)
        self.assertEqual(
            packet.hex(),
            "4346585301010016000d12345678a1b2c3d401020304"
            "0000001f0180010a141e289e00"
            "41949784e575f93831d22833fb45e842",
        )
        decoded = decode(packet)
        self.assertEqual(decoded["effect_kind"], EFFECT_NONE)
        self.assertEqual(decoded["effect_id"], 0)
        self.assertEqual(decoded["effect_name"], "")

    def test_effect_round_trips_all_wire_kinds(self):
        cases = (
            (EFFECT_NONE, 0, "", 0, ""),
            (
                EFFECT_CHIMERAFX,
                17,
                "Aurora \u2728",
                17,
                "Aurora \u2728",
            ),
            (EFFECT_UNSUPPORTED, 99, "Native Glow", 0, "Native Glow"),
        )
        for kind, effect_id, name, expected_id, expected_name in cases:
            with self.subTest(kind=kind):
                decoded = decode(
                    encode(
                        TYPE_STATE,
                        full_state_effect_payload(
                            kind, effect_id=effect_id, name=name
                        ),
                    )
                )
                self.assertTrue(decoded["has_effect"])
                self.assertEqual(decoded["effect_kind"], kind)
                self.assertEqual(decoded["effect_id"], expected_id)
                self.assertEqual(decoded["effect_name"], expected_name)

    def test_effect_names_accept_exactly_64_utf8_bytes(self):
        for kind in (EFFECT_CHIMERAFX, EFFECT_UNSUPPORTED):
            with self.subTest(kind=kind):
                packet = encode(
                    TYPE_STATE,
                    full_state_effect_payload(
                        kind, effect_id=1, name="a" * 64
                    ),
                )
                decoded = decode(
                    packet
                )
                self.assertEqual(decoded["effect_name"], "a" * 64)
                expected_size = (
                    MAX_EFFECT_STATE_PACKET_SIZE
                    if kind == EFFECT_CHIMERAFX
                    else MAX_EFFECT_STATE_PACKET_SIZE - 1
                )
                self.assertEqual(len(packet), expected_size)

    def test_power_only_v1_packet_remains_valid(self):
        decoded = decode(
            encode(TYPE_STATE, struct.pack(">I", FIELD_POWER) + b"\x01")
        )
        self.assertTrue(decoded["power"])
        self.assertFalse(decoded["has_brightness"])
        self.assertFalse(decoded["has_color"])
        self.assertFalse(decoded["has_color_brightness"])
        self.assertFalse(decoded["has_effect"])

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

    def test_unknown_fields_after_effect_value_are_ignored(self):
        payload = (
            full_state_effect_payload(
                EFFECT_UNSUPPORTED, name="External"
            )
            + b"\xaa\xbb"
        )
        payload = (
            struct.pack(
                ">I",
                FULL_STATE_MASK | FIELD_EFFECT | 0x80000000,
            )
            + payload[4:]
        )
        decoded = decode(encode(TYPE_STATE, payload))
        self.assertEqual(decoded["effect_name"], "External")

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

    def test_truncated_effect_values_are_rejected(self):
        malformed_values = (
            b"",
            bytes((EFFECT_CHIMERAFX,)),
            bytes((EFFECT_CHIMERAFX, 3)),
            bytes((EFFECT_CHIMERAFX, 3, 4)) + b"Wip",
            bytes((EFFECT_UNSUPPORTED,)),
            bytes((EFFECT_UNSUPPORTED, 4)) + b"Wip",
        )
        for value in malformed_values:
            with self.subTest(value=value):
                payload = struct.pack(">I", FIELD_EFFECT) + value
                with self.assertRaisesRegex(
                    ValueError, "effect-(kind|length)"
                ):
                    decode(encode(TYPE_STATE, payload))

    def test_bad_effect_kind_is_rejected(self):
        payload = struct.pack(">I", FIELD_EFFECT) + b"\x03"
        with self.assertRaisesRegex(ValueError, "effect-kind"):
            decode(encode(TYPE_STATE, payload))

    def test_invalid_effect_utf8_is_rejected(self):
        invalid_names = (
            b"\xc0\x80",
            b"\xed\xa0\x80",
            b"\xf4\x90\x80\x80",
            b"\x80",
        )
        for name in invalid_names:
            with self.subTest(name=name):
                value = bytes(
                    (EFFECT_UNSUPPORTED, len(name))
                ) + name
                payload = struct.pack(">I", FIELD_EFFECT) + value
                with self.assertRaisesRegex(ValueError, "effect-utf8"):
                    decode(encode(TYPE_STATE, payload))

    def test_effect_name_size_and_empty_names_are_rejected(self):
        malformed_values = (
            bytes((EFFECT_CHIMERAFX, 3, 0)),
            bytes((EFFECT_UNSUPPORTED, 0)),
            bytes((EFFECT_CHIMERAFX, 3, 65)) + b"a" * 65,
            bytes((EFFECT_UNSUPPORTED, 65)) + b"a" * 65,
        )
        for value in malformed_values:
            with self.subTest(value=value[:3]):
                payload = struct.pack(">I", FIELD_EFFECT) + value
                with self.assertRaisesRegex(
                    ValueError, "effect-name-(empty|size)"
                ):
                    decode(encode(TYPE_STATE, payload))

    def test_effect_encode_rejects_invalid_values(self):
        invalid = (
            (3, 0, ""),
            (EFFECT_CHIMERAFX, 1, ""),
            (EFFECT_UNSUPPORTED, 0, ""),
            (EFFECT_CHIMERAFX, 1, "a" * 65),
            (EFFECT_UNSUPPORTED, 0, "a" * 65),
        )
        for kind, effect_id, name in invalid:
            with self.subTest(kind=kind, name_length=len(name)):
                with self.assertRaises(ValueError):
                    effect_value(
                        kind, effect_id=effect_id, name=name
                    )

    def test_known_effect_value_rejects_trailing_bytes(self):
        payload = (
            struct.pack(">I", FIELD_EFFECT)
            + effect_value(EFFECT_NONE)
            + b"\xaa"
        )
        with self.assertRaisesRegex(ValueError, "state-length"):
            decode(encode(TYPE_STATE, payload))

    def test_truncated_controls_values_are_rejected(self):
        malformed_values = (
            b"",
            b"\x00",
            struct.pack(">H", CONTROL_FORCE_WHITE),
            struct.pack(">H", CONTROL_INOUT_DURATION) + b"\x00",
        )
        for value in malformed_values:
            with self.subTest(value=value):
                payload = struct.pack(">I", FIELD_CONTROLS) + value
                with self.assertRaisesRegex(ValueError, "controls"):
                    decode(encode(TYPE_STATE, payload))

    def test_controls_reject_invalid_boolean_values(self):
        payload = (
            struct.pack(">I", FIELD_CONTROLS)
            + struct.pack(">H", CONTROL_FORCE_WHITE)
            + b"\x02"
        )
        with self.assertRaisesRegex(ValueError, "controls-force-white"):
            decode(encode(TYPE_STATE, payload))

    def test_controls_reject_invalid_mirror_values(self):
        payload = (
            struct.pack(">I", FIELD_CONTROLS)
            + struct.pack(">H", CONTROL_MIRROR)
            + b"\x02"
        )
        with self.assertRaisesRegex(ValueError, "controls-mirror"):
            decode(encode(TYPE_STATE, payload))

    def test_legacy_packet_without_controls_remains_valid(self):
        decoded = decode(
            encode(
                TYPE_STATE,
                full_state_effect_payload(
                    EFFECT_CHIMERAFX, effect_id=3, name="Wipe"
                ),
            )
        )
        self.assertFalse(decoded["has_controls"])

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

    def test_replay_state_is_independent_per_peer(self):
        peers = {
            b"\x01\x02\x03\x04\x05\x06": ReplayState(),
            b"\x0a\x0b\x0c\x0d\x0e\x0f": ReplayState(),
        }
        self.assertTrue(
            peers[b"\x01\x02\x03\x04\x05\x06"].accept(BOOT_ID, 10)
        )
        self.assertTrue(
            peers[b"\x0a\x0b\x0c\x0d\x0e\x0f"].accept(BOOT_ID, 10)
        )
        self.assertFalse(
            peers[b"\x01\x02\x03\x04\x05\x06"].accept(BOOT_ID, 10)
        )
        self.assertFalse(
            peers[b"\x0a\x0b\x0c\x0d\x0e\x0f"].accept(BOOT_ID, 9)
        )


if __name__ == "__main__":
    unittest.main()
