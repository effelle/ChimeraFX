/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Versioned, authenticated wire format for ChimeraFX synchronization.
 */

#pragma once

#ifdef USE_ESP32

#include "esphome/components/hmac_sha256/hmac_sha256.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome {
namespace cfx_sync {

enum class CFXSyncPacketType : uint8_t {
  STATE = 1,
  SYNC_REQUEST = 2,
};

enum class CFXSyncDecodeResult : uint8_t {
  OK = 0,
  NOT_CFX,
  MALFORMED,
  UNSUPPORTED_VERSION,
  WRONG_GROUP,
  BAD_AUTH,
  UNSUPPORTED_TYPE,
};

struct CFXSyncPacket {
  CFXSyncPacketType type{CFXSyncPacketType::STATE};
  uint8_t flags{0};
  uint32_t boot_id{0};
  uint32_t sequence{0};
  uint32_t field_mask{0};
  bool has_power{false};
  bool power{false};
  bool has_brightness{false};
  uint8_t brightness{0};
  bool has_color_brightness{false};
  uint8_t color_brightness{0};
  bool has_color{false};
  bool source_has_white{false};
  uint8_t red{0};
  uint8_t green{0};
  uint8_t blue{0};
  uint8_t white{0};
};

class CFXSyncPacketCodec {
 public:
  static constexpr uint8_t VERSION = 1;
  static constexpr size_t HEADER_SIZE = 22;
  static constexpr size_t AUTH_TAG_SIZE = 16;
  static constexpr uint32_t FIELD_POWER = 0x00000001UL;
  static constexpr uint32_t FIELD_BRIGHTNESS = 0x00000002UL;
  static constexpr uint32_t FIELD_COLOR = 0x00000004UL;
  static constexpr uint32_t FIELD_COLOR_BRIGHTNESS = 0x00000008UL;
  static constexpr uint8_t COLOR_CAP_WHITE = 0x01;
  static constexpr uint32_t FULL_STATE_MASK =
      FIELD_POWER | FIELD_BRIGHTNESS | FIELD_COLOR | FIELD_COLOR_BRIGHTNESS;
  static constexpr size_t FULL_STATE_PAYLOAD_SIZE = 12;
  static constexpr size_t STATE_PACKET_SIZE =
      HEADER_SIZE + FULL_STATE_PAYLOAD_SIZE + AUTH_TAG_SIZE;
  static constexpr size_t REQUEST_PACKET_SIZE = HEADER_SIZE + AUTH_TAG_SIZE;

  static bool encode_state(uint32_t group_hash, uint32_t boot_id,
                           uint32_t sequence, bool power, uint8_t brightness,
                           uint8_t color_brightness,
                           uint8_t red, uint8_t green, uint8_t blue,
                           uint8_t white, bool has_white,
                           const std::array<uint8_t, 32> &key,
                           std::vector<uint8_t> &output);
  static bool encode_sync_request(uint32_t group_hash, uint32_t boot_id,
                                  uint32_t sequence,
                                  const std::array<uint8_t, 32> &key,
                                  std::vector<uint8_t> &output);
  static CFXSyncDecodeResult decode(const uint8_t *data, size_t size,
                                    uint32_t expected_group_hash,
                                    const std::array<uint8_t, 32> &key,
                                    CFXSyncPacket &packet);

 protected:
  static bool encode_(CFXSyncPacketType type, uint32_t group_hash,
                      uint32_t boot_id, uint32_t sequence,
                      const uint8_t *payload, size_t payload_size,
                      const std::array<uint8_t, 32> &key,
                      std::vector<uint8_t> &output);
  static void append_u16_(std::vector<uint8_t> &output, uint16_t value);
  static void append_u32_(std::vector<uint8_t> &output, uint32_t value);
  static uint16_t read_u16_(const uint8_t *data);
  static uint32_t read_u32_(const uint8_t *data);
  static void calculate_tag_(const uint8_t *data, size_t size,
                             const std::array<uint8_t, 32> &key,
                             uint8_t *tag);
  static bool tags_equal_(const uint8_t *left, const uint8_t *right,
                          size_t size);
};

static_assert(CFXSyncPacketCodec::STATE_PACKET_SIZE < 250,
              "CFX sync state packet exceeds ESP-NOW V1 payload limit");
static_assert(CFXSyncPacketCodec::REQUEST_PACKET_SIZE < 250,
              "CFX sync request packet exceeds ESP-NOW V1 payload limit");

}  // namespace cfx_sync
}  // namespace esphome

#endif  // USE_ESP32
