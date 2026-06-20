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
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/version.h"

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
                         public espnow::ESPNowReceivedPacketHandler,
                         public espnow::ESPNowUnknownPeerHandler,
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)
                         public espnow::ESPNowBroadcastedHandler
#else
                         public espnow::ESPNowBroadcastHandler
#endif
{
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
    this->control_bindings_.emplace_back();
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
  void set_peer(const std::array<uint8_t, 6> &peer);
  void set_group_hash(uint32_t group_hash) {
    this->group_hash_ = group_hash;
  }
  void set_key(const std::array<uint8_t, 32> &key) { this->key_ = key; }
  void set_heartbeat_ms(uint32_t heartbeat_ms) {
    this->heartbeat_ms_ = heartbeat_ms;
  }
  void set_force_white_control(size_t light_index,
                               switch_::Switch *control) {
    if (light_index < this->control_bindings_.size()) {
      this->control_bindings_[light_index].force_white = control;
    }
  }
  void set_intro_control(size_t light_index, select::Select *control) {
    if (light_index < this->control_bindings_.size()) {
      this->control_bindings_[light_index].intro = control;
    }
  }
  void set_outro_control(size_t light_index, select::Select *control) {
    if (light_index < this->control_bindings_.size()) {
      this->control_bindings_[light_index].outro = control;
    }
  }
  void set_inout_duration_control(size_t light_index,
                                  number::Number *control) {
    if (light_index < this->control_bindings_.size()) {
      this->control_bindings_[light_index].inout_duration = control;
    }
  }
  void set_speed_control(size_t light_index, number::Number *control) {
    if (light_index < this->control_bindings_.size()) {
      this->control_bindings_[light_index].speed = control;
    }
  }
  void set_intensity_control(size_t light_index, number::Number *control) {
    if (light_index < this->control_bindings_.size()) {
      this->control_bindings_[light_index].intensity = control;
    }
  }
  void set_mirror_control(size_t light_index, switch_::Switch *control) {
    if (light_index < this->control_bindings_.size()) {
      this->control_bindings_[light_index].mirror = control;
    }
  }
  void set_palette_control(size_t light_index, select::Select *control) {
    if (light_index < this->control_bindings_.size()) {
      this->control_bindings_[light_index].palette = control;
    }
  }

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)
  bool on_received(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                   uint8_t size) override;
  bool on_unknown_peer(const espnow::ESPNowRecvInfo &info,
                       const uint8_t *data, uint8_t size) override;
  bool on_broadcasted(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                      uint8_t size) override;
#else
  bool on_receive(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                  uint8_t size) override;
  bool on_unknown_peer(const espnow::ESPNowRecvInfo &info,
                       const uint8_t *data, uint8_t size) override;
  bool on_broadcast(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                    uint8_t size) override;
