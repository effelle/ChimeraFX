/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Authenticated leader/follower light synchronization over ESP-NOW.
 */

#pragma once

#if defined(USE_ESP32) || defined(USE_ESP8266)

#if defined(USE_ESP32)
#include "cfx_sync_color.h"
#include "cfx_sync_effect.h"
#include "../cfx_button/cfx_button.h"
#endif
#include "cfx_sync_bus.h"
#include "cfx_sync_packet.h"
#include "cfx_sync_transport.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#ifdef USE_ESPNOW
#include "esphome/components/espnow/espnow_component.h"
#endif
#include "esphome/components/light/light_state.h"
#if defined(USE_ESP32)
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#endif
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
  CFX_SYNC_ROLE_CONTROLLER = 2,
  SATELLITE = 3,
};

enum class CFXSyncInputMode : uint8_t {
  CFX_SYNC_INPUT_MOMENTARY = 0,
  CFX_SYNC_INPUT_MAINTAINED = 1,
  CFX_SYNC_INPUT_TOGGLE = 2,
};

enum class CFXSyncTransport : uint8_t {
  CFX_SYNC_TRANSPORT_AUTO = 0,
  CFX_SYNC_TRANSPORT_ESPNOW = 1,
  CFX_SYNC_TRANSPORT_UDP = 2,
};

class CFXSyncComponent;

#if defined(USE_ESP32)
class CFXSyncEnableSwitch : public switch_::Switch {
 public:
  void set_parent(CFXSyncComponent *parent) { this->parent_ = parent; }
  void write_state(bool state) override;

 protected:
  CFXSyncComponent *parent_{nullptr};
};

class CFXSyncLightListener : public light::LightRemoteValuesListener {
 public:
  explicit CFXSyncLightListener(CFXSyncComponent *parent) : parent_(parent) {}
  void on_light_remote_values_update() override;

 protected:
  CFXSyncComponent *parent_;
};
#endif

class CFXSyncComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::LATE - 1.0f;
  }

#ifdef USE_ESPNOW
  void set_espnow(espnow::ESPNowComponent *espnow) {
    this->bus_->set_espnow(espnow);
  }
#endif
  void add_light(light::LightState *light) {
    this->lights_.push_back(light);
#if defined(USE_ESP32)
    this->effect_catalogs_.emplace_back();
    this->effect_log_states_.emplace_back();
    this->control_bindings_.emplace_back();
#endif
  }
#if defined(USE_ESP32)
  void add_effect(size_t light_index, uint8_t effect_id,
                  const std::string &name) {
    if (light_index >= this->effect_catalogs_.size()) {
      return;
    }
    this->effect_catalogs_[light_index].push_back(
        CFXSyncEffectEntry{effect_id, name});
  }
#endif
  void set_role(CFXSyncRole role) { this->role_ = role; }
  void set_local_input(binary_sensor::BinarySensor *input) {
    this->local_input_ = input;
  }
#if defined(USE_ESP32)
  void set_remote_input(cfx_button::CFXButton *input) {
    this->remote_input_ = input;
  }
  void set_sync_switch(CFXSyncEnableSwitch *sync_switch) {
    this->sync_switch_ = sync_switch;
  }
#endif
  void set_input_mode(CFXSyncInputMode mode) {
    this->input_mode_ = mode;
  }
  void set_transport(CFXSyncTransport transport) {
    this->transport_ = transport;
  }
  void set_fallback_channel(uint8_t channel) {
    this->fallback_channel_ = channel;
  }
  void set_peer(const std::array<uint8_t, 6> &peer);
  void set_group_hash(uint32_t group_hash) {
    this->group_hash_ = group_hash;
  }
  void set_key(const std::array<uint8_t, 32> &key) { this->key_ = key; }
  void set_heartbeat_ms(uint32_t heartbeat_ms) {
    this->heartbeat_ms_ = heartbeat_ms;
  }
