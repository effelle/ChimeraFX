/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Versioned, authenticated wire format for ChimeraFX synchronization.
 */

#pragma once

#if defined(USE_ESP32) || defined(USE_ESP8266)

#include "cfx_sync_effect.h"
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
  HELLO = 3,
  STATE_ACK = 4,
  INPUT_STATE = 5,
};

enum class CFXSyncNodeRole : uint8_t {
  LEADER = 1,
  FOLLOWER = 2,
  REMOTE = 3,
};

enum class CFXSyncAckResult : uint8_t {
  APPLIED = 0,
  IGNORED_UNSUPPORTED = 1,
  APPLY_FAILED = 2,
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

struct CFXSyncControlState {
  bool has_force_white{false};
  bool force_white{false};
  bool has_intro{false};
  uint8_t intro{0};
  bool has_outro{false};
  uint8_t outro{0};
  bool has_inout_duration{false};
  uint16_t inout_duration_deciseconds{0};
  bool has_speed{false};
  uint8_t speed{0};
  bool has_intensity{false};
  uint8_t intensity{0};
  bool has_mirror{false};
  bool mirror{false};
  bool has_palette{false};
  uint8_t palette{0};

  bool has_any() const {
    return this->has_force_white || this->has_intro || this->has_outro ||
           this->has_inout_duration || this->has_speed ||
           this->has_intensity || this->has_mirror || this->has_palette;
  }

