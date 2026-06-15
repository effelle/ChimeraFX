/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Authenticated leader/follower light synchronization over ESP-NOW.
 */

#pragma once

#ifdef USE_ESP32

#include "cfx_sync_color.h"
#include "cfx_sync_effect.h"
#include "cfx_sync_packet.h"
#include "esphome/components/espnow/espnow_component.h"
#include "esphome/components/light/light_state.h"
#include "esphome/core/component.h"

#include <array>
#include <cstdint>
#include <vector>

namespace esphome {
namespace cfx_sync {

enum CFXSyncRole : uint8_t {
  LEADER = 0,
  FOLLOWER = 1,
};

class CFXSyncComponent;

class CFXSyncLightListener : public light::LightRemoteValuesListener {
 public:
  explicit CFXSyncLightListener(CFXSyncComponent *parent) : parent_(parent) {}
  void on_light_remote_values_update() override;

 protected:
  CFXSyncComponent *parent_;
};

class CFXSyncComponent : public Component,
                         public espnow::ESPNowReceivedPacketHandler {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::LATE - 1.0f;
  }

  void set_espnow(espnow::ESPNowComponent *espnow) {
    this->espnow_ = espnow;
  }
  void add_light(light::LightState *light) {
    this->lights_.push_back(light);
    this->effect_catalogs_.emplace_back();
    this->effect_log_states_.emplace_back();
  }
  void add_effect(size_t light_index, uint8_t effect_id,
                  const std::string &name) {
    if (light_index >= this->effect_catalogs_.size()) {
      return;
    }
    this->effect_catalogs_[light_index].push_back(
        CFXSyncEffectEntry{effect_id, name});
  }
  void set_role(CFXSyncRole role) { this->role_ = role; }
  void set_peer(const std::array<uint8_t, 6> &peer) {
    this->peer_ = peer;
  }
  void set_group_hash(uint32_t group_hash) {
    this->group_hash_ = group_hash;
  }
  void set_key(const std::array<uint8_t, 32> &key) { this->key_ = key; }
  void set_heartbeat_ms(uint32_t heartbeat_ms) {
    this->heartbeat_ms_ = heartbeat_ms;
  }

  bool on_receive(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                  uint8_t size) override;
  void on_local_light_update();

 protected:
  static constexpr uint8_t MAX_CONSECUTIVE_SEND_FAILURES = 3;

  struct EffectLogState {
    bool valid{false};
    CFXSyncEffectKind kind{CFXSyncEffectKind::NONE};
    uint8_t effect_id{0};
    std::string name;
    uint32_t last_log_ms{0};
  };

  bool send_state_();
  bool send_state_(const CFXSyncLightSnapshot &snapshot);
  bool send_sync_request_();
  bool send_packet_(std::vector<uint8_t> &packet);
  uint32_t next_sequence_();
  bool accept_sequence_(uint32_t boot_id, uint32_t sequence);
  void handle_send_result_(esp_err_t result);
  void handle_decode_failure_(CFXSyncDecodeResult result);
  void log_rejection_(const char *message);
  void schedule_follower_recovery_();
  void apply_remote_state_(const CFXSyncPacket &packet);
  void apply_remote_state_to_light_(const CFXSyncPacket &packet,
                                    light::LightState *light);
  light::LightState *leader_light_() const;
  bool is_peer_(const uint8_t *address) const;
  const char *role_name_() const;

  espnow::ESPNowComponent *espnow_{nullptr};
  std::vector<light::LightState *> lights_;
  std::vector<std::vector<CFXSyncEffectEntry>> effect_catalogs_;
  std::vector<EffectLogState> effect_log_states_;
  CFXSyncRole role_{CFXSyncRole::FOLLOWER};
  std::array<uint8_t, 6> peer_{};
  std::array<uint8_t, 32> key_{};
  uint32_t group_hash_{0};
  uint32_t heartbeat_ms_{30000};
  uint32_t boot_id_{0};
  uint32_t tx_sequence_{0};

  CFXSyncLightListener light_listener_{this};
  bool applying_remote_state_{false};
  bool has_observed_state_{false};
  CFXSyncLightSnapshot observed_state_{};
  bool has_valid_state_{false};

  bool has_rx_sequence_{false};
  uint32_t rx_boot_id_{0};
  uint32_t rx_sequence_{0};
  uint32_t last_valid_packet_ms_{0};

  uint8_t consecutive_send_failures_{0};
  uint32_t last_rejection_log_ms_{0};
  uint32_t last_send_failure_log_ms_{0};
  uint32_t sent_packets_{0};
  uint32_t received_packets_{0};
  uint32_t malformed_packets_{0};
  uint32_t authentication_failures_{0};
  uint32_t wrong_group_packets_{0};
  uint32_t stale_packets_{0};
  uint32_t unsupported_packets_{0};
  uint32_t send_failures_{0};
};

}  // namespace cfx_sync
}  // namespace esphome

#endif  // USE_ESP32
