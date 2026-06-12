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
  std::vector<uint8_t> payload;
  payload.reserve(FULL_STATE_PAYLOAD_SIZE);
  append_u32_(payload, FULL_STATE_MASK);
  payload.push_back(power ? 1 : 0);
  payload.push_back(brightness);
  payload.push_back(has_white ? COLOR_CAP_WHITE : 0);
  payload.push_back(red);
  payload.push_back(green);
  payload.push_back(blue);
  payload.push_back(white);
  payload.push_back(color_brightness);
  return encode_(CFXSyncPacketType::STATE, group_hash, boot_id, sequence,
                 payload.data(), payload.size(), key, output);
}

bool CFXSyncPacketCodec::encode_sync_request(
    uint32_t group_hash, uint32_t boot_id, uint32_t sequence,
    const std::array<uint8_t, 32> &key, std::vector<uint8_t> &output) {
  return encode_(CFXSyncPacketType::SYNC_REQUEST, group_hash, boot_id,
                 sequence, nullptr, 0, key, output);
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
          static_cast<uint8_t>(CFXSyncPacketType::SYNC_REQUEST)) {
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

  if (payload_size < sizeof(uint32_t)) {
    return CFXSyncDecodeResult::MALFORMED;
  }
  const uint8_t *payload = data + HEADER_SIZE;
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

  constexpr uint32_t KNOWN_FIELDS =
      FIELD_POWER | FIELD_BRIGHTNESS | FIELD_COLOR | FIELD_COLOR_BRIGHTNESS;
  if ((packet.field_mask & ~KNOWN_FIELDS) == 0 && offset != payload_size) {
    return CFXSyncDecodeResult::MALFORMED;
  }

  // Unknown mask bits and their trailing canonical values are ignored by V1.
  return CFXSyncDecodeResult::OK;
}

}  // namespace cfx_sync
}  // namespace esphome

#endif  // USE_ESP32