  bool operator==(const CFXSyncControlState &other) const {
    return this->has_force_white == other.has_force_white &&
           this->force_white == other.force_white &&
           this->has_intro == other.has_intro &&
           this->intro == other.intro &&
           this->has_outro == other.has_outro &&
           this->outro == other.outro &&
           this->has_inout_duration == other.has_inout_duration &&
           this->inout_duration_deciseconds ==
               other.inout_duration_deciseconds &&
           this->has_speed == other.has_speed &&
           this->speed == other.speed &&
           this->has_intensity == other.has_intensity &&
           this->intensity == other.intensity &&
           this->has_mirror == other.has_mirror &&
           this->mirror == other.mirror &&
           this->has_palette == other.has_palette &&
           this->palette == other.palette;
  }
  bool operator!=(const CFXSyncControlState &other) const {
    return !(*this == other);
  }
};

struct CFXSyncTimingState {
  bool has_transition{false};
  uint16_t transition_ms{0};
  bool has_ramp{false};
  uint16_t ramp_ms{0};
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
  bool has_effect{false};
  CFXSyncEffectState effect;
  bool has_controls{false};
  CFXSyncControlState controls;
  bool has_transition{false};
  uint16_t transition_ms{0};
  bool has_ramp{false};
  uint16_t ramp_ms{0};
  CFXSyncNodeRole node_role{CFXSyncNodeRole::FOLLOWER};
  uint16_t capabilities{0};
  uint32_t acked_boot_id{0};
  uint32_t acked_sequence{0};
  CFXSyncAckResult ack_result{CFXSyncAckResult::APPLIED};
  bool input_pressed{false};
  bool input_maintained{false};
  bool input_toggle{false};
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
  static constexpr uint32_t FIELD_EFFECT = 0x00000010UL;
  static constexpr uint32_t FIELD_CONTROLS = 0x00000020UL;
  static constexpr uint32_t FIELD_TRANSITION = 0x00000040UL;
  static constexpr uint32_t FIELD_RAMP = 0x00000080UL;
  static constexpr uint16_t CONTROL_FORCE_WHITE = 0x0001U;
  static constexpr uint16_t CONTROL_INTRO = 0x0002U;
  static constexpr uint16_t CONTROL_OUTRO = 0x0004U;
  static constexpr uint16_t CONTROL_INOUT_DURATION = 0x0008U;
  static constexpr uint16_t CONTROL_SPEED = 0x0010U;
  static constexpr uint16_t CONTROL_INTENSITY = 0x0020U;
  static constexpr uint16_t CONTROL_MIRROR = 0x0040U;
  static constexpr uint16_t CONTROL_PALETTE = 0x0080U;
  static constexpr uint16_t CAP_LIGHT_LEADER = 0x0001U;
  static constexpr uint16_t CAP_LIGHT_FOLLOWER = 0x0002U;
  static constexpr uint16_t CAP_BINARY_REMOTE = 0x0004U;
  static constexpr uint8_t COLOR_CAP_WHITE = 0x01;
  static constexpr uint8_t INPUT_FLAG_PRESSED = 0x01;
  static constexpr uint8_t INPUT_FLAG_MAINTAINED = 0x02;
  static constexpr uint8_t INPUT_FLAG_TOGGLE = 0x04;
  static constexpr uint32_t FULL_STATE_MASK =
      FIELD_POWER | FIELD_BRIGHTNESS | FIELD_COLOR | FIELD_COLOR_BRIGHTNESS;
  static constexpr size_t FULL_STATE_PAYLOAD_SIZE = 12;
  static constexpr size_t MAX_EFFECT_NAME_BYTES = 64;
  static constexpr size_t MAX_EFFECT_VALUE_SIZE = 67;
  static constexpr size_t MAX_CONTROLS_VALUE_SIZE = 11;
  static constexpr size_t MAX_TIMING_VALUE_SIZE = 4;
  static constexpr size_t MAX_EFFECT_STATE_PACKET_SIZE =
      HEADER_SIZE + FULL_STATE_PAYLOAD_SIZE + MAX_EFFECT_VALUE_SIZE +
      AUTH_TAG_SIZE;
  static constexpr size_t MAX_STATE_PAYLOAD_SIZE =
      FULL_STATE_PAYLOAD_SIZE + MAX_EFFECT_VALUE_SIZE +
      MAX_CONTROLS_VALUE_SIZE + MAX_TIMING_VALUE_SIZE;
  static constexpr size_t STATE_PACKET_SIZE =
      HEADER_SIZE + FULL_STATE_PAYLOAD_SIZE + AUTH_TAG_SIZE;
  static constexpr size_t HELLO_PAYLOAD_SIZE = 3;
  static constexpr size_t STATE_ACK_PAYLOAD_SIZE = 9;
  static constexpr size_t INPUT_STATE_PAYLOAD_SIZE = 1;
  static constexpr size_t MAX_STATE_PACKET_SIZE =
      HEADER_SIZE + MAX_STATE_PAYLOAD_SIZE + AUTH_TAG_SIZE;  // 132 bytes.
  static constexpr size_t REQUEST_PACKET_SIZE = HEADER_SIZE + AUTH_TAG_SIZE;
  static constexpr size_t HELLO_PACKET_SIZE =
      HEADER_SIZE + HELLO_PAYLOAD_SIZE + AUTH_TAG_SIZE;
  static constexpr size_t STATE_ACK_PACKET_SIZE =
      HEADER_SIZE + STATE_ACK_PAYLOAD_SIZE + AUTH_TAG_SIZE;
  static constexpr size_t INPUT_STATE_PACKET_SIZE =
      HEADER_SIZE + INPUT_STATE_PAYLOAD_SIZE + AUTH_TAG_SIZE;