#if defined(USE_ESP32)
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
#endif

  void on_local_light_update();
  void on_sync_enabled_switch(bool enabled);

 protected:
  friend class CFXSyncBus;

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
  static constexpr uint32_t FOLLOWER_RECOVERY_REPEAT_MS = 15000;
  static constexpr uint32_t FOLLOWER_RECOVERY_REPEAT_JITTER_SPREAD_MS = 5000;
  static constexpr uint32_t FOLLOWER_RECOVERY_REARM_MS = 60000;
  static constexpr uint32_t WIFI_OFFLINE_GRACE_MS = 5000;
  static constexpr uint32_t WIFI_CHANNEL_STABLE_MS = 1500;
  static constexpr uint32_t RECOVERY_JITTER_SPREAD_MS = 750;
  static constexpr uint32_t ESPNOW_REARM_DELAY_MS = 750;
  static constexpr uint32_t ESPNOW_REARM_MIN_INTERVAL_MS = 30000;
  static constexpr uint32_t ACK_WARNING_MS = 15000;
  static constexpr uint32_t ACK_JITTER_MIN_MS = 5;
  static constexpr uint32_t ACK_JITTER_SPREAD_MS = 40;
  static constexpr uint32_t STATE_RETRY_DELAY_MS = 500;
  static constexpr uint8_t STATE_RETRY_MAX_ATTEMPTS = 3;
  static constexpr uint32_t PEER_SEND_COOLDOWN_MS = 10000;
  static constexpr uint32_t INPUT_HOLD_REPEAT_MS = 750;
  static constexpr uint32_t INPUT_MAINTAINED_SETTLE_MS = 150;
  static constexpr uint32_t INPUT_RELEASE_REPEAT_MS = 120;
  static constexpr uint8_t INPUT_RELEASE_REPEAT_COUNT = 3;
  static constexpr uint32_t UDP_INPUT_RETRY_DELAY_MS = 25;
  static constexpr uint8_t UDP_INPUT_RETRY_COUNT = 1;
  static constexpr uint32_t REMOTE_INPUT_TIMEOUT_MS = 2500;
  static constexpr uint8_t PENDING_INPUT_QUEUE_SIZE = 8;
  static constexpr uint16_t DEFAULT_UDP_PORT = 39580;
  static constexpr std::array<uint8_t, 6> BROADCAST_MAC{
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  struct PeerState {
    bool active{false};
    CFXSyncTransportKind transport{CFXSyncTransportKind::ESPNOW};
    std::array<uint8_t, 6> mac{};
    uint32_t ipv4{0};
    uint16_t udp_port{0};
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

#if defined(USE_ESP32)
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
#endif

  struct PendingInputEvent {
    bool pressed{false};
    bool maintained{false};
    bool toggle{false};
  };

#if defined(USE_ESP32)
  bool send_state_(bool include_default_transition = false);
  bool send_state_(const CFXSyncLightSnapshot &snapshot,
                   const CFXSyncEffectState &effect,
                   const CFXSyncControlState &controls,
                   bool include_default_transition);
  bool send_heartbeat_state_();
  bool send_state_to_followers_();
  bool send_state_to_followers_(const CFXSyncLightSnapshot &snapshot,
                                const CFXSyncEffectState &effect,
                                const CFXSyncControlState &controls,
                                bool include_default_transition);
  void mark_state_sent_to_followers_(uint32_t sequence);
  bool peer_accepts_leader_state_(const PeerState &peer) const;
  bool send_state_to_peer_(PeerState &peer);
  bool send_state_to_peer_(PeerState &peer,
                           const CFXSyncLightSnapshot &snapshot,
                           const CFXSyncEffectState &effect,
                           const CFXSyncControlState &controls);
#endif
  bool send_state_ack_(const uint8_t *destination,
                       const CFXSyncPacket &packet,
                       CFXSyncAckResult result);
  void schedule_state_ack_(const uint8_t *destination,
                           const CFXSyncPacket &packet,
                           CFXSyncAckResult result);
  bool send_sync_request_();
  bool send_sync_request_to_(const std::array<uint8_t, 6> &mac);
  bool send_hello_();
  bool send_input_state_(bool pressed, bool maintained, bool toggle);
  void schedule_udp_input_retry_(std::vector<uint8_t> packet, uint8_t remaining);
  void queue_input_state_(bool pressed, bool maintained, bool toggle);
  void flush_deferred_input_();
  void on_local_input_update_(bool pressed);
  void schedule_local_input_hold_repeat_(uint32_t generation);
  void schedule_local_input_release_repeat_(uint8_t remaining,
                                            uint32_t generation);
  bool handle_remote_input_(PeerState &peer, bool pressed, bool maintained,
                            bool toggle);
  bool inject_remote_input_(bool pressed, bool maintained, bool toggle);
  void clear_remote_input_owner_();
  void apply_remote_power_input_(bool pressed);
  void apply_remote_toggle_input_();
  void schedule_remote_input_timeout_();
  bool send_packet_(std::vector<uint8_t> &packet);
  bool send_udp_packet_(std::vector<uint8_t> &packet);
  bool send_espnow_packet_to_(const std::array<uint8_t, 6> &mac,
                              std::vector<uint8_t> &packet);
  bool send_packet_to_(const std::array<uint8_t, 6> &mac,
                       std::vector<uint8_t> &packet);
  bool send_state_packet_to_followers_(std::vector<uint8_t> &packet);
  bool send_packet_to_peer_(PeerState &peer,
                            std::vector<uint8_t> &packet);
  bool use_udp_transport_() const;
  bool use_espnow_transport_() const;
  CFXSyncNodeRole local_node_role_() const;
  uint16_t local_capabilities_() const;
  bool is_state_receiver_role_() const;
  bool is_input_sender_role_() const;
  bool accepts_peer_role_(CFXSyncNodeRole role) const;
  bool should_send_state_for_hello_(const PeerState &peer,
                                    bool new_peer,
                                    bool peer_rebooted) const;
  uint32_t next_sequence_();
  PeerState *find_peer_(const uint8_t *mac);
  PeerState *find_peer_(const CFXSyncSource &source);
  PeerState *find_or_add_peer_(const uint8_t *mac, CFXSyncNodeRole role,
                               uint16_t capabilities);
  PeerState *find_or_add_peer_(const CFXSyncSource &source,
                               CFXSyncNodeRole role,
                               uint16_t capabilities);
  bool peer_matches_source_(const PeerState &peer,
                            const CFXSyncSource &source) const;
  bool register_peer_(PeerState &peer);
  bool accept_sequence_(PeerState &peer, uint32_t boot_id,
                        uint32_t sequence);
  bool handle_unknown_packet_(const CFXSyncSource &source,
                              const uint8_t *data, size_t size);
  bool handle_packet_(const CFXSyncSource &source, const uint8_t *data,
                      size_t size);
  bool handle_decoded_packet_(const CFXSyncSource &source,
                              const CFXSyncPacket &packet);
  bool has_peer_send_warning_() const;
  bool has_pending_ack_(const PeerState &peer) const;
#if defined(USE_ESP32)
  bool is_current_broadcast_ack_(const CFXSyncPacket &packet) const;
  void seed_peer_sent_state_from_ack_(PeerState &peer,
                                      const CFXSyncPacket &packet);
#endif
  bool is_peer_send_suspended_(const PeerState &peer) const;
  uint8_t active_peer_count_() const;
  uint8_t follower_peer_count_() const;
  uint8_t remote_peer_count_() const;
  uint8_t pending_ack_count_() const;
  uint32_t missed_ack_count_() const;
#if defined(USE_ESP32)
  void handle_state_ack_(PeerState &peer, const CFXSyncPacket &packet);
  void check_ack_health_();
  void handle_send_result_(esp_err_t result);
  void handle_peer_send_result_(PeerState &peer, esp_err_t result);
#endif
  void clear_warning_if_set_();
  void flush_deferred_state_();
#if defined(USE_ESP32)
  void schedule_state_retry_();
#endif
  void handle_decode_failure_(CFXSyncDecodeResult result);
  void log_rejection_(const char *message);
  void schedule_boot_discovery_();
  void run_boot_discovery_();
  bool boot_radio_ready_() const;
  void schedule_follower_hello_();
  void apply_remote_state_(const CFXSyncPacket &packet);
  void apply_remote_state_to_light_(const CFXSyncPacket &packet,
                                    size_t light_index);
#if defined(USE_ESP32)
  uint8_t current_wifi_channel_() const;
  uint8_t active_sync_channel_() const;
  const char *sync_mode_name_() const;
  void enter_offline_fallback_();
  void exit_offline_fallback_(uint8_t channel);
  bool apply_fallback_channel_();
  void format_current_wifi_bssid_(char *buffer, size_t size) const;
  void schedule_espnow_rearm_(const char *reason);
  void schedule_follower_recovery_();
  void schedule_follower_recovery_loop_();
  void schedule_follower_recovery_attempt_(const char *name,
                                           uint32_t base_delay_ms);
  void schedule_enable_resync_();
  void schedule_enable_resync_attempt_(const char *name, uint32_t delay_ms);
  void apply_remote_controls_to_light_(const CFXSyncPacket &packet,
                                       size_t light_index);
  void register_control_callbacks_(size_t light_index);
  CFXSyncControlState capture_control_state_(size_t light_index) const;
  void on_local_control_update();
  void log_control_skip_(ControlBinding &binding, light::LightState *light,
                         size_t light_index, const char *control_name,
                         const char *reason);
  light::LightState *leader_light_() const;
#endif
  bool is_broadcast_(const uint8_t *address) const;
  const char *role_name_() const;
  const char *transport_name_() const;

  CFXSyncBus *bus_{&global_cfx_sync_bus()};
  std::vector<light::LightState *> lights_;
#if defined(USE_ESP32)
  std::vector<std::vector<CFXSyncEffectEntry>> effect_catalogs_;
  std::vector<EffectLogState> effect_log_states_;
  std::vector<ControlBinding> control_bindings_;
#endif
  CFXSyncRole role_{CFXSyncRole::FOLLOWER};
  binary_sensor::BinarySensor *local_input_{nullptr};
#if defined(USE_ESP32)
  cfx_button::CFXButton *remote_input_{nullptr};
  CFXSyncEnableSwitch *sync_switch_{nullptr};
#endif
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
#if defined(USE_ESP32)
  bool state_send_deferred_{false};
  bool state_send_deferred_with_transition_{false};
  uint8_t state_retry_attempts_{0};
  bool state_retry_active_{false};
  bool state_retry_scheduled_{false};
  uint32_t last_broadcast_state_boot_id_{0};
  uint32_t last_broadcast_state_sequence_{0};
  uint32_t last_broadcast_state_ms_{0};
  uint8_t last_wifi_channel_{0};
  uint8_t pending_wifi_channel_{0};
  uint32_t pending_wifi_channel_since_ms_{0};
  bool last_wifi_connected_{false};
  bool offline_fallback_active_{false};
  uint32_t wifi_disconnected_since_ms_{0};
  bool espnow_rearm_scheduled_{false};
  uint32_t last_espnow_rearm_ms_{0};
#endif
  uint8_t fallback_channel_{6};
  CFXSyncInputMode input_mode_{CFXSyncInputMode::CFX_SYNC_INPUT_MOMENTARY};
  CFXSyncTransport transport_{CFXSyncTransport::CFX_SYNC_TRANSPORT_AUTO};
  uint16_t udp_port_{DEFAULT_UDP_PORT};
  bool local_input_has_state_{false};
  bool local_input_pressed_{false};
  bool local_input_sent_has_state_{false};
  bool local_input_sent_pressed_{false};
  uint32_t local_input_maintained_generation_{0};
  uint32_t local_input_repeat_generation_{0};
  PendingInputEvent pending_input_events_[PENDING_INPUT_QUEUE_SIZE];
  uint8_t pending_input_head_{0};
  uint8_t pending_input_count_{0};
  PeerState *remote_input_owner_{nullptr};
  bool remote_input_pressed_{false};
  uint32_t last_remote_input_ms_{0};
  bool sync_enabled_{true};
  bool applying_remote_state_{false};
  bool has_valid_state_{false};
  uint32_t last_unsupported_visual_log_ms_{0};

#if defined(USE_ESP32)
  CFXSyncLightListener light_listener_{this};
  bool has_observed_state_{false};
  CFXSyncLightSnapshot observed_state_{};
  CFXSyncEffectState observed_effect_{};
  CFXSyncControlState observed_controls_{};
#endif

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
  uint32_t udp_input_sent_{0};
  uint32_t udp_input_retried_{0};
  uint32_t udp_input_received_{0};
  uint32_t udp_input_applied_{0};
  uint32_t udp_state_sent_{0};
  uint32_t espnow_state_sent_{0};
};

}  // namespace cfx_sync
}  // namespace esphome

#endif  // defined(USE_ESP32) || defined(USE_ESP8266)