#endif
  void on_local_light_update();

 protected:
  static constexpr uint8_t MAX_CONSECUTIVE_SEND_FAILURES = 3;
  static constexpr uint32_t EFFECT_FALLBACK_LOG_INTERVAL_MS = 30000;
  static constexpr uint32_t CONTROL_SKIP_LOG_INTERVAL_MS = 30000;
  static constexpr uint8_t CFX_SYNC_MAX_PEERS = 8;
  static constexpr uint32_t PEER_TIMEOUT_MS = 120000;
  static constexpr uint32_t HELLO_INTERVAL_MS = 10000;
  static constexpr uint32_t HELLO_JITTER_SPREAD_MS = 3000;
  static constexpr uint32_t BOOT_DISCOVERY_DELAY_MS = 6000;
  static constexpr uint32_t BOOT_DISCOVERY_JITTER_SPREAD_MS = 1500;
  static constexpr uint32_t BOOT_DISCOVERY_RETRY_MS = 1000;
  static constexpr uint32_t BOOT_DISCOVERY_MAX_WAIT_MS = 20000;
  static constexpr uint32_t FOLLOWER_RECOVERY_FIRST_MS = 8000;
  static constexpr uint32_t FOLLOWER_RECOVERY_SECOND_MS = 12000;
  static constexpr uint32_t FOLLOWER_RECOVERY_THIRD_MS = 16000;
  static constexpr uint32_t FOLLOWER_RECOVERY_EXPIRE_MS = 22000;
  static constexpr uint32_t RECOVERY_JITTER_SPREAD_MS = 750;
  static constexpr uint32_t ACK_WARNING_MS = 15000;
  static constexpr uint32_t ACK_JITTER_MIN_MS = 5;
  static constexpr uint32_t ACK_JITTER_SPREAD_MS = 40;
  static constexpr uint32_t STATE_RETRY_DELAY_MS = 500;
  static constexpr uint8_t STATE_RETRY_MAX_ATTEMPTS = 3;
  static constexpr uint32_t PEER_SEND_COOLDOWN_MS = 10000;
  static constexpr std::array<uint8_t, 6> BROADCAST_MAC{
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  struct PeerState {
    bool active{false};
    std::array<uint8_t, 6> mac{};
    CFXSyncNodeRole node_role{CFXSyncNodeRole::FOLLOWER};
    uint16_t capabilities{0};
    bool registered{false};
    bool has_rx_sequence{false};
    uint32_t rx_boot_id{0};
    uint32_t rx_sequence{0};
    uint32_t last_seen_ms{0};
    uint32_t last_state_sent_boot_id{0};
    uint32_t last_state_sent_sequence{0};
    uint32_t last_state_sent_ms{0};
    uint32_t last_ack_boot_id{0};
    uint32_t last_ack_sequence{0};
    uint32_t last_ack_ms{0};
    uint32_t missed_acks{0};
    uint8_t consecutive_send_failures{0};
    uint32_t send_failures{0};
    uint32_t last_send_failure_log_ms{0};
    uint32_t tx_suspended_until_ms{0};
  };

  struct EffectLogState {
    bool valid{false};
    CFXSyncEffectKind kind{CFXSyncEffectKind::NONE};
    uint8_t effect_id{0};
    std::string name;
    uint32_t last_log_ms{0};
  };

  struct ControlBinding {
    switch_::Switch *force_white{nullptr};
    select::Select *intro{nullptr};
    select::Select *outro{nullptr};
    number::Number *inout_duration{nullptr};
    number::Number *speed{nullptr};
    number::Number *intensity{nullptr};
    switch_::Switch *mirror{nullptr};
    select::Select *palette{nullptr};
    bool callbacks_registered{false};
    uint32_t last_skip_log_ms{0};
  };

  bool send_state_();
  bool send_state_(const CFXSyncLightSnapshot &snapshot,
                   const CFXSyncEffectState &effect,
                   const CFXSyncControlState &controls);
  bool send_heartbeat_state_();
  bool send_state_to_followers_();
  bool send_state_to_followers_(const CFXSyncLightSnapshot &snapshot,
                                const CFXSyncEffectState &effect,
                                const CFXSyncControlState &controls);
  void mark_state_sent_to_followers_(uint32_t sequence);
  bool peer_accepts_leader_state_(const PeerState &peer) const;
  bool send_state_to_peer_(PeerState &peer);
  bool send_state_to_peer_(PeerState &peer,
                           const CFXSyncLightSnapshot &snapshot,
                           const CFXSyncEffectState &effect,
                           const CFXSyncControlState &controls);
  bool send_state_ack_(const uint8_t *destination,
                       const CFXSyncPacket &packet,
                       CFXSyncAckResult result);
  void schedule_state_ack_(const uint8_t *destination,
                           const CFXSyncPacket &packet,
                           CFXSyncAckResult result);
  bool send_sync_request_();
  bool send_sync_request_to_(const std::array<uint8_t, 6> &mac);
  bool send_hello_();
  bool send_packet_(std::vector<uint8_t> &packet);
  bool send_packet_to_(const std::array<uint8_t, 6> &mac,
                       std::vector<uint8_t> &packet);
  bool send_packet_to_peer_(PeerState &peer,
                            std::vector<uint8_t> &packet);
  CFXSyncNodeRole local_node_role_() const;
  uint16_t local_capabilities_() const;
  bool accepts_peer_role_(CFXSyncNodeRole role) const;
  bool should_send_state_for_hello_(const PeerState &peer,
                                    bool new_peer,
                                    bool peer_rebooted) const;
  uint32_t next_sequence_();
  PeerState *find_peer_(const uint8_t *mac);
  PeerState *find_or_add_peer_(const uint8_t *mac, CFXSyncNodeRole role,
                               uint16_t capabilities);
  bool register_peer_(PeerState &peer);
  bool accept_sequence_(PeerState &peer, uint32_t boot_id,
                        uint32_t sequence);
  bool admit_unknown_peer_(const espnow::ESPNowRecvInfo &info,
                           const uint8_t *data, uint8_t size);
  bool handle_packet_(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                      uint8_t size);
  bool handle_decoded_packet_(const espnow::ESPNowRecvInfo &info,
                              const CFXSyncPacket &packet);
  bool has_peer_send_warning_() const;
  bool has_pending_ack_(const PeerState &peer) const;
  bool is_current_broadcast_ack_(const CFXSyncPacket &packet) const;
  void seed_peer_sent_state_from_ack_(PeerState &peer,
                                      const CFXSyncPacket &packet);
  bool is_peer_send_suspended_(const PeerState &peer) const;
  uint8_t active_peer_count_() const;
  uint8_t follower_peer_count_() const;
  uint8_t remote_peer_count_() const;
  uint8_t pending_ack_count_() const;
  uint32_t missed_ack_count_() const;
  void handle_state_ack_(PeerState &peer, const CFXSyncPacket &packet);
  void check_ack_health_();
  void handle_send_result_(esp_err_t result);
  void handle_peer_send_result_(PeerState &peer, esp_err_t result);
  void clear_warning_if_set_();
  void flush_deferred_state_();
  void schedule_state_retry_();
  void handle_decode_failure_(CFXSyncDecodeResult result);
  void log_rejection_(const char *message);
  void schedule_boot_discovery_();
  void run_boot_discovery_();
  bool boot_radio_ready_() const;
  void schedule_follower_hello_();
  void schedule_follower_recovery_();
  void schedule_follower_recovery_attempt_(const char *name,
                                           uint32_t base_delay_ms);
  void apply_remote_state_(const CFXSyncPacket &packet);
  void apply_remote_state_to_light_(const CFXSyncPacket &packet,
                                    size_t light_index);
  void apply_remote_controls_to_light_(const CFXSyncPacket &packet,
                                       size_t light_index);
  void register_control_callbacks_(size_t light_index);
  CFXSyncControlState capture_control_state_(size_t light_index) const;
  void on_local_control_update();
  void log_control_skip_(ControlBinding &binding, light::LightState *light,
                         size_t light_index, const char *control_name,
                         const char *reason);
  light::LightState *leader_light_() const;
  bool is_broadcast_(const uint8_t *address) const;
  const char *role_name_() const;

  espnow::ESPNowComponent *espnow_{nullptr};
  std::vector<light::LightState *> lights_;
  std::vector<std::vector<CFXSyncEffectEntry>> effect_catalogs_;
  std::vector<EffectLogState> effect_log_states_;
  std::vector<ControlBinding> control_bindings_;
  CFXSyncRole role_{CFXSyncRole::FOLLOWER};
  std::array<uint8_t, 6> peer_{};
  bool has_static_peer_{false};
  std::array<PeerState, CFX_SYNC_MAX_PEERS> peers_{};
  std::array<uint8_t, 32> key_{};
  uint32_t group_hash_{0};
  uint32_t heartbeat_ms_{30000};
  uint32_t boot_id_{0};
  uint32_t tx_sequence_{0};
  uint32_t boot_discovery_started_ms_{0};
  bool send_pending_{false};
  bool state_send_deferred_{false};
  uint8_t state_retry_attempts_{0};
  bool state_retry_active_{false};
  bool state_retry_scheduled_{false};
  uint32_t last_broadcast_state_boot_id_{0};
  uint32_t last_broadcast_state_sequence_{0};
  uint32_t last_broadcast_state_ms_{0};

  CFXSyncLightListener light_listener_{this};
  bool applying_remote_state_{false};
  bool has_observed_state_{false};
  CFXSyncLightSnapshot observed_state_{};
  CFXSyncEffectState observed_effect_{};
  CFXSyncControlState observed_controls_{};
  bool has_valid_state_{false};

  uint32_t last_valid_packet_ms_{0};

  uint8_t consecutive_send_failures_{0};
  uint32_t last_rejection_log_ms_{0};
  uint32_t last_send_failure_log_ms_{0};
  uint32_t last_ack_warning_log_ms_{0};
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