  static bool encode_state(uint32_t group_hash, uint32_t boot_id,
                           uint32_t sequence, bool power, uint8_t brightness,
                           uint8_t color_brightness,
                           uint8_t red, uint8_t green, uint8_t blue,
                           uint8_t white, bool has_white,
                           const std::array<uint8_t, 32> &key,
                           std::vector<uint8_t> &output);
  static bool encode_state(uint32_t group_hash, uint32_t boot_id,
                           uint32_t sequence, bool power, uint8_t brightness,
                           uint8_t color_brightness,
                           uint8_t red, uint8_t green, uint8_t blue,
                           uint8_t white, bool has_white, bool has_effect,
                           const CFXSyncEffectState &effect,
                           const std::array<uint8_t, 32> &key,
                           std::vector<uint8_t> &output);
  static bool encode_state(uint32_t group_hash, uint32_t boot_id,
                           uint32_t sequence, bool power, uint8_t brightness,
                           uint8_t color_brightness,
                           uint8_t red, uint8_t green, uint8_t blue,
                           uint8_t white, bool has_white, bool has_effect,
                           const CFXSyncEffectState &effect,
                           bool has_controls,
                           const CFXSyncControlState &controls,
                           const CFXSyncTimingState &timing,
                           const std::array<uint8_t, 32> &key,
                           std::vector<uint8_t> &output);
  static bool encode_sync_request(uint32_t group_hash, uint32_t boot_id,
                                  uint32_t sequence,
                                  const std::array<uint8_t, 32> &key,
                                  std::vector<uint8_t> &output);
  static bool encode_hello(uint32_t group_hash, uint32_t boot_id,
                           uint32_t sequence, CFXSyncNodeRole role,
                           uint16_t capabilities,
                           const std::array<uint8_t, 32> &key,
                           std::vector<uint8_t> &output);
  static bool encode_state_ack(uint32_t group_hash, uint32_t boot_id,
                               uint32_t sequence, uint32_t acked_boot_id,
                               uint32_t acked_sequence,
                               CFXSyncAckResult result,
                               const std::array<uint8_t, 32> &key,
                               std::vector<uint8_t> &output);
  static bool encode_input_state(uint32_t group_hash, uint32_t boot_id,
                                 uint32_t sequence, bool pressed,
                                 bool maintained, bool toggle,
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
  static bool is_valid_utf8_(const uint8_t *data, size_t size);
};

static_assert(CFXSyncPacketCodec::MAX_EFFECT_VALUE_SIZE == 67,
              "CFX sync maximum effect value size changed");
static_assert(CFXSyncPacketCodec::MAX_EFFECT_VALUE_SIZE ==
                  3 + CFXSyncPacketCodec::MAX_EFFECT_NAME_BYTES,
              "CFX sync effect value size is inconsistent");
static_assert(CFXSyncPacketCodec::MAX_CONTROLS_VALUE_SIZE == 11,
              "CFX sync maximum controls value size changed");
static_assert(CFXSyncPacketCodec::MAX_TIMING_VALUE_SIZE == 4,
              "CFX sync maximum timing value size changed");
static_assert(CFXSyncPacketCodec::MAX_EFFECT_STATE_PACKET_SIZE == 117,
              "CFX sync maximum effect state packet size changed");
static_assert(CFXSyncPacketCodec::MAX_STATE_PACKET_SIZE == 132,
              "CFX sync maximum state packet size changed");
static_assert(CFXSyncPacketCodec::MAX_STATE_PACKET_SIZE < 250,
              "CFX sync state packet exceeds ESP-NOW V1 payload limit");
static_assert(CFXSyncPacketCodec::REQUEST_PACKET_SIZE < 250,
              "CFX sync request packet exceeds ESP-NOW V1 payload limit");
static_assert(CFXSyncPacketCodec::HELLO_PACKET_SIZE == 41,
              "CFX sync hello packet size changed");
static_assert(CFXSyncPacketCodec::STATE_ACK_PACKET_SIZE == 47,
              "CFX sync state ack packet size changed");
static_assert(CFXSyncPacketCodec::INPUT_STATE_PACKET_SIZE == 39,
              "CFX sync input state packet size changed");
static_assert(CFXSyncPacketCodec::HELLO_PACKET_SIZE < 250,
              "CFX sync hello packet exceeds ESP-NOW V1 payload limit");
static_assert(CFXSyncPacketCodec::STATE_ACK_PACKET_SIZE < 250,
              "CFX sync state ack packet exceeds ESP-NOW V1 payload limit");
static_assert(CFXSyncPacketCodec::INPUT_STATE_PACKET_SIZE < 250,
              "CFX sync input state packet exceeds ESP-NOW V1 payload limit");

}  // namespace cfx_sync
}  // namespace esphome

#endif  // defined(USE_ESP32) || defined(USE_ESP8266)
