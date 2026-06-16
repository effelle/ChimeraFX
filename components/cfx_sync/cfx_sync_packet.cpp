#include "cfx_sync_packet.h"

#ifdef USE_ESP32

#include <cstring>

namespace esphome {
namespace cfx_sync {

static constexpr uint8_t CFX_SYNC_MAGIC[4] = {'C', 'F', 'X', 'S'};

void CFXSyncPacketCodec::append_u16_(std::vector<uint8_t> &output,
                                     uint16_t value) {
  output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  output.push_back(static_cast<uint8_t>(value & 0xFF));
}

void CFXSyncPacketCodec::append_u32_(std::vector<uint8_t> &output,
                                     uint32_t value) {
  output.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
  output.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
  output.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
  output.push_back(static_cast<uint8_t>(value & 0xFF));
}

uint16_t CFXSyncPacketCodec::read_u16_(const uint8_t *data) {
  return (static_cast<uint16_t>(data[0]) << 8) |
         static_cast<uint16_t>(data[1]);
}

uint32_t CFXSyncPacketCodec::read_u32_(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

void CFXSyncPacketCodec::calculate_tag_(
    const uint8_t *data, size_t size, const std::array<uint8_t, 32> &key,
    uint8_t *tag) {
  hmac_sha256::HmacSHA256 hmac;
  hmac.init(key.data(), key.size());
  hmac.add(data, size);
  hmac.calculate();

  uint8_t digest[32];
  hmac.get_bytes(digest);
  memcpy(tag, digest, AUTH_TAG_SIZE);
}

bool CFXSyncPacketCodec::tags_equal_(const uint8_t *left,
                                     const uint8_t *right, size_t size) {
  uint8_t difference = 0;
  for (size_t i = 0; i < size; i++) {
    difference |= left[i] ^ right[i];
  }
  return difference == 0;
}

bool CFXSyncPacketCodec::is_valid_utf8_(const uint8_t *data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    const uint8_t first = data[offset++];
    if (first <= 0x7F) {
      continue;
    }

    if (first >= 0xC2 && first <= 0xDF) {
      if (offset >= size || (data[offset] & 0xC0) != 0x80) {
        return false;
      }
      offset++;
      continue;
    }

    if (first >= 0xE0 && first <= 0xEF) {
      if (offset + 1 >= size) {
        return false;
      }
      const uint8_t second = data[offset];
      const uint8_t third = data[offset + 1];
      if ((third & 0xC0) != 0x80 ||
          (first == 0xE0 && (second < 0xA0 || second > 0xBF)) ||
          (first == 0xED && (second < 0x80 || second > 0x9F)) ||
          (first != 0xE0 && first != 0xED &&
           (second & 0xC0) != 0x80)) {
        return false;
      }
      offset += 2;
      continue;
    }

    if (first >= 0xF0 && first <= 0xF4) {
      if (offset + 2 >= size) {
        return false;
      }
      const uint8_t second = data[offset];
      if ((data[offset + 1] & 0xC0) != 0x80 ||
          (data[offset + 2] & 0xC0) != 0x80 ||
          (first == 0xF0 && (second < 0x90 || second > 0xBF)) ||
          (first == 0xF4 && (second < 0x80 || second > 0x8F)) ||
          (first != 0xF0 && first != 0xF4 &&
           (second & 0xC0) != 0x80)) {
        return false;
      }
      offset += 3;
      continue;
    }

    return false;
  }
  return true;
}

bool CFXSyncPacketCodec::encode_(
    CFXSyncPacketType type, uint32_t group_hash, uint32_t boot_id,
    uint32_t sequence, const uint8_t *payload, size_t payload_size,
    const std::array<uint8_t, 32> &key, std::vector<uint8_t> &output) {
  if (payload_size > UINT16_MAX) {
    return false;
  }

  output.clear();
  output.reserve(HEADER_SIZE + payload_size + AUTH_TAG_SIZE);
  output.insert(output.end(), CFX_SYNC_MAGIC,
                CFX_SYNC_MAGIC + sizeof(CFX_SYNC_MAGIC));
  output.push_back(VERSION);
  output.push_back(static_cast<uint8_t>(type));
  output.push_back(0);  // Flags reserved for future protocol revisions.
  output.push_back(static_cast<uint8_t>(HEADER_SIZE));
  append_u16_(output, static_cast<uint16_t>(payload_size));
  append_u32_(output, group_hash);
  append_u32_(output, boot_id);
  append_u32_(output, sequence);

  if (payload != nullptr && payload_size != 0) {
    output.insert(output.end(), payload, payload + payload_size);
  }

  uint8_t tag[AUTH_TAG_SIZE];
  calculate_tag_(output.data(), output.size(), key, tag);
  output.insert(output.end(), tag, tag + AUTH_TAG_SIZE);
  return true;
}

bool CFXSyncPacketCodec::encode_state(
    uint32_t group_hash, uint32_t boot_id, uint32_t sequence, bool power,
    uint8_t brightness, uint8_t color_brightness, uint8_t red, uint8_t green,
    uint8_t blue, uint8_t white, bool has_white,
    const std::array<uint8_t, 32> &key, std::vector<uint8_t> &output) {
  return encode_state(group_hash, boot_id, sequence, power, brightness,
                      color_brightness, red, green, blue, white, has_white,
                      false, {}, key, output);
}

bool CFXSyncPacketCodec::encode_state(
    uint32_t group_hash, uint32_t boot_id, uint32_t sequence, bool power,
    uint8_t brightness, uint8_t color_brightness, uint8_t red, uint8_t green,
    uint8_t blue, uint8_t white, bool has_white, bool has_effect,
    const CFXSyncEffectState &effect, const std::array<uint8_t, 32> &key,
    std::vector<uint8_t> &output) {
  return encode_state(group_hash, boot_id, sequence, power, brightness,
                      color_brightness, red, green, blue, white, has_white,
                      has_effect, effect, false, {}, key, output);
}

bool CFXSyncPacketCodec::encode_state(
    uint32_t group_hash, uint32_t boot_id, uint32_t sequence, bool power,
    uint8_t brightness, uint8_t color_brightness, uint8_t red, uint8_t green,
    uint8_t blue, uint8_t white, bool has_white, bool has_effect,
    const CFXSyncEffectState &effect, bool has_controls,
    const CFXSyncControlState &controls, const std::array<uint8_t, 32> &key,
    std::vector<uint8_t> &output) {
  if (has_effect) {
    const size_t name_size = effect.name.size();
    switch (effect.kind) {
      case CFXSyncEffectKind::NONE:
        break;
      case CFXSyncEffectKind::CHIMERAFX:
      case CFXSyncEffectKind::UNSUPPORTED:
        if (name_size == 0 || name_size > MAX_EFFECT_NAME_BYTES ||
            !is_valid_utf8_(
                reinterpret_cast<const uint8_t *>(effect.name.data()),
                name_size)) {
          return false;
        }
        break;
      default:
        return false;
    }
  }

  std::vector<uint8_t> payload;
  payload.reserve(MAX_STATE_PAYLOAD_SIZE);
  uint32_t field_mask = FULL_STATE_MASK;
  if (has_effect) {
    field_mask |= FIELD_EFFECT;
  }
  if (has_controls) {
    field_mask |= FIELD_CONTROLS;
  }
  append_u32_(payload, field_mask);
  payload.push_back(power ? 1 : 0);
  payload.push_back(brightness);
  payload.push_back(has_white ? COLOR_CAP_WHITE : 0);
  payload.push_back(red);
  payload.push_back(green);
  payload.push_back(blue);
  payload.push_back(white);
  payload.push_back(color_brightness);

  if (has_effect) {
    payload.push_back(static_cast<uint8_t>(effect.kind));
    switch (effect.kind) {
      case CFXSyncEffectKind::NONE:
        break;
      case CFXSyncEffectKind::CHIMERAFX:
        payload.push_back(effect.effect_id);
        payload.push_back(static_cast<uint8_t>(effect.name.size()));
        payload.insert(payload.end(), effect.name.begin(), effect.name.end());
        break;
      case CFXSyncEffectKind::UNSUPPORTED:
        payload.push_back(static_cast<uint8_t>(effect.name.size()));
        payload.insert(payload.end(), effect.name.begin(), effect.name.end());
        break;
      default:
        return false;
    }
  }

  if (has_controls) {
    uint16_t control_mask = 0;
    if (controls.has_force_white) {
      control_mask |= CONTROL_FORCE_WHITE;
    }
    if (controls.has_intro) {
      control_mask |= CONTROL_INTRO;
    }
    if (controls.has_outro) {
      control_mask |= CONTROL_OUTRO;
    }
    if (controls.has_inout_duration) {
      control_mask |= CONTROL_INOUT_DURATION;
    }
    if (controls.has_speed) {
      control_mask |= CONTROL_SPEED;
    }
    if (controls.has_intensity) {
      control_mask |= CONTROL_INTENSITY;
    }
    if (controls.has_mirror) {
      control_mask |= CONTROL_MIRROR;
    }
    if (controls.has_palette) {
      control_mask |= CONTROL_PALETTE;
    }
    append_u16_(payload, control_mask);
    if (controls.has_force_white) {
      payload.push_back(controls.force_white ? 1 : 0);
    }
    if (controls.has_intro) {
      payload.push_back(controls.intro);
    }
    if (controls.has_outro) {
      payload.push_back(controls.outro);
    }
    if (controls.has_inout_duration) {
      append_u16_(payload, controls.inout_duration_deciseconds);
    }
    if (controls.has_speed) {
      payload.push_back(controls.speed);
    }
    if (controls.has_intensity) {
      payload.push_back(controls.intensity);
    }
    if (controls.has_mirror) {
      payload.push_back(controls.mirror ? 1 : 0);
    }
    if (controls.has_palette) {
      payload.push_back(controls.palette);
    }
  }

  return encode_(CFXSyncPacketType::STATE, group_hash, boot_id, sequence,
                 payload.data(), payload.size(), key, output);
}

bool CFXSyncPacketCodec::encode_sync_request(
    uint32_t group_hash, uint32_t boot_id, uint32_t sequence,
    const std::array<uint8_t, 32> &key, std::vector<uint8_t> &output) {
  return encode_(CFXSyncPacketType::SYNC_REQUEST, group_hash, boot_id,
                 sequence, nullptr, 0, key, output);
}

bool CFXSyncPacketCodec::encode_hello(
    uint32_t group_hash, uint32_t boot_id, uint32_t sequence,
    CFXSyncNodeRole role, uint16_t capabilities,
    const std::array<uint8_t, 32> &key, std::vector<uint8_t> &output) {
  switch (role) {
    case CFXSyncNodeRole::LEADER:
    case CFXSyncNodeRole::FOLLOWER:
    case CFXSyncNodeRole::REMOTE:
      break;
    default:
      return false;
  }

  std::vector<uint8_t> payload;
  payload.reserve(HELLO_PAYLOAD_SIZE);
  payload.push_back(static_cast<uint8_t>(role));
  append_u16_(payload, capabilities);
  return encode_(CFXSyncPacketType::HELLO, group_hash, boot_id, sequence,
                 payload.data(), payload.size(), key, output);
}

bool CFXSyncPacketCodec::encode_state_ack(
    uint32_t group_hash, uint32_t boot_id, uint32_t sequence,
    uint32_t acked_boot_id, uint32_t acked_sequence,
    CFXSyncAckResult result, const std::array<uint8_t, 32> &key,
    std::vector<uint8_t> &output) {
  if (acked_boot_id == 0 || acked_sequence == 0) {
    return false;
  }

  switch (result) {
    case CFXSyncAckResult::APPLIED:
    case CFXSyncAckResult::IGNORED_UNSUPPORTED:
    case CFXSyncAckResult::APPLY_FAILED:
      break;
    default:
      return false;
  }

  std::vector<uint8_t> payload;
  payload.reserve(STATE_ACK_PAYLOAD_SIZE);
  append_u32_(payload, acked_boot_id);
  append_u32_(payload, acked_sequence);
  payload.push_back(static_cast<uint8_t>(result));
  return encode_(CFXSyncPacketType::STATE_ACK, group_hash, boot_id, sequence,
                 payload.data(), payload.size(), key, output);
}

CFXSyncDecodeResult CFXSyncPacketCodec::decode(
    const uint8_t *data, size_t size, uint32_t expected_group_hash,
    const std::array<uint8_t, 32> &key, CFXSyncPacket &packet) {
  if (data == nullptr || size < sizeof(CFX_SYNC_MAGIC)) {
    return CFXSyncDecodeResult::MALFORMED;
  }
  if (memcmp(data, CFX_SYNC_MAGIC, sizeof(CFX_SYNC_MAGIC)) != 0) {
    return CFXSyncDecodeResult::NOT_CFX;
  }
  if (size < HEADER_SIZE + AUTH_TAG_SIZE) {
    return CFXSyncDecodeResult::MALFORMED;
  }
  if (data[4] != VERSION) {
    return CFXSyncDecodeResult::UNSUPPORTED_VERSION;
  }
  if (data[7] != HEADER_SIZE) {
    return CFXSyncDecodeResult::MALFORMED;
  }

  const uint16_t payload_size = read_u16_(data + 8);
  const size_t authenticated_size = HEADER_SIZE + payload_size;
  if (authenticated_size + AUTH_TAG_SIZE != size) {
    return CFXSyncDecodeResult::MALFORMED;
  }

  const uint32_t group_hash = read_u32_(data + 10);
  if (group_hash != expected_group_hash) {
    return CFXSyncDecodeResult::WRONG_GROUP;
  }

  uint8_t expected_tag[AUTH_TAG_SIZE];
  calculate_tag_(data, authenticated_size, key, expected_tag);
  if (!tags_equal_(expected_tag, data + authenticated_size, AUTH_TAG_SIZE)) {
    return CFXSyncDecodeResult::BAD_AUTH;
  }

  const uint8_t raw_type = data[5];
  if (raw_type != static_cast<uint8_t>(CFXSyncPacketType::STATE) &&
      raw_type !=
          static_cast<uint8_t>(CFXSyncPacketType::SYNC_REQUEST) &&
      raw_type != static_cast<uint8_t>(CFXSyncPacketType::HELLO) &&
      raw_type != static_cast<uint8_t>(CFXSyncPacketType::STATE_ACK)) {
    return CFXSyncDecodeResult::UNSUPPORTED_TYPE;
  }

  packet = {};
  packet.type = static_cast<CFXSyncPacketType>(raw_type);
  packet.flags = data[6];
  packet.boot_id = read_u32_(data + 14);
  packet.sequence = read_u32_(data + 18);
  if (packet.boot_id == 0 || packet.sequence == 0) {
    return CFXSyncDecodeResult::MALFORMED;
  }

  if (packet.type == CFXSyncPacketType::SYNC_REQUEST) {
    return payload_size == 0 ? CFXSyncDecodeResult::OK
                             : CFXSyncDecodeResult::MALFORMED;
  }

  const uint8_t *payload = data + HEADER_SIZE;
  if (packet.type == CFXSyncPacketType::HELLO) {
    if (payload_size != HELLO_PAYLOAD_SIZE) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    const uint8_t raw_role = payload[0];
    switch (static_cast<CFXSyncNodeRole>(raw_role)) {
      case CFXSyncNodeRole::LEADER:
      case CFXSyncNodeRole::FOLLOWER:
      case CFXSyncNodeRole::REMOTE:
        packet.node_role = static_cast<CFXSyncNodeRole>(raw_role);
        break;
      default:
        return CFXSyncDecodeResult::MALFORMED;
    }
    packet.capabilities = read_u16_(payload + 1);
    return CFXSyncDecodeResult::OK;
  }

  if (packet.type == CFXSyncPacketType::STATE_ACK) {
    if (payload_size != STATE_ACK_PAYLOAD_SIZE) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    packet.acked_boot_id = read_u32_(payload);
    packet.acked_sequence = read_u32_(payload + 4);
    if (packet.acked_boot_id == 0 || packet.acked_sequence == 0) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    const uint8_t raw_result = payload[8];
    switch (static_cast<CFXSyncAckResult>(raw_result)) {
      case CFXSyncAckResult::APPLIED:
      case CFXSyncAckResult::IGNORED_UNSUPPORTED:
      case CFXSyncAckResult::APPLY_FAILED:
        packet.ack_result = static_cast<CFXSyncAckResult>(raw_result);
        break;
      default:
        return CFXSyncDecodeResult::MALFORMED;
    }
    return CFXSyncDecodeResult::OK;
  }

  if (payload_size < sizeof(uint32_t)) {
    return CFXSyncDecodeResult::MALFORMED;
  }
  packet.field_mask = read_u32_(payload);
  size_t offset = sizeof(uint32_t);

  if ((packet.field_mask & FIELD_POWER) != 0) {
    if (offset + 1 > payload_size || payload[offset] > 1) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    packet.has_power = true;
    packet.power = payload[offset++] != 0;
  }

  if ((packet.field_mask & FIELD_BRIGHTNESS) != 0) {
    if (offset + 1 > payload_size) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    packet.has_brightness = true;
    packet.brightness = payload[offset++];
  }

  if ((packet.field_mask & FIELD_COLOR) != 0) {
    if (offset + 5 > payload_size) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    const uint8_t capabilities = payload[offset++];
    if ((capabilities & ~COLOR_CAP_WHITE) != 0) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    packet.has_color = true;
    packet.source_has_white = (capabilities & COLOR_CAP_WHITE) != 0;
    packet.red = payload[offset++];
    packet.green = payload[offset++];
    packet.blue = payload[offset++];
    packet.white = payload[offset++];
  }

  if ((packet.field_mask & FIELD_COLOR_BRIGHTNESS) != 0) {
    if (offset + 1 > payload_size) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    packet.has_color_brightness = true;
    packet.color_brightness = payload[offset++];
  }

  if ((packet.field_mask & FIELD_EFFECT) != 0) {
    if (offset + 1 > payload_size) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    packet.has_effect = true;
    packet.effect.kind =
        static_cast<CFXSyncEffectKind>(payload[offset++]);

    size_t name_size = 0;
    switch (packet.effect.kind) {
      case CFXSyncEffectKind::NONE:
        packet.effect.effect_id = 0;
        packet.effect.name.clear();
        break;
      case CFXSyncEffectKind::CHIMERAFX:
        if (offset + 2 > payload_size) {
          return CFXSyncDecodeResult::MALFORMED;
        }
        packet.effect.effect_id = payload[offset++];
        name_size = payload[offset++];
        break;
      case CFXSyncEffectKind::UNSUPPORTED:
        if (offset + 1 > payload_size) {
          return CFXSyncDecodeResult::MALFORMED;
        }
        packet.effect.effect_id = 0;
        name_size = payload[offset++];
        break;
      default:
        return CFXSyncDecodeResult::MALFORMED;
    }

    if (packet.effect.kind != CFXSyncEffectKind::NONE) {
      if (name_size == 0 || name_size > MAX_EFFECT_NAME_BYTES ||
          offset + name_size > payload_size ||
          !is_valid_utf8_(payload + offset, name_size)) {
        return CFXSyncDecodeResult::MALFORMED;
      }
      packet.effect.name.assign(
          reinterpret_cast<const char *>(payload + offset), name_size);
      offset += name_size;
    }
  }

  if ((packet.field_mask & FIELD_CONTROLS) != 0) {
    if (offset + 2 > payload_size) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    const uint16_t control_mask = read_u16_(payload + offset);
    constexpr uint16_t KNOWN_CONTROLS =
        CONTROL_FORCE_WHITE | CONTROL_INTRO | CONTROL_OUTRO |
        CONTROL_INOUT_DURATION | CONTROL_SPEED | CONTROL_INTENSITY |
        CONTROL_MIRROR | CONTROL_PALETTE;
    if ((control_mask & ~KNOWN_CONTROLS) != 0) {
      return CFXSyncDecodeResult::MALFORMED;
    }
    offset += 2;
    packet.has_controls = true;

    if ((control_mask & CONTROL_FORCE_WHITE) != 0) {
      if (offset + 1 > payload_size || payload[offset] > 1) {
        return CFXSyncDecodeResult::MALFORMED;
      }
      packet.controls.has_force_white = true;
      packet.controls.force_white = payload[offset++] != 0;
    }
    if ((control_mask & CONTROL_INTRO) != 0) {
      if (offset + 1 > payload_size) {
        return CFXSyncDecodeResult::MALFORMED;
      }
      packet.controls.has_intro = true;
      packet.controls.intro = payload[offset++];
    }
    if ((control_mask & CONTROL_OUTRO) != 0) {
      if (offset + 1 > payload_size) {
        return CFXSyncDecodeResult::MALFORMED;
      }
      packet.controls.has_outro = true;
      packet.controls.outro = payload[offset++];
    }
    if ((control_mask & CONTROL_INOUT_DURATION) != 0) {
      if (offset + 2 > payload_size) {
        return CFXSyncDecodeResult::MALFORMED;
      }
      packet.controls.has_inout_duration = true;
      packet.controls.inout_duration_deciseconds =
          read_u16_(payload + offset);
      offset += 2;
    }
    if ((control_mask & CONTROL_SPEED) != 0) {
      if (offset + 1 > payload_size) {
        return CFXSyncDecodeResult::MALFORMED;
      }
      packet.controls.has_speed = true;
      packet.controls.speed = payload[offset++];
    }
    if ((control_mask & CONTROL_INTENSITY) != 0) {
      if (offset + 1 > payload_size) {
        return CFXSyncDecodeResult::MALFORMED;
      }
      packet.controls.has_intensity = true;
      packet.controls.intensity = payload[offset++];
    }
    if ((control_mask & CONTROL_MIRROR) != 0) {
      if (offset + 1 > payload_size || payload[offset] > 1) {
        return CFXSyncDecodeResult::MALFORMED;
      }
      packet.controls.has_mirror = true;
      packet.controls.mirror = payload[offset++] != 0;
    }
    if ((control_mask & CONTROL_PALETTE) != 0) {
      if (offset + 1 > payload_size) {
        return CFXSyncDecodeResult::MALFORMED;
      }
      packet.controls.has_palette = true;
      packet.controls.palette = payload[offset++];
    }
  }

  constexpr uint32_t KNOWN_FIELDS =
      FIELD_POWER | FIELD_BRIGHTNESS | FIELD_COLOR | FIELD_COLOR_BRIGHTNESS |
      FIELD_EFFECT | FIELD_CONTROLS;
  if ((packet.field_mask & ~KNOWN_FIELDS) == 0 && offset != payload_size) {
    return CFXSyncDecodeResult::MALFORMED;
  }

  // Unknown mask bits and their trailing canonical values are ignored by V1.
  return CFXSyncDecodeResult::OK;
}

}  // namespace cfx_sync
}  // namespace esphome

#endif  // USE_ESP32
