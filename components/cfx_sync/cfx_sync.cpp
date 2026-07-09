#include "cfx_sync.h"

#if defined(USE_ESP32) || defined(USE_ESP8266)

#if defined(USE_ESP32)
#include "../cfx_button/cfx_dimmer_timing.h"
#endif
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#if defined(USE_ESP32) && defined(USE_WIFI)
#include "esphome/components/wifi/wifi_component.h"
#endif

#if defined(USE_ESP32)
#include <esp_err.h>
#include <esp_random.h>
#include <esp_wifi.h>
#elif defined(USE_ESP8266)
#include <Arduino.h>
static uint32_t esp_random() { return static_cast<uint32_t>(::random()); }
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <inttypes.h>
#include <limits>

namespace esphome {
namespace cfx_sync {

#if defined(USE_ESP32)
static CFXSyncTimingState capture_sync_timing_state(
    light::LightState *leader, const CFXSyncLightSnapshot &snapshot,
    const CFXSyncEffectState &effect, bool include_default_transition) {
  CFXSyncTimingState timing;
  const auto hint = cfx_dimmer::capture_light_timing_hint(leader, millis());
  timing.has_transition = hint.has_transition;
  timing.transition_ms = hint.transition_ms;
  timing.has_ramp = hint.has_ramp;
  timing.ramp_ms = hint.ramp_ms;
  if (hint.has_ramp) {
    return timing;
  }

  const bool should_include_default_transition =
      effect.kind == CFXSyncEffectKind::NONE || !snapshot.power;
  if (include_default_transition && leader != nullptr &&
      should_include_default_transition) {
    const uint32_t transition_ms = leader->get_default_transition_length();
    if (transition_ms > 0) {
      timing.has_transition = true;
      timing.transition_ms = static_cast<uint16_t>(
          std::min<uint32_t>(transition_ms,
                             std::numeric_limits<uint16_t>::max()));
    }
  }
  return timing;
}
#endif

static const char *const TAG = "cfx_sync";

const char *node_role_name(CFXSyncNodeRole role) {
  switch (role) {
    case CFXSyncNodeRole::LEADER:
      return "leader";
    case CFXSyncNodeRole::FOLLOWER:
      return "follower";
    case CFXSyncNodeRole::REMOTE:
      return "remote";
    case CFXSyncNodeRole::SATELLITE:
      return "satellite";
    default:
      return "unknown";
  }
}

class RemoteApplyGuard {
 public:
  explicit RemoteApplyGuard(bool &flag) : flag_(flag) { this->flag_ = true; }
  ~RemoteApplyGuard() { this->flag_ = false; }

 protected:
  bool &flag_;
};

void CFXSyncLightListener::on_light_remote_values_update() {
  if (this->parent_ != nullptr) {
    this->parent_->on_local_light_update();
  }
}

#if defined(USE_ESP32)
void CFXSyncEnableSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) {
    this->parent_->on_sync_enabled_switch(state);
  }
  this->publish_state(state);
}

light::LightState *CFXSyncComponent::leader_light_() const {
  if (this->role_ != CFXSyncRole::LEADER || this->lights_.size() != 1) {
    return nullptr;
  }
  return this->lights_[0];
}
#endif

bool CFXSyncComponent::use_udp_transport_() const {
  if (this->transport_ == CFXSyncTransport::CFX_SYNC_TRANSPORT_UDP) {
    return true;
  }
#if defined(USE_ESP32)
  return this->transport_ == CFXSyncTransport::CFX_SYNC_TRANSPORT_AUTO &&
         this->role_ == CFXSyncRole::LEADER;
#elif defined(USE_ESP8266)
  return this->transport_ == CFXSyncTransport::CFX_SYNC_TRANSPORT_AUTO;
#else
  return false;
#endif
}

bool CFXSyncComponent::use_espnow_transport_() const {
  if (this->transport_ == CFXSyncTransport::CFX_SYNC_TRANSPORT_ESPNOW) {
    return true;
  }
#if defined(USE_ESP8266)
  return false;
#elif defined(USE_ESP32)
  return this->transport_ == CFXSyncTransport::CFX_SYNC_TRANSPORT_AUTO;
#else
  return false;
#endif
}

CFXSyncNodeRole CFXSyncComponent::local_node_role_() const {
  if (this->role_ == CFXSyncRole::LEADER) {
    return CFXSyncNodeRole::LEADER;
  }
  if (this->role_ == CFXSyncRole::CFX_SYNC_ROLE_CONTROLLER) {
    return CFXSyncNodeRole::REMOTE;
  }
  if (this->role_ == CFXSyncRole::SATELLITE) {
    return CFXSyncNodeRole::SATELLITE;
  }
  return CFXSyncNodeRole::FOLLOWER;
}

uint16_t CFXSyncComponent::local_capabilities_() const {
  if (this->role_ == CFXSyncRole::LEADER) {
    return CFXSyncPacketCodec::CAP_LIGHT_LEADER;
  }
  if (this->role_ == CFXSyncRole::CFX_SYNC_ROLE_CONTROLLER) {
    return CFXSyncPacketCodec::CAP_BINARY_REMOTE;
  }
  if (this->role_ == CFXSyncRole::SATELLITE) {
    return CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER |
           CFXSyncPacketCodec::CAP_BINARY_REMOTE;
  }
  return CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER;
}

bool CFXSyncComponent::is_state_receiver_role_() const {
  return this->role_ == CFXSyncRole::FOLLOWER ||
         this->role_ == CFXSyncRole::SATELLITE;
}

bool CFXSyncComponent::is_input_sender_role_() const {
  return this->role_ == CFXSyncRole::CFX_SYNC_ROLE_CONTROLLER ||
         this->role_ == CFXSyncRole::SATELLITE;
}

bool CFXSyncComponent::accepts_peer_role_(CFXSyncNodeRole role) const {
  if (this->role_ == CFXSyncRole::LEADER) {
    return role == CFXSyncNodeRole::FOLLOWER ||
           role == CFXSyncNodeRole::REMOTE ||
           role == CFXSyncNodeRole::SATELLITE;
  }
  return role == CFXSyncNodeRole::LEADER;
}

bool CFXSyncComponent::should_send_state_for_hello_(
    const PeerState &peer, bool new_peer, bool peer_rebooted) const {
#if defined(USE_ESP32)
  if (this->role_ != CFXSyncRole::LEADER ||
      !this->peer_accepts_leader_state_(peer)) {
    return false;
  }
  return new_peer || peer_rebooted ||
         peer.last_state_sent_sequence == 0 ||
         this->has_pending_ack_(peer);
#else
  return false;
#endif
}

void CFXSyncComponent::set_peer(const std::array<uint8_t, 6> &peer) {
  this->peer_ = peer;
  this->has_static_peer_ = true;
  const CFXSyncNodeRole peer_role =
      this->role_ == CFXSyncRole::LEADER ? CFXSyncNodeRole::FOLLOWER
                                         : CFXSyncNodeRole::LEADER;
  const uint16_t peer_capabilities =
      peer_role == CFXSyncNodeRole::FOLLOWER
          ? CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER
          : CFXSyncPacketCodec::CAP_LIGHT_LEADER;
  this->find_or_add_peer_(this->peer_.data(), peer_role,
                          peer_capabilities);
}

void CFXSyncComponent::setup() {
  if (this->role_ == CFXSyncRole::CFX_SYNC_ROLE_CONTROLLER) {
    bool has_local_input = this->local_input_ != nullptr;
#if CFX_SYNC_HAS_CFX_BUTTON
    has_local_input = has_local_input || this->local_button_ != nullptr;
#endif
    if (!has_local_input) {
      ESP_LOGE(TAG, "Controller requires one local input");
      this->mark_failed();
      return;
    }
#if defined(USE_ESP32)
    if (!this->lights_.empty()) {
      ESP_LOGE(TAG, "Controller cannot declare light references");
      this->mark_failed();
      return;
    }
#endif
#if defined(USE_ESP8266)
    if (!this->use_udp_transport_()) {
      ESP_LOGE(TAG, "ESP8266 controller requires UDP transport");
      this->mark_failed();
      return;
    }
#endif
#if defined(USE_ESP32)
  } else if (this->lights_.empty()) {
    ESP_LOGE(TAG, "At least one light reference is required");
    this->mark_failed();
    return;
  }
  if (this->role_ == CFXSyncRole::LEADER && this->lights_.size() != 1) {
    ESP_LOGE(TAG, "Leader requires exactly one light reference");
    this->mark_failed();
    return;
  }
#elif defined(USE_ESP8266)
  } else if (this->is_state_receiver_role_()) {
    if (this->lights_.empty()) {
      ESP_LOGE(TAG, "%s requires at least one light reference",
               this->role_name_());
      this->mark_failed();
      return;
    }
    if (!this->use_udp_transport_()) {
      ESP_LOGE(TAG, "ESP8266 %s requires UDP transport",
               this->role_name_());
      this->mark_failed();
      return;
    }
  } else {
    ESP_LOGE(TAG,
             "ESP8266 cfx_sync support is controller or light follower over UDP");
    this->mark_failed();
    return;
  }
#endif

  this->boot_id_ = esp_random();
  if (this->boot_id_ == 0) {
    this->boot_id_ = 1;
  }
  this->bus_->register_group(this);

  bool transport_started = false;
  if (this->use_udp_transport_()) {
    if (this->transport_ == CFXSyncTransport::CFX_SYNC_TRANSPORT_AUTO) {
      ESP_LOGI(TAG, "CFX Sync transport auto selected UDP");
    }
    if (!this->bus_->begin_udp(this->udp_port_)) {
      ESP_LOGE(TAG, "UDP transport is required");
      this->mark_failed();
      return;
    }
    transport_started = true;
  }
  if (this->use_espnow_transport_()) {
#ifdef USE_ESPNOW
    if (this->transport_ == CFXSyncTransport::CFX_SYNC_TRANSPORT_AUTO) {
      ESP_LOGI(TAG, "CFX Sync transport auto selected ESP-NOW");
    }
    if (!this->bus_->has_espnow()) {
      ESP_LOGE(TAG, "ESP-NOW is required");
      this->mark_failed();
      return;
    }
    if (!this->bus_->begin_espnow()) {
      ESP_LOGE(TAG, "ESP-NOW is required");
      this->mark_failed();
      return;
    }
    transport_started = true;
#else
    ESP_LOGE(TAG, "ESP-NOW transport was not compiled");
    this->mark_failed();
    return;
#endif
  }
  if (!transport_started) {
    ESP_LOGE(TAG, "No supported CFX Sync transport is available");
    this->mark_failed();
    return;
  }
  this->boot_discovery_started_ms_ = millis();
  this->schedule_boot_discovery_();
  if (this->role_ != CFXSyncRole::LEADER) {
    this->schedule_follower_hello_();
  }
  if (this->has_static_peer_) {
    const CFXSyncNodeRole peer_role =
        this->role_ == CFXSyncRole::LEADER ? CFXSyncNodeRole::FOLLOWER
                                           : CFXSyncNodeRole::LEADER;
    const uint16_t peer_capabilities =
        peer_role == CFXSyncNodeRole::FOLLOWER
            ? CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER
            : CFXSyncPacketCodec::CAP_LIGHT_LEADER;
    if (auto *peer = this->find_or_add_peer_(this->peer_.data(), peer_role,
                                             peer_capabilities);
        peer != nullptr) {
      peer->registered = true;
    }
  }
#if defined(USE_ESP32)
  if (this->is_state_receiver_role_() && this->sync_switch_ != nullptr) {
    const auto initial_state =
        this->sync_switch_->get_initial_state_with_restore_mode();
    this->sync_enabled_ = initial_state.has_value() ? initial_state.value() : true;
    this->sync_switch_->publish_state(this->sync_enabled_);
  }

  if (this->role_ == CFXSyncRole::LEADER) {
    auto *leader = this->leader_light_();
    if (leader == nullptr || this->effect_catalogs_.empty()) {
      ESP_LOGE(TAG, "Leader light and effect catalog are required");
      this->mark_failed();
      return;
    }
    this->observed_state_ = capture_light_snapshot(*leader);
    this->observed_effect_ = capture_effect_state(
        this->lights_[0], this->effect_catalogs_[0]);
    this->observed_controls_ = capture_control_state_(0);
    this->has_observed_state_ = true;
    leader->add_remote_values_listener(&this->light_listener_);
    this->register_control_callbacks_(0);
    this->set_interval("heartbeat", this->heartbeat_ms_,
                       [this]() { this->send_heartbeat_state_(); });
  } else if (this->is_state_receiver_role_()) {
    this->schedule_follower_recovery_();
  }
#endif
  if (this->role_ == CFXSyncRole::SATELLITE && this->local_light_input_) {
    if (this->lights_.size() != 1 || this->lights_[0] == nullptr) {
      ESP_LOGE(TAG,
               "local_light_input requires a satellite with exactly one light");
      this->mark_failed();
      return;
    }
    this->observed_state_ = capture_light_snapshot(*this->lights_[0]);
    this->has_observed_state_ = true;
    this->lights_[0]->add_remote_values_listener(&this->light_listener_);
    ESP_LOGV(TAG, "Satellite local light input bound to %s",
             this->lights_[0]->get_name().c_str());
  }
  if (this->is_input_sender_role_() && this->local_input_ != nullptr) {
    if (this->local_input_->has_state()) {
      this->local_input_has_state_ = true;
      this->local_input_pressed_ = this->local_input_->state;
    }
    ESP_LOGV(TAG, "%s input bound to %s",
             this->role_ == CFXSyncRole::SATELLITE ? "Satellite"
                                                    : "Controller",
             this->local_input_->get_name().c_str());
    this->local_input_->add_on_state_callback(
        [this](bool pressed) { this->on_local_input_update_(pressed); });
  }
#if CFX_SYNC_HAS_CFX_BUTTON
  if (this->is_input_sender_role_() && this->local_button_ != nullptr) {
    ESP_LOGV(TAG, "%s CFX button input bound",
             this->role_ == CFXSyncRole::SATELLITE ? "Satellite"
                                                    : "Controller");
    this->local_button_->add_sync_command_callback(
        [this](const cfx_button::CFXButtonSyncCommand &command) {
          this->on_local_button_command_(command);
        });
  }
#endif
}

void CFXSyncComponent::loop() {
  if (this->use_udp_transport_()) {
    this->bus_->poll();
  }
  if (!this->use_espnow_transport_()) {
    return;
  }
#if defined(USE_ESP32)
  const uint8_t channel = this->current_wifi_channel_();
  const bool connected = channel != 0;
  const uint32_t now = millis();
  if (connected) {
    this->wifi_disconnected_since_ms_ = 0;
    if (this->offline_fallback_active_) {
      this->exit_offline_fallback_(channel);
    }
  }
  if (connected && !this->last_wifi_connected_) {
    this->pending_wifi_channel_ = 0;
    this->pending_wifi_channel_since_ms_ = 0;
    this->last_wifi_connected_ = connected;
    this->last_wifi_channel_ = channel;
    this->schedule_espnow_rearm_("wifi-channel");
    return;
  }
  if (connected && this->last_wifi_channel_ != 0 &&
      channel != this->last_wifi_channel_) {
    if (this->pending_wifi_channel_ != channel) {
      this->pending_wifi_channel_ = channel;
      this->pending_wifi_channel_since_ms_ = now;
      return;
    }
    if (now - this->pending_wifi_channel_since_ms_ <
        WIFI_CHANNEL_STABLE_MS) {
      return;
    }
    this->last_wifi_connected_ = connected;
    this->last_wifi_channel_ = channel;
    this->pending_wifi_channel_ = 0;
    this->pending_wifi_channel_since_ms_ = 0;
    this->schedule_espnow_rearm_("wifi-channel");
    return;
  }
  if (!connected) {
    this->pending_wifi_channel_ = 0;
    this->pending_wifi_channel_since_ms_ = 0;
    if (this->last_wifi_connected_ ||
        this->wifi_disconnected_since_ms_ == 0) {
      this->wifi_disconnected_since_ms_ = now;
    }
    if (!this->offline_fallback_active_ &&
        now - this->wifi_disconnected_since_ms_ >= WIFI_OFFLINE_GRACE_MS) {
      this->enter_offline_fallback_();
      return;
    }
  }
  this->pending_wifi_channel_ = 0;
  this->pending_wifi_channel_since_ms_ = 0;
  this->last_wifi_connected_ = connected;
  this->last_wifi_channel_ = channel;
#endif
}

void CFXSyncComponent::dump_config() {
  const uint32_t packet_age =
      this->last_valid_packet_ms_ == 0
          ? 0
          : millis() - this->last_valid_packet_ms_;

  ESP_LOGCONFIG(TAG,
                "CFX Sync:\n"
                "  Role: %s\n"
                "  Discovery: group authenticated\n"
                "  Group hash: %08" PRIX32 "\n"
                "  Heartbeat: %" PRIu32 " ms\n"
#if defined(USE_ESP32)
                "  Sync mode: %s\n"
                "  Fallback channel: %u\n"
                "  Active sync channel: %u\n"
#endif
                "  Last valid packet age: %" PRIu32 " ms\n"
                "  Peers: active=%u followers=%u remotes=%u\n"
                "  ACK: pending=%u missed=%" PRIu32,
                this->role_name_(), this->group_hash_,
                this->heartbeat_ms_,
#if defined(USE_ESP32)
                this->sync_mode_name_(),
                static_cast<unsigned>(this->fallback_channel_),
                static_cast<unsigned>(this->active_sync_channel_()),
#endif
                packet_age,
                static_cast<unsigned>(this->active_peer_count_()),
                static_cast<unsigned>(this->follower_peer_count_()),
                static_cast<unsigned>(this->remote_peer_count_()),
                static_cast<unsigned>(this->pending_ack_count_()),
                this->missed_ack_count_());
#if defined(USE_ESP8266)
  if (this->role_ == CFXSyncRole::CFX_SYNC_ROLE_CONTROLLER &&
      this->use_udp_transport_()) {
    ESP_LOGCONFIG(TAG, "  Transport: UDP (ESP8266 controller)");
  }
#endif
#if defined(USE_ESP32)
  ESP_LOGCONFIG(TAG, "  Lights: %u",
                static_cast<unsigned>(this->lights_.size()));
  ESP_LOGCONFIG(TAG, "  Wi-Fi channel: %u",
                static_cast<unsigned>(this->current_wifi_channel_()));
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *light = this->lights_[i];
    ESP_LOGCONFIG(TAG, "    [%u] %s", static_cast<unsigned>(i),
                  light != nullptr ? light->get_name().c_str() : "<null>");
  }
#endif
  ESP_LOGCONFIG(TAG,
                "  Packets: sent=%" PRIu32 " received=%" PRIu32
                " malformed=%" PRIu32 " auth_failed=%" PRIu32
                " wrong_group=%" PRIu32 " stale=%" PRIu32
                " unsupported=%" PRIu32 " send_failed=%" PRIu32,
                this->sent_packets_, this->received_packets_,
                this->malformed_packets_, this->authentication_failures_,
                this->wrong_group_packets_, this->stale_packets_,
                this->unsupported_packets_, this->send_failures_);
  ESP_LOGCONFIG(TAG, "  Transport: %s", this->transport_name_());
  ESP_LOGCONFIG(TAG,
                "  UDP input: sent=%" PRIu32 " retried=%" PRIu32
                " received=%" PRIu32 " applied=%" PRIu32,
                this->udp_input_sent_, this->udp_input_retried_,
                this->udp_input_received_, this->udp_input_applied_);
  ESP_LOGCONFIG(TAG,
                "  State fanout: espnow=%" PRIu32 " udp=%" PRIu32,
                this->espnow_state_sent_, this->udp_state_sent_);
}

bool CFXSyncComponent::handle_unknown_packet_(const CFXSyncSource &source,
                                              const uint8_t *data,
                                              size_t size) {
  CFXSyncPacket packet;
  const CFXSyncDecodeResult result = CFXSyncPacketCodec::decode(
      data, size, this->group_hash_, this->key_, packet);
  if (result == CFXSyncDecodeResult::NOT_CFX) {
    return false;
  }
  if (result == CFXSyncDecodeResult::WRONG_GROUP) {
    this->handle_decode_failure_(result);
    return false;
  }
  if (result != CFXSyncDecodeResult::OK) {
    this->handle_decode_failure_(result);
    return true;
  }
  if (packet.type != CFXSyncPacketType::HELLO &&
      packet.type != CFXSyncPacketType::SYNC_REQUEST &&
      packet.type != CFXSyncPacketType::STATE &&
      packet.type != CFXSyncPacketType::STATE_ACK &&
      packet.type != CFXSyncPacketType::INPUT_STATE &&
      packet.type != CFXSyncPacketType::LIGHT_COMMAND) {
    this->log_rejection_(
        "Ignoring authenticated non-discovery packet from unknown peer");
    return true;
  }

  return this->handle_decoded_packet_(source, packet);
}

bool CFXSyncComponent::handle_packet_(const CFXSyncSource &source,
                                      const uint8_t *data, size_t size) {
  CFXSyncPacket packet;
  const CFXSyncDecodeResult result = CFXSyncPacketCodec::decode(
      data, size, this->group_hash_, this->key_, packet);
  if (result == CFXSyncDecodeResult::NOT_CFX) {
    return false;
  }
  if (result == CFXSyncDecodeResult::WRONG_GROUP) {
    this->handle_decode_failure_(result);
    return false;
  }
  if (result != CFXSyncDecodeResult::OK) {
    this->handle_decode_failure_(result);
    return true;
  }

  return this->handle_decoded_packet_(source, packet);
}

bool CFXSyncComponent::handle_decoded_packet_(
    const CFXSyncSource &source, const CFXSyncPacket &packet) {
  if (!source.identity_valid) {
    this->log_rejection_("Ignoring packet without source identity");
    return true;
  }

  auto *peer = this->find_peer_(source);
  if (packet.type == CFXSyncPacketType::HELLO) {
    if (this->is_state_receiver_role_() &&
        (packet.node_role == CFXSyncNodeRole::FOLLOWER ||
         packet.node_role == CFXSyncNodeRole::SATELLITE)) {
      return true;
    }
    if (!this->accepts_peer_role_(packet.node_role)) {
      this->log_rejection_("Ignoring HELLO from incompatible role");
      return true;
    }
    const bool new_peer = peer == nullptr;
    const bool peer_rebooted =
        peer != nullptr && peer->has_rx_sequence &&
        peer->rx_boot_id != packet.boot_id;
    peer = this->find_or_add_peer_(source, packet.node_role,
                                   packet.capabilities);
    if (peer == nullptr) {
      return true;
    }
    if (!this->accept_sequence_(*peer, packet.boot_id, packet.sequence)) {
      this->stale_packets_++;
      this->log_rejection_("Ignoring duplicate or stale packet");
      return true;
    }
    this->register_peer_(*peer);
    this->received_packets_++;
    this->last_valid_packet_ms_ = millis();
#if defined(USE_ESP32)
    if (this->role_ == CFXSyncRole::LEADER &&
        this->should_send_state_for_hello_(*peer, new_peer,
                                           peer_rebooted)) {
      this->send_state_();
    }
#endif
    return true;
  }

  if (packet.type == CFXSyncPacketType::SYNC_REQUEST) {
#if defined(USE_ESP32)
    if (this->role_ != CFXSyncRole::LEADER) {
      this->log_rejection_("Ignoring SYNC_REQUEST for incompatible role");
      return true;
    }
    if (peer == nullptr) {
      peer = this->find_or_add_peer_(source, CFXSyncNodeRole::FOLLOWER,
                                     CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER);
    }
    if (peer == nullptr) {
      return true;
    }
    if (!this->accept_sequence_(*peer, packet.boot_id, packet.sequence)) {
      this->stale_packets_++;
      this->log_rejection_("Ignoring duplicate or stale packet");
      return true;
    }
    this->register_peer_(*peer);
    this->received_packets_++;
    this->last_valid_packet_ms_ = millis();
    if (this->role_ == CFXSyncRole::LEADER &&
        this->peer_accepts_leader_state_(*peer)) {
      this->send_state_();
    }
#endif
    return true;
  }

  if (peer == nullptr && packet.type == CFXSyncPacketType::STATE &&
      this->is_state_receiver_role_()) {
    peer = this->find_or_add_peer_(source, CFXSyncNodeRole::LEADER,
                                    CFXSyncPacketCodec::CAP_LIGHT_LEADER);
    if (peer != nullptr) {
      this->register_peer_(*peer);
    }
  }
#if defined(USE_ESP32)
  if (peer == nullptr && packet.type == CFXSyncPacketType::STATE &&
      this->role_ == CFXSyncRole::LEADER) {
    peer = this->find_or_add_peer_(
        source, CFXSyncNodeRole::SATELLITE,
        CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER |
            CFXSyncPacketCodec::CAP_BINARY_REMOTE);
    if (peer != nullptr) {
      this->register_peer_(*peer);
    }
  }
#endif
#if defined(USE_ESP32)
  if (peer == nullptr && packet.type == CFXSyncPacketType::STATE_ACK &&
      this->role_ == CFXSyncRole::LEADER &&
      this->is_current_broadcast_ack_(packet)) {
    peer = this->find_or_add_peer_(source, CFXSyncNodeRole::FOLLOWER,
                                   CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER);
    if (peer != nullptr) {
      this->register_peer_(*peer);
      this->seed_peer_sent_state_from_ack_(*peer, packet);
    }
  }
#endif
  if (packet.type == CFXSyncPacketType::STATE_ACK &&
      this->role_ != CFXSyncRole::LEADER) {
    return true;
  }

  if (packet.type == CFXSyncPacketType::INPUT_STATE) {
#if defined(USE_ESP32)
    if (this->role_ != CFXSyncRole::LEADER) {
      return true;
    }
    if (peer == nullptr) {
      peer = this->find_or_add_peer_(source, CFXSyncNodeRole::REMOTE,
                                     CFXSyncPacketCodec::CAP_BINARY_REMOTE);
      if (peer != nullptr) {
        this->register_peer_(*peer);
      }
    }
    if (peer == nullptr) {
      return true;
    }
    if (peer->node_role != CFXSyncNodeRole::REMOTE &&
        peer->node_role != CFXSyncNodeRole::SATELLITE &&
        (peer->capabilities & CFXSyncPacketCodec::CAP_BINARY_REMOTE) == 0) {
      return true;
    }
    peer->last_seen_ms = millis();
    if (!this->accept_sequence_(*peer, packet.boot_id, packet.sequence)) {
      this->stale_packets_++;
      this->log_rejection_("Ignoring duplicate or stale packet");
      return true;
    }
    this->received_packets_++;
    this->last_valid_packet_ms_ = millis();
    const bool applied = this->handle_remote_input_(
        *peer, packet.input_pressed, packet.input_maintained,
        packet.input_toggle, packet.input_action);
    if (source.transport == CFXSyncTransportKind::UDP) {
      this->udp_input_received_++;
    }
    if (source.transport == CFXSyncTransportKind::UDP && applied) {
      this->udp_input_applied_++;
      ESP_LOGV(TAG, "UDP input applied");
    }
#endif
    return true;
  }

  if (packet.type == CFXSyncPacketType::LIGHT_COMMAND) {
#if defined(USE_ESP32)
    if (this->role_ != CFXSyncRole::LEADER) {
      this->log_rejection_("Ignoring remote light command on non-leader");
      return true;
    }
    if (peer == nullptr) {
      peer = this->find_or_add_peer_(source, CFXSyncNodeRole::REMOTE,
                                     CFXSyncPacketCodec::CAP_BINARY_REMOTE);
      if (peer != nullptr) {
        this->register_peer_(*peer);
      }
    }
    if (peer == nullptr) {
      return true;
    }
    if (peer->node_role != CFXSyncNodeRole::REMOTE &&
        peer->node_role != CFXSyncNodeRole::SATELLITE &&
        (peer->capabilities & CFXSyncPacketCodec::CAP_BINARY_REMOTE) == 0) {
      return true;
    }
    peer->last_seen_ms = millis();
    if (!this->accept_sequence_(*peer, packet.boot_id, packet.sequence)) {
      this->stale_packets_++;
      this->log_rejection_("Ignoring duplicate or stale packet");
      return true;
    }
    this->received_packets_++;
    this->last_valid_packet_ms_ = millis();
    const bool applied = this->handle_remote_light_command_(*peer, packet);
    if (source.transport == CFXSyncTransportKind::UDP) {
      this->udp_input_received_++;
    }
    if (source.transport == CFXSyncTransportKind::UDP && applied) {
      this->udp_input_applied_++;
      ESP_LOGV(TAG, "UDP input applied");
    }
#else
    (void) source;
#endif
    return true;
  }

  if (peer == nullptr) {
    this->log_rejection_("Ignoring authenticated packet from unknown peer");
    return true;
  }

  peer->last_seen_ms = millis();
  if (!this->accept_sequence_(*peer, packet.boot_id, packet.sequence)) {
    if (packet.type == CFXSyncPacketType::STATE &&
        this->is_state_receiver_role_() &&
        peer->has_rx_sequence &&
        packet.boot_id == peer->rx_boot_id &&
        packet.sequence == peer->rx_sequence) {
      this->schedule_state_ack_(source.espnow_mac_or_null(), packet,
                                CFXSyncAckResult::APPLIED);
      return true;
    }
    this->stale_packets_++;
    this->log_rejection_("Ignoring duplicate or stale packet");
    return true;
  }

  this->received_packets_++;
  this->last_valid_packet_ms_ = millis();

  if (packet.type == CFXSyncPacketType::STATE_ACK) {
#if defined(USE_ESP32)
    if (this->role_ == CFXSyncRole::LEADER) {
      this->handle_state_ack_(*peer, packet);
    }
#endif
    return true;
  }

#if defined(USE_ESP32)
  if (this->role_ == CFXSyncRole::LEADER &&
      packet.type == CFXSyncPacketType::STATE &&
      this->handle_satellite_state_proposal_(*peer, packet)) {
    return true;
  }
#endif

  if (packet.type == CFXSyncPacketType::STATE &&
      this->is_state_receiver_role_() &&
      !this->sync_enabled_) {
    this->log_rejection_("Ignoring STATE while sync is disabled");
    return true;
  }

  if (packet.type == CFXSyncPacketType::STATE &&
      this->is_state_receiver_role_() &&
      (packet.has_power || packet.has_brightness || packet.has_color ||
       packet.has_color_brightness || packet.has_effect ||
       packet.has_controls || packet.has_color_temperature ||
       packet.has_cold_warm_white)) {
    this->has_valid_state_ = true;
    this->clear_warning_if_set_();
    const bool applied = this->apply_remote_state_(packet);
    if (this->role_ == CFXSyncRole::SATELLITE && applied) {
      if (this->local_light_input_ && this->lights_.size() == 1 &&
          this->lights_[0] != nullptr) {
        this->observed_state_ = capture_light_snapshot(*this->lights_[0]);
        this->has_observed_state_ = true;
      }
      ESP_LOGV(TAG, "Satellite applying leader state");
    }
    this->schedule_state_ack_(source.espnow_mac_or_null(), packet,
                              CFXSyncAckResult::APPLIED);
  }
  return true;
}

void CFXSyncComponent::on_local_light_update() {
  if (this->applying_remote_state_) {
    return;
  }
  if (this->role_ == CFXSyncRole::SATELLITE && this->local_light_input_) {
    this->send_satellite_local_state_();
    return;
  }
#if defined(USE_ESP32)
  auto *leader = this->leader_light_();
  if (leader == nullptr || this->effect_catalogs_.empty()) {
    return;
  }

  const auto snapshot = capture_light_snapshot(*leader);
  const auto effect = capture_effect_state(
      leader, this->effect_catalogs_[0]);
  const auto controls = this->capture_control_state_(0);
  if (this->has_observed_state_ && snapshot == this->observed_state_ &&
      effect == this->observed_effect_ &&
      controls == this->observed_controls_) {
    return;
  }
  this->has_observed_state_ = true;
  this->observed_state_ = snapshot;
  this->observed_effect_ = effect;
  this->observed_controls_ = controls;
  this->send_state_(snapshot, effect, controls, true);
#endif
}

void CFXSyncComponent::on_sync_enabled_switch(bool enabled) {
#if defined(USE_ESP32)
  this->sync_enabled_ = enabled;
  if (!this->is_state_receiver_role_()) {
    return;
  }
  this->has_valid_state_ = false;
  this->clear_warning_if_set_();
  if (enabled) {
    this->schedule_enable_resync_();
  }
#else
  this->sync_enabled_ = enabled;
#endif
}

#if defined(USE_ESP32)
void CFXSyncComponent::on_local_control_update() {
  this->on_local_light_update();
}

bool CFXSyncComponent::send_state_(bool include_default_transition) {
  auto *leader = this->leader_light_();
  if (leader == nullptr || this->effect_catalogs_.empty()) {
    return false;
  }
  const auto snapshot = capture_light_snapshot(*leader);
  const auto effect = capture_effect_state(leader, this->effect_catalogs_[0]);
  const auto controls = this->capture_control_state_(0);
  return this->send_state_to_followers_(snapshot, effect, controls,
                                        include_default_transition);
}

bool CFXSyncComponent::send_state_(
    const CFXSyncLightSnapshot &snapshot,
    const CFXSyncEffectState &effect,
    const CFXSyncControlState &controls,
    bool include_default_transition) {
  return this->send_state_to_followers_(snapshot, effect, controls,
                                        include_default_transition);
}

bool CFXSyncComponent::send_heartbeat_state_() {
  return this->send_state_();
}

bool CFXSyncComponent::send_state_to_followers_() {
  auto *leader = this->leader_light_();
  if (leader == nullptr || this->effect_catalogs_.empty()) {
    return false;
  }
  const auto snapshot = capture_light_snapshot(*leader);
  const auto effect = capture_effect_state(leader, this->effect_catalogs_[0]);
  const auto controls = this->capture_control_state_(0);
  return this->send_state_to_followers_(snapshot, effect, controls, false);
}

bool CFXSyncComponent::send_state_to_followers_(
    const CFXSyncLightSnapshot &snapshot,
    const CFXSyncEffectState &effect,
    const CFXSyncControlState &controls,
    bool include_default_transition) {
  auto *leader = this->leader_light_();
  const auto timing = capture_sync_timing_state(
      leader, snapshot, effect, include_default_transition);
  return this->send_state_to_followers_(snapshot, effect, controls, timing);
}

bool CFXSyncComponent::send_state_to_followers_(
    const CFXSyncLightSnapshot &snapshot,
    const CFXSyncEffectState &effect,
    const CFXSyncControlState &controls,
    const CFXSyncTimingState &timing) {
  if (this->send_pending_) {
    this->state_send_deferred_ = true;
    this->state_send_deferred_with_transition_ =
        this->state_send_deferred_with_transition_ ||
        timing.has_transition || timing.has_ramp;
    return false;
  }

  std::vector<uint8_t> packet;
  const uint32_t sequence = this->next_sequence_();
  if (!CFXSyncPacketCodec::encode_state_snapshot(
          this->group_hash_, this->boot_id_, sequence, snapshot, true, effect,
          controls.has_any(), controls, timing, this->key_, packet)) {
    return false;
  }
  if (!this->send_state_packet_to_followers_(packet)) {
    return false;
  }
  this->last_state_retry_packet_ = packet;
  this->last_state_retry_packet_valid_ = true;
  if (!this->state_retry_active_) {
    this->state_retry_attempts_ = 0;
  }
  this->mark_state_sent_to_followers_(sequence);
  this->schedule_state_retry_();
  return true;
}

void CFXSyncComponent::mark_state_sent_to_followers_(uint32_t sequence) {
  const uint32_t now = millis();
  this->last_broadcast_state_boot_id_ = this->boot_id_;
  this->last_broadcast_state_sequence_ = sequence;
  this->last_broadcast_state_ms_ = now;
  for (auto &peer : this->peers_) {
    if (!this->peer_accepts_leader_state_(peer)) {
      continue;
    }
    peer.last_state_sent_boot_id = this->boot_id_;
    peer.last_state_sent_sequence = sequence;
    peer.last_state_sent_ms = now;
  }
}

bool CFXSyncComponent::peer_accepts_leader_state_(
    const PeerState &peer) const {
  return peer.active && peer.registered &&
         (peer.node_role == CFXSyncNodeRole::FOLLOWER ||
          peer.node_role == CFXSyncNodeRole::SATELLITE) &&
         (peer.capabilities & CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER);
}

bool CFXSyncComponent::send_state_to_peer_(PeerState &peer) {
  auto *leader = this->leader_light_();
  if (leader == nullptr || this->effect_catalogs_.empty()) {
    return false;
  }

  const auto snapshot = capture_light_snapshot(*leader);
  const auto effect = capture_effect_state(leader, this->effect_catalogs_[0]);
  const auto controls = this->capture_control_state_(0);
  return this->send_state_to_peer_(peer, snapshot, effect, controls);
}

bool CFXSyncComponent::send_state_to_peer_(
    PeerState &peer, const CFXSyncLightSnapshot &snapshot,
    const CFXSyncEffectState &effect,
    const CFXSyncControlState &controls) {
  if (this->send_pending_) {
    this->state_send_deferred_ = true;
    return false;
  }

  std::vector<uint8_t> packet;
  auto *leader = this->leader_light_();
  const auto timing = capture_sync_timing_state(leader, snapshot, effect, false);
  const uint32_t sequence = this->next_sequence_();
  if (!CFXSyncPacketCodec::encode_state_snapshot(
          this->group_hash_, this->boot_id_, sequence, snapshot, true, effect,
          controls.has_any(), controls, timing, this->key_, packet)) {
    return false;
  }
  if (!this->send_packet_to_peer_(peer, packet)) {
    return false;
  }
  peer.last_state_sent_boot_id = this->boot_id_;
  peer.last_state_sent_sequence = sequence;
  peer.last_state_sent_ms = millis();
  return true;
}

bool CFXSyncComponent::handle_satellite_state_proposal_(
    PeerState &peer, const CFXSyncPacket &packet) {
  if (this->role_ != CFXSyncRole::LEADER ||
      packet.type != CFXSyncPacketType::STATE ||
      peer.node_role != CFXSyncNodeRole::SATELLITE ||
      (peer.capabilities & CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER) == 0) {
    return false;
  }
  if (!(packet.has_power || packet.has_brightness || packet.has_color ||
        packet.has_color_brightness || packet.has_color_temperature ||
        packet.has_cold_warm_white)) {
    return true;
  }

  const bool applied = this->apply_remote_state_(packet);
  if (applied) {
    ESP_LOGV(TAG, "Leader applied satellite local light state");
    this->send_state_();
  }
  return true;
}
#endif

bool CFXSyncComponent::send_satellite_local_state_() {
  if (this->role_ != CFXSyncRole::SATELLITE || !this->local_light_input_ ||
      this->lights_.size() != 1 || this->lights_[0] == nullptr ||
      this->applying_remote_state_) {
    return false;
  }

  auto *light = this->lights_[0];
  const auto snapshot = capture_light_snapshot(*light);
  if (this->has_observed_state_ && snapshot == this->observed_state_) {
    return false;
  }
  this->has_observed_state_ = true;
  this->observed_state_ = snapshot;

  CFXSyncEffectState effect;
  CFXSyncControlState controls;
  CFXSyncTimingState timing;
  std::vector<uint8_t> packet;
  if (!CFXSyncPacketCodec::encode_state_snapshot(
          this->group_hash_, this->boot_id_, this->next_sequence_(),
          snapshot, false, effect, false, controls, timing, this->key_,
          packet)) {
    return false;
  }
  ESP_LOGV(TAG, "Satellite sending local light state");
  return this->send_satellite_state_packet_(packet);
}

bool CFXSyncComponent::send_satellite_state_packet_(
    std::vector<uint8_t> &packet) {
  bool sent = false;
  if (this->use_udp_transport_()) {
    for (auto &peer : this->peers_) {
      if (!peer.active || peer.node_role != CFXSyncNodeRole::LEADER ||
          peer.transport != CFXSyncTransportKind::UDP) {
        continue;
      }
      sent = this->send_packet_to_peer_(peer, packet) || sent;
    }
  }
  return this->send_packet_to_(BROADCAST_MAC, packet) || sent;
}

bool CFXSyncComponent::send_state_ack_(const uint8_t *destination,
                                       const CFXSyncPacket &packet,
                                       CFXSyncAckResult result) {
  (void) destination;

  std::vector<uint8_t> ack;
  if (!CFXSyncPacketCodec::encode_state_ack(
          this->group_hash_, this->boot_id_, this->next_sequence_(),
          packet.boot_id, packet.sequence, result, this->key_, ack)) {
    return false;
  }
  return this->send_packet_to_(BROADCAST_MAC, ack);
}

void CFXSyncComponent::schedule_state_ack_(const uint8_t *destination,
                                           const CFXSyncPacket &packet,
                                           CFXSyncAckResult result) {
  (void) destination;
  const uint32_t acked_boot_id = packet.boot_id;
  const uint32_t acked_sequence = packet.sequence;
  const uint32_t delay_ms =
      ACK_JITTER_MIN_MS + (esp_random() % (ACK_JITTER_SPREAD_MS + 1));
  this->set_timeout(
      "state-ack", delay_ms,
      [this, acked_boot_id, acked_sequence, result]() {
        std::vector<uint8_t> ack;
        if (!CFXSyncPacketCodec::encode_state_ack(
                this->group_hash_, this->boot_id_, this->next_sequence_(),
                acked_boot_id, acked_sequence, result, this->key_, ack)) {
          return;
        }
        this->send_packet_to_(BROADCAST_MAC, ack);
      });
}

bool CFXSyncComponent::send_sync_request_() {
  if (!this->has_static_peer_) {
    return false;
  }
  return this->send_sync_request_to_(this->peer_);
}

bool CFXSyncComponent::send_sync_request_to_(
    const std::array<uint8_t, 6> &mac) {
  if (!this->is_state_receiver_role_()) {
    return false;
  }

  std::vector<uint8_t> packet;
  if (!CFXSyncPacketCodec::encode_sync_request(
          this->group_hash_, this->boot_id_, this->next_sequence_(),
          this->key_, packet)) {
    return false;
  }
  return this->send_packet_to_(mac, packet);
}

bool CFXSyncComponent::send_hello_() {
  std::vector<uint8_t> packet;
  if (!CFXSyncPacketCodec::encode_hello(
          this->group_hash_, this->boot_id_, this->next_sequence_(),
          this->local_node_role_(), this->local_capabilities_(), this->key_,
          packet)) {
    return false;
  }
  return this->send_packet_to_(BROADCAST_MAC, packet);
}

bool CFXSyncComponent::send_input_packet_(std::vector<uint8_t> &packet) {
  bool sent = false;
  if (this->use_udp_transport_()) {
    for (auto &peer : this->peers_) {
      if (!peer.active || peer.node_role != CFXSyncNodeRole::LEADER ||
          peer.transport != CFXSyncTransportKind::UDP) {
        continue;
      }
      sent = this->send_packet_to_peer_(peer, packet) || sent;
    }
  }
  return this->send_packet_to_(BROADCAST_MAC, packet) || sent;
}

bool CFXSyncComponent::send_input_state_(bool pressed, bool maintained,
                                         bool toggle,
                                         CFXSyncInputAction action) {
  if (!this->is_input_sender_role_()) {
    return false;
  }
  if (this->send_pending_) {
    this->queue_input_state_(pressed, maintained, toggle, action);
    return false;
  }
  std::vector<uint8_t> packet;
  const uint32_t sequence = this->next_sequence_();
  if (!CFXSyncPacketCodec::encode_input_state(
          this->group_hash_, this->boot_id_, sequence,
          pressed, maintained, toggle, action, this->key_, packet)) {
    return false;
  }
  const char *action_name = "primary";
  if (action == CFXSyncInputAction::DIMMER_UP) {
    action_name = "dimmer-up";
  } else if (action == CFXSyncInputAction::DIMMER_DOWN) {
    action_name = "dimmer-down";
  }
  ESP_LOGV(TAG, "Sending CFX Sync input %s %s%s%s",
           action_name,
           pressed ? "pressed" : "released",
           maintained ? " maintained" : "",
           toggle ? " toggle" : "");
  const bool sent = this->send_input_packet_(packet);
  if (sent && this->use_udp_transport_()) {
    this->udp_input_sent_++;
    this->schedule_udp_input_retry_(packet, UDP_INPUT_RETRY_COUNT);
  }
  return sent;
}

void CFXSyncComponent::schedule_udp_input_retry_(std::vector<uint8_t> packet,
                                                 uint8_t remaining) {
  if (!this->use_udp_transport_() || remaining == 0) {
    return;
  }
  this->set_timeout("udp-input-retry", UDP_INPUT_RETRY_DELAY_MS,
                    [this, packet, remaining]() mutable {
                      if (!this->use_udp_transport_()) {
                        return;
                      }
                      if (this->send_input_packet_(packet)) {
                        this->udp_input_retried_++;
                        ESP_LOGV(TAG, "UDP input burst resend");
                      }
                      if (remaining > 1) {
                        this->schedule_udp_input_retry_(packet, remaining - 1);
                      }
                    });
}

void CFXSyncComponent::queue_input_state_(bool pressed, bool maintained,
                                          bool toggle,
                                          CFXSyncInputAction action) {
  if (this->pending_input_count_ >= PENDING_INPUT_QUEUE_SIZE) {
    this->pending_input_head_ =
        (this->pending_input_head_ + 1) % PENDING_INPUT_QUEUE_SIZE;
    this->pending_input_count_--;
    ESP_LOGW(TAG, "CFX Sync input queue full; dropping oldest input edge");
  }
  const uint8_t index =
      (this->pending_input_head_ + this->pending_input_count_) %
      PENDING_INPUT_QUEUE_SIZE;
  this->pending_input_events_[index] =
      PendingInputEvent{pressed, maintained, toggle, action};
  this->pending_input_count_++;
  ESP_LOGV(TAG, "Queued CFX Sync input %s%s%s",
           pressed ? "pressed" : "released",
           maintained ? " maintained" : "",
           toggle ? " toggle" : "");
}

void CFXSyncComponent::flush_deferred_input_() {
  if (!this->is_input_sender_role_() ||
      this->send_pending_ || this->pending_input_count_ == 0) {
    return;
  }
  const auto event = this->pending_input_events_[this->pending_input_head_];
  this->pending_input_head_ =
      (this->pending_input_head_ + 1) % PENDING_INPUT_QUEUE_SIZE;
  this->pending_input_count_--;
  this->send_input_state_(event.pressed, event.maintained, event.toggle,
                          event.action);
}

void CFXSyncComponent::on_local_input_update_(bool pressed) {
  if (!this->is_input_sender_role_()) {
    return;
  }
  if (this->local_input_has_state_ &&
      this->local_input_pressed_ == pressed) {
    return;
  }
  this->local_input_has_state_ = true;
  this->local_input_pressed_ = pressed;
  ESP_LOGV(TAG, "%s local input %s",
           this->role_ == CFXSyncRole::SATELLITE ? "Satellite" : "Controller",
           pressed ? "pressed" : "released");
  const bool maintained =
      this->input_mode_ == CFXSyncInputMode::CFX_SYNC_INPUT_MAINTAINED;
  const bool toggle =
      this->input_mode_ == CFXSyncInputMode::CFX_SYNC_INPUT_TOGGLE;
  if (maintained || toggle) {
    const uint32_t generation = ++this->local_input_maintained_generation_;
    this->set_timeout("input-maintained-settle",
                      INPUT_MAINTAINED_SETTLE_MS,
                      [this, pressed, maintained, toggle, generation]() {
                        if (!this->is_input_sender_role_() ||
                            !((maintained &&
                               this->input_mode_ ==
                                   CFXSyncInputMode::
                                       CFX_SYNC_INPUT_MAINTAINED) ||
                              (toggle &&
                               this->input_mode_ ==
                                   CFXSyncInputMode::CFX_SYNC_INPUT_TOGGLE)) ||
                            generation !=
                                this->local_input_maintained_generation_ ||
                            !this->local_input_has_state_ ||
                            this->local_input_pressed_ != pressed) {
                          return;
                        }
                        if (this->local_input_sent_has_state_ &&
                            this->local_input_sent_pressed_ == pressed) {
                          return;
                        }
                        this->local_input_sent_has_state_ = true;
                        this->local_input_sent_pressed_ = pressed;
                        this->send_input_state_(
                            pressed, maintained, toggle,
                            CFXSyncInputAction::PRIMARY);
                      });
    return;
  }
  this->local_input_sent_has_state_ = true;
  this->local_input_sent_pressed_ = pressed;
  const uint32_t repeat_generation =
      ++this->local_input_repeat_generation_;
  this->send_input_state_(pressed, maintained, false,
                          CFXSyncInputAction::PRIMARY);
  if (pressed) {
    this->schedule_local_input_hold_repeat_(repeat_generation);
  } else {
    this->schedule_local_input_release_repeat_(INPUT_RELEASE_REPEAT_COUNT,
                                               repeat_generation);
  }
}

#if CFX_SYNC_HAS_CFX_BUTTON
void CFXSyncComponent::on_local_button_update_(
    cfx_button::CFXButtonInputAction action, bool pressed) {
  if (!this->is_input_sender_role_()) {
    return;
  }
  CFXSyncInputAction sync_action = CFXSyncInputAction::PRIMARY;
  switch (action) {
    case cfx_button::CFXButtonInputAction::PRIMARY:
      sync_action = CFXSyncInputAction::PRIMARY;
      break;
    case cfx_button::CFXButtonInputAction::DIMMER_UP:
      sync_action = CFXSyncInputAction::DIMMER_UP;
      break;
    case cfx_button::CFXButtonInputAction::DIMMER_DOWN:
      sync_action = CFXSyncInputAction::DIMMER_DOWN;
      break;
    default:
      return;
  }
  this->send_input_state_(pressed, false, false, sync_action);
}

void CFXSyncComponent::on_local_button_command_(
    const cfx_button::CFXButtonSyncCommand &command) {
  if (!this->is_input_sender_role_()) {
    return;
  }
  this->send_light_command_(command);
}

bool CFXSyncComponent::send_light_command_(
    const cfx_button::CFXButtonSyncCommand &command) {
  if (!this->is_input_sender_role_() || this->send_pending_) {
    return false;
  }

  CFXSyncPacket packet;
  switch (command.kind) {
    case cfx_button::CFXButtonSyncKind::BINARY:
      packet.command_kind = CFXSyncCommandKind::BINARY;
      break;
    case cfx_button::CFXButtonSyncKind::DIMMER:
      packet.command_kind = CFXSyncCommandKind::DIMMER;
      break;
    case cfx_button::CFXButtonSyncKind::HUE:
      packet.command_kind = CFXSyncCommandKind::HUE;
      break;
    case cfx_button::CFXButtonSyncKind::CCT:
      packet.command_kind = CFXSyncCommandKind::CCT;
      break;
    case cfx_button::CFXButtonSyncKind::EFFECT:
    default:
      packet.command_kind = CFXSyncCommandKind::EFFECT;
      packet.command_mask = CFXSyncPacketCodec::COMMAND_EFFECT;
      break;
  }

  if (command.pressed) {
    packet.command_flags |= CFXSyncPacketCodec::COMMAND_FLAG_PRESSED;
  } else {
    packet.command_flags |= CFXSyncPacketCodec::COMMAND_FLAG_RELEASED;
  }
  if (command.direction_up) {
    packet.command_flags |= CFXSyncPacketCodec::COMMAND_FLAG_DIRECTION_UP;
  }
  if (command.direction_down) {
    packet.command_flags |= CFXSyncPacketCodec::COMMAND_FLAG_DIRECTION_DOWN;
  }

  if (command.has_power) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_POWER;
    packet.command_power = command.power;
  }
  if (command.toggle) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_TOGGLE;
  }
  if (command.has_brightness) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_BRIGHTNESS;
    packet.command_brightness = command.brightness;
  }
  if (command.has_ramp) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_RAMP;
    packet.command_ramp_ms = command.ramp_ms;
  }
  if (command.has_rgb) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_RGB;
    packet.command_red = command.red;
    packet.command_green = command.green;
    packet.command_blue = command.blue;
  }
  if (command.has_white) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_WHITE;
    packet.command_white = command.white;
  }
  if (command.has_color_brightness) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_COLOR_BRIGHTNESS;
    packet.command_color_brightness = command.color_brightness;
  }
  if (command.has_color_temperature) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_COLOR_TEMPERATURE;
    packet.command_color_temperature_mireds =
        command.color_temperature_mireds;
  }
  if (command.has_cold_warm_white) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_COLD_WARM_WHITE;
    packet.command_cold_white = command.cold_white;
    packet.command_warm_white = command.warm_white;
  }

  if (packet.command_kind == CFXSyncCommandKind::DIMMER &&
      (packet.command_flags &
       CFXSyncPacketCodec::COMMAND_FLAG_RELEASED) != 0 &&
      (packet.command_flags &
       (CFXSyncPacketCodec::COMMAND_FLAG_DIRECTION_UP |
        CFXSyncPacketCodec::COMMAND_FLAG_DIRECTION_DOWN)) != 0 &&
      packet.command_mask == 0) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_RAMP;
    packet.command_ramp_ms = 0;
  }
  if (packet.command_kind == CFXSyncCommandKind::DIMMER &&
      command.action == cfx_button::CFXButtonInputAction::PRIMARY &&
      command.pressed && packet.command_mask == 0) {
    packet.command_mask |= CFXSyncPacketCodec::COMMAND_TOGGLE;
  }

  std::vector<uint8_t> wire_packet;
  const uint32_t sequence = this->next_sequence_();
  if (!CFXSyncPacketCodec::encode_light_command(
          this->group_hash_, this->boot_id_, sequence, packet, this->key_,
          wire_packet)) {
    ESP_LOGW(TAG, "CFX Sync light command could not be encoded");
    return false;
  }
  ESP_LOGV(TAG, "Sending CFX Sync resolved command kind=%u mask=%04X",
           static_cast<unsigned>(packet.command_kind),
           static_cast<unsigned>(packet.command_mask));
  const bool sent = this->send_input_packet_(wire_packet);
  if (sent && this->use_udp_transport_()) {
    this->udp_input_sent_++;
    this->schedule_udp_input_retry_(wire_packet, UDP_INPUT_RETRY_COUNT);
  }
  return sent;
}
#endif

void CFXSyncComponent::schedule_local_input_hold_repeat_(
    uint32_t generation) {
  this->set_timeout("input-hold-repeat", INPUT_HOLD_REPEAT_MS,
                    [this, generation]() {
    if (!this->is_input_sender_role_() ||
        generation != this->local_input_repeat_generation_ ||
        !this->local_input_pressed_) {
      return;
    }
    this->send_input_state_(true, false, false,
                            CFXSyncInputAction::PRIMARY);
    this->schedule_local_input_hold_repeat_(generation);
  });
}

void CFXSyncComponent::schedule_local_input_release_repeat_(
    uint8_t remaining, uint32_t generation) {
  if (remaining == 0) {
    return;
  }
  this->set_timeout("input-release-repeat", INPUT_RELEASE_REPEAT_MS,
                    [this, remaining, generation]() {
                      if (!this->is_input_sender_role_() ||
                          generation != this->local_input_repeat_generation_ ||
                          this->local_input_pressed_) {
                        return;
                      }
                      this->send_input_state_(
                          false, false, false, CFXSyncInputAction::PRIMARY);
                      this->schedule_local_input_release_repeat_(
                          remaining - 1, generation);
                    });
}

bool CFXSyncComponent::inject_remote_input_(bool pressed, bool maintained,
                                           bool toggle,
                                           CFXSyncInputAction action) {
#if defined(USE_ESP32)
  if (this->role_ != CFXSyncRole::LEADER) {
    return false;
  }
  if (action != CFXSyncInputAction::PRIMARY && (maintained || toggle)) {
    return false;
  }
  if (toggle) {
    this->apply_remote_toggle_input_();
    return true;
  }
  if (maintained) {
    this->apply_remote_power_input_(pressed);
    return true;
  }
#if CFX_SYNC_HAS_CFX_BUTTON
  if (this->remote_input_ == nullptr) {
    if (pressed) {
      this->apply_remote_toggle_input_();
      return true;
    }
    return false;
  }
  if (!pressed && !this->remote_input_pressed_) {
    ESP_LOGV(TAG, "Ignoring duplicate CFX Sync remote release");
    return false;
  }
  this->remote_input_pressed_ = pressed;
  this->remote_input_action_ = pressed ? action : CFXSyncInputAction::PRIMARY;
  this->last_remote_input_ms_ = millis();
  if (action == CFXSyncInputAction::DIMMER_UP) {
    this->remote_input_->inject_remote_dimmer_up(pressed);
    if (pressed) {
      this->schedule_remote_input_timeout_();
    }
    return true;
  }
  if (action == CFXSyncInputAction::DIMMER_DOWN) {
    this->remote_input_->inject_remote_dimmer_down(pressed);
    if (pressed) {
      this->schedule_remote_input_timeout_();
    }
    return true;
  }
  ESP_LOGV(TAG, "Applying CFX Sync remote input %s",
           pressed ? "pressed" : "released");
  this->remote_input_->inject_remote_state(pressed);
  if (pressed && !maintained) {
    this->schedule_remote_input_timeout_();
  }
  return true;
#else
  if (action != CFXSyncInputAction::PRIMARY) {
    return false;
  }
  if (pressed) {
    this->apply_remote_toggle_input_();
    return true;
  }
  return false;
#endif
#else
  (void) pressed;
  (void) maintained;
  (void) toggle;
  return false;
#endif
}

#if defined(USE_ESP32)
bool CFXSyncComponent::handle_remote_input_(PeerState &peer, bool pressed,
                                           bool maintained, bool toggle,
                                           CFXSyncInputAction action) {
  if (toggle || maintained) {
    return this->inject_remote_input_(pressed, maintained, toggle, action);
  }
  if (pressed) {
    if (this->remote_input_owner_ == nullptr) {
      this->remote_input_owner_ = &peer;
    } else if (this->remote_input_owner_ != &peer) {
      this->log_rejection_(
          "Ignoring remote input while another controller is active");
      return false;
    }
    return this->inject_remote_input_(pressed, maintained, toggle, action);
  }
  if (this->remote_input_owner_ != nullptr &&
      this->remote_input_owner_ != &peer) {
    this->log_rejection_(
        "Ignoring remote input while another controller is active");
    return false;
  }
  const bool applied =
      this->inject_remote_input_(pressed, maintained, toggle, action);
  this->clear_remote_input_owner_();
  return applied;
}

bool CFXSyncComponent::handle_remote_light_command_(
    PeerState &peer, const CFXSyncPacket &packet) {
  const bool pressed =
      (packet.command_flags & CFXSyncPacketCodec::COMMAND_FLAG_PRESSED) != 0;
  const bool released =
      (packet.command_flags & CFXSyncPacketCodec::COMMAND_FLAG_RELEASED) != 0;
  const bool directional =
      (packet.command_flags &
       (CFXSyncPacketCodec::COMMAND_FLAG_DIRECTION_UP |
        CFXSyncPacketCodec::COMMAND_FLAG_DIRECTION_DOWN)) != 0;
  const bool hold_like =
      packet.command_kind == CFXSyncCommandKind::DIMMER && directional &&
      (pressed || released);

  if (hold_like && pressed) {
    if (this->remote_input_owner_ == nullptr) {
      this->remote_input_owner_ = &peer;
    } else if (this->remote_input_owner_ != &peer) {
      this->log_rejection_(
          "Ignoring remote input while another controller is active");
      return false;
    }
  } else if (hold_like && released) {
    if (this->remote_input_owner_ != nullptr &&
        this->remote_input_owner_ != &peer) {
      this->log_rejection_(
          "Ignoring remote input while another controller is active");
      return false;
    }
  }

  auto *leader = this->leader_light_();
  bool predicted_sent = false;
  CFXSyncLightSnapshot predicted_snapshot;
  CFXSyncEffectState predicted_effect;
  CFXSyncControlState predicted_controls;
  if (leader != nullptr && !this->effect_catalogs_.empty()) {
    predicted_snapshot = capture_light_snapshot(*leader);
    CFXSyncTimingState predicted_timing;
    if (this->predict_leader_state_from_command_(
            packet, predicted_snapshot, predicted_timing)) {
      predicted_effect =
          capture_effect_state(leader, this->effect_catalogs_[0]);
      predicted_controls = this->capture_control_state_(0);
      predicted_sent = this->send_state_to_followers_(
          predicted_snapshot, predicted_effect, predicted_controls,
          predicted_timing);
    }
  }

  const bool applied = this->apply_light_command_to_leader_(packet);
  if (predicted_sent && applied) {
    this->has_observed_state_ = true;
    this->observed_state_ = predicted_snapshot;
    this->observed_effect_ = predicted_effect;
    this->observed_controls_ = predicted_controls;
  }
  if (hold_like && released) {
    this->clear_remote_input_owner_();
  }
  return applied;
}

bool CFXSyncComponent::predict_leader_state_from_command_(
    const CFXSyncPacket &packet, CFXSyncLightSnapshot &snapshot,
    CFXSyncTimingState &timing) const {
  auto *leader = this->leader_light_();
  if (leader == nullptr) {
    return false;
  }
  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_EFFECT) != 0 ||
      packet.command_kind == CFXSyncCommandKind::EFFECT) {
    return false;
  }

  bool has_action = false;
  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_TOGGLE) != 0) {
    snapshot.power = !leader->remote_values.is_on();
    has_action = true;
  }
  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_POWER) != 0) {
    snapshot.power = packet.command_power;
    has_action = true;
  }

  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_RAMP) != 0) {
    timing.has_ramp = true;
    timing.ramp_ms = packet.command_ramp_ms;
  }

  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_BRIGHTNESS) != 0 &&
      light_supports_brightness(*leader)) {
    snapshot.power = true;
    snapshot.brightness = packet.command_brightness;
    has_action = true;
  }

  const bool has_rgb =
      (packet.command_mask & CFXSyncPacketCodec::COMMAND_RGB) != 0;
  const bool has_white =
      (packet.command_mask & CFXSyncPacketCodec::COMMAND_WHITE) != 0;
  const bool has_color_brightness =
      (packet.command_mask &
       CFXSyncPacketCodec::COMMAND_COLOR_BRIGHTNESS) != 0;
  if (has_rgb && light_supports_rgb(*leader)) {
    snapshot.power = true;
    snapshot.has_color = true;
    snapshot.red = packet.command_red;
    snapshot.green = packet.command_green;
    snapshot.blue = packet.command_blue;
    if (has_color_brightness) {
      snapshot.has_color_brightness = true;
      snapshot.color_brightness = packet.command_color_brightness;
    }
    if (has_white && light_supports_white(*leader)) {
      snapshot.has_white = true;
      snapshot.white = packet.command_white;
    }
    has_action = true;
  } else if (has_white && light_supports_white(*leader)) {
    snapshot.power = true;
    snapshot.has_white = true;
    snapshot.white = packet.command_white;
    if (has_color_brightness && light_supports_rgb(*leader)) {
      snapshot.has_color_brightness = true;
      snapshot.color_brightness = packet.command_color_brightness;
    }
    has_action = true;
  } else if (has_color_brightness && light_supports_rgb(*leader)) {
    snapshot.power = true;
    snapshot.has_color_brightness = true;
    snapshot.color_brightness = packet.command_color_brightness;
    has_action = true;
  }

  if ((packet.command_mask &
       CFXSyncPacketCodec::COMMAND_COLD_WARM_WHITE) != 0 &&
      light_supports_cold_warm_white(*leader)) {
    snapshot.power = true;
    snapshot.has_cold_warm_white = true;
    snapshot.cold_white = packet.command_cold_white;
    snapshot.warm_white = packet.command_warm_white;
    if ((packet.command_mask &
         CFXSyncPacketCodec::COMMAND_COLOR_TEMPERATURE) != 0 &&
        light_supports_color_temperature(*leader)) {
      snapshot.has_color_temperature = true;
      snapshot.color_temperature_mireds =
          packet.command_color_temperature_mireds;
    }
    has_action = true;
  } else if ((packet.command_mask &
              CFXSyncPacketCodec::COMMAND_COLOR_TEMPERATURE) != 0 &&
             light_supports_color_temperature(*leader)) {
    snapshot.power = true;
    snapshot.has_color_temperature = true;
    snapshot.color_temperature_mireds =
        packet.command_color_temperature_mireds;
    has_action = true;
  }

  return has_action;
}

bool CFXSyncComponent::apply_light_command_to_leader_(
    const CFXSyncPacket &packet) {
  auto *leader = this->leader_light_();
  if (leader == nullptr) {
    return false;
  }
  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_EFFECT) != 0 ||
      packet.command_kind == CFXSyncCommandKind::EFFECT) {
    ESP_LOGW(TAG,
             "CFX Sync effect command ignored: unsupported in this release");
    return false;
  }

  auto call = leader->make_call();
  bool has_action = false;
  const bool released =
      (packet.command_flags & CFXSyncPacketCodec::COMMAND_FLAG_RELEASED) != 0;
  const bool directional =
      (packet.command_flags &
       (CFXSyncPacketCodec::COMMAND_FLAG_DIRECTION_UP |
        CFXSyncPacketCodec::COMMAND_FLAG_DIRECTION_DOWN)) != 0;
  const bool dimmer_release =
      packet.command_kind == CFXSyncCommandKind::DIMMER && released &&
      directional;

  if (dimmer_release) {
    const float brightness =
        leader->current_values.is_on()
            ? std::max(0.0f,
                       std::min(1.0f,
                                leader->current_values.get_brightness() *
                                    leader->current_values.get_state()))
            : leader->remote_values.get_brightness();
    call.set_transition_length(0);
    call.set_state(leader->remote_values.is_on());
    if (light_supports_brightness(*leader)) {
      call.set_brightness(brightness);
    }
    cfx_dimmer::clear_light_timing_hint(leader);
    call.perform();
    return true;
  }

  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_RAMP) != 0) {
    call.set_transition_length(packet.command_ramp_ms);
  }
  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_TOGGLE) != 0) {
    call.set_state(!leader->remote_values.is_on());
    has_action = true;
  }
  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_POWER) != 0) {
    call.set_state(packet.command_power);
    has_action = true;
  }

  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_RAMP) != 0 &&
      packet.command_ramp_ms > 0 &&
      ((packet.command_mask & CFXSyncPacketCodec::COMMAND_BRIGHTNESS) != 0 ||
       packet.command_kind == CFXSyncCommandKind::DIMMER)) {
    cfx_dimmer::publish_light_ramp_duration_hint(leader,
                                                 packet.command_ramp_ms);
  }

  if ((packet.command_mask & CFXSyncPacketCodec::COMMAND_BRIGHTNESS) != 0 &&
      light_supports_brightness(*leader)) {
    call.set_state(true);
    call.set_brightness(packet.command_brightness / 255.0f);
    has_action = true;
  }

  const bool has_rgb =
      (packet.command_mask & CFXSyncPacketCodec::COMMAND_RGB) != 0;
  const bool has_white =
      (packet.command_mask & CFXSyncPacketCodec::COMMAND_WHITE) != 0;
  const bool has_color_brightness =
      (packet.command_mask &
       CFXSyncPacketCodec::COMMAND_COLOR_BRIGHTNESS) != 0;
  if (has_rgb && light_supports_rgb_white(*leader)) {
    call.set_state(true);
    call.set_color_mode(light::ColorMode::RGB_WHITE);
    call.set_rgb(packet.command_red / 255.0f,
                 packet.command_green / 255.0f,
                 packet.command_blue / 255.0f);
    if (has_white) {
      call.set_white(packet.command_white / 255.0f);
    }
    if (has_color_brightness) {
      call.set_color_brightness(packet.command_color_brightness / 255.0f);
    }
    has_action = true;
  } else if (has_rgb && light_supports_rgb(*leader)) {
    call.set_state(true);
    call.set_color_mode(light::ColorMode::RGB);
    call.set_rgb(packet.command_red / 255.0f,
                 packet.command_green / 255.0f,
                 packet.command_blue / 255.0f);
    if (has_color_brightness) {
      call.set_color_brightness(packet.command_color_brightness / 255.0f);
    }
    has_action = true;
  } else if (has_white && light_supports_rgb_white(*leader)) {
    call.set_state(true);
    call.set_color_mode(light::ColorMode::RGB_WHITE);
    call.set_white(packet.command_white / 255.0f);
    if (has_color_brightness) {
      call.set_color_brightness(packet.command_color_brightness / 255.0f);
    }
    has_action = true;
  } else if (has_white && light_supports_white(*leader) &&
             !light_supports_rgb(*leader)) {
    call.set_state(true);
    call.set_color_mode(light::ColorMode::WHITE);
    call.set_white(packet.command_white / 255.0f);
    has_action = true;
  } else if (has_color_brightness && light_supports_rgb(*leader)) {
    call.set_state(true);
    call.set_color_brightness(packet.command_color_brightness / 255.0f);
    has_action = true;
  }

  if ((packet.command_mask &
       CFXSyncPacketCodec::COMMAND_COLD_WARM_WHITE) != 0 &&
      light_supports_cold_warm_white(*leader)) {
    call.set_state(true);
    call.set_color_mode(light::ColorMode::COLD_WARM_WHITE);
    if ((packet.command_mask &
         CFXSyncPacketCodec::COMMAND_COLOR_TEMPERATURE) != 0 &&
        light_supports_color_temperature(*leader)) {
      call.set_color_temperature(packet.command_color_temperature_mireds);
    }
    call.set_cold_white(packet.command_cold_white / 255.0f);
    call.set_warm_white(packet.command_warm_white / 255.0f);
    has_action = true;
  } else if ((packet.command_mask &
              CFXSyncPacketCodec::COMMAND_COLOR_TEMPERATURE) != 0 &&
             light_supports_color_temperature(*leader)) {
    call.set_state(true);
    call.set_color_mode(light::ColorMode::COLOR_TEMPERATURE);
    call.set_color_temperature(packet.command_color_temperature_mireds);
    has_action = true;
  }

  if (!has_action) {
    return false;
  }
  call.perform();
  return true;
}

void CFXSyncComponent::clear_remote_input_owner_() {
  this->remote_input_owner_ = nullptr;
}

void CFXSyncComponent::apply_remote_power_input_(bool pressed) {
  auto *leader = this->leader_light_();
  if (leader == nullptr) {
    return;
  }
  auto call = leader->make_call();
  call.set_state(pressed);
  call.perform();
}

void CFXSyncComponent::apply_remote_toggle_input_() {
  auto *leader = this->leader_light_();
  if (leader == nullptr) {
    return;
  }
  auto call = leader->make_call();
  call.set_state(!leader->remote_values.is_on());
  call.perform();
}

void CFXSyncComponent::schedule_remote_input_timeout_() {
  this->set_timeout("remote-input-timeout", REMOTE_INPUT_TIMEOUT_MS,
                    [this]() {
                      if (!this->remote_input_pressed_) {
                        return;
                      }
                      const uint32_t now = millis();
                      if (now - this->last_remote_input_ms_ <
                          REMOTE_INPUT_TIMEOUT_MS) {
                        this->schedule_remote_input_timeout_();
                        return;
                      }
                      this->inject_remote_input_(false, false, false,
                                                this->remote_input_action_);
                      this->clear_remote_input_owner_();
                    });
}
#endif

bool CFXSyncComponent::send_packet_(std::vector<uint8_t> &packet) {
  if (!this->has_static_peer_) {
    return false;
  }
  return this->send_packet_to_(this->peer_, packet);
}

bool CFXSyncComponent::send_udp_packet_(std::vector<uint8_t> &packet) {
  if (!this->bus_->send_udp(packet)) {
#if defined(USE_ESP32)
    this->handle_send_result_(ESP_FAIL);
#else
    this->send_failures_++;
#endif
    return false;
  }
#if defined(USE_ESP32)
  this->handle_send_result_(ESP_OK);
#endif
  this->sent_packets_++;
#if defined(USE_ESP32)
  this->flush_deferred_state_();
#endif
  return true;
}

bool CFXSyncComponent::send_udp_packet_to_(uint32_t ipv4, uint16_t port,
                                           std::vector<uint8_t> &packet) {
  if (!this->bus_->send_udp_to(ipv4, port, packet)) {
#if defined(USE_ESP32)
    this->handle_send_result_(ESP_FAIL);
#else
    this->send_failures_++;
#endif
    return false;
  }
#if defined(USE_ESP32)
  this->handle_send_result_(ESP_OK);
#endif
  this->sent_packets_++;
#if defined(USE_ESP32)
  this->flush_deferred_state_();
#endif
  return true;
}

bool CFXSyncComponent::send_espnow_packet_to_(
    const std::array<uint8_t, 6> &mac, std::vector<uint8_t> &packet) {
#ifdef USE_ESPNOW
  if (!this->bus_->has_espnow()) {
    return false;
  }
  if (this->send_pending_) {
    return false;
  }

  this->send_pending_ = true;
  const esp_err_t result = this->bus_->send_espnow(
      mac.data(), packet,
      [this](esp_err_t send_result) {
        this->send_pending_ = false;
        this->handle_send_result_(send_result);
        this->flush_deferred_input_();
        if (send_result == ESP_OK) {
          this->flush_deferred_state_();
        }
      });
  if (result != ESP_OK) {
    this->send_pending_ = false;
    this->handle_send_result_(result);
    return false;
  }
  this->sent_packets_++;
  return true;
#else
  (void) mac;
  (void) packet;
  return false;
#endif
}

bool CFXSyncComponent::send_packet_to_(const std::array<uint8_t, 6> &mac,
                                       std::vector<uint8_t> &packet) {
  if (this->use_udp_transport_() && !this->use_espnow_transport_()) {
    (void) mac;
    return this->send_udp_packet_(packet);
  }
  return this->send_espnow_packet_to_(mac, packet);
}

bool CFXSyncComponent::send_state_packet_to_followers_(
    std::vector<uint8_t> &packet) {
  bool sent = false;
  if (this->use_udp_transport_()) {
    if (this->send_udp_packet_(packet)) {
      this->udp_state_sent_++;
      sent = true;
    }
  }
  if (this->use_espnow_transport_()) {
    if (this->send_espnow_packet_to_(BROADCAST_MAC, packet)) {
      this->espnow_state_sent_++;
      sent = true;
    }
  }
  return sent;
}

bool CFXSyncComponent::send_packet_to_peer_(PeerState &peer,
                                            std::vector<uint8_t> &packet) {
  if (peer.transport == CFXSyncTransportKind::UDP) {
    return this->send_udp_packet_to_(peer.ipv4, peer.udp_port, packet);
  }
  if (this->use_udp_transport_() && !this->use_espnow_transport_()) {
    return this->send_udp_packet_(packet);
  }
#ifdef USE_ESPNOW
  if (!this->bus_->has_espnow()) {
    return false;
  }
  if (this->send_pending_) {
    return false;
  }

  this->send_pending_ = true;
  const esp_err_t result = this->bus_->send_espnow(
      peer.mac.data(), packet,
      [this, &peer](esp_err_t send_result) {
        this->send_pending_ = false;
        this->handle_peer_send_result_(peer, send_result);
        this->flush_deferred_input_();
        this->flush_deferred_state_();
      });
  if (result != ESP_OK) {
    this->send_pending_ = false;
    this->handle_peer_send_result_(peer, result);
    this->flush_deferred_state_();
    return false;
  }
  this->sent_packets_++;
  return true;
#else
  (void) peer;
  (void) packet;
  return false;
#endif
}

uint32_t CFXSyncComponent::next_sequence_() {
  this->tx_sequence_++;
  if (this->tx_sequence_ == 0) {
    this->boot_id_ = esp_random();
    if (this->boot_id_ == 0) {
      this->boot_id_ = 1;
    }
    this->tx_sequence_ = 1;
  }
  return this->tx_sequence_;
}

CFXSyncComponent::PeerState *CFXSyncComponent::find_peer_(
    const uint8_t *mac) {
  if (mac == nullptr) {
    return nullptr;
  }
  for (auto &peer : this->peers_) {
    if (peer.active &&
        memcmp(peer.mac.data(), mac, peer.mac.size()) == 0) {
      return &peer;
    }
  }
  return nullptr;
}

bool CFXSyncComponent::peer_matches_source_(
    const PeerState &peer, const CFXSyncSource &source) const {
  if (!peer.active || !source.identity_valid ||
      peer.transport != source.transport) {
    return false;
  }
  if (source.transport == CFXSyncTransportKind::ESPNOW) {
    return memcmp(peer.mac.data(), source.mac.data(), peer.mac.size()) == 0;
  }
  return peer.ipv4 == source.ipv4;
}

CFXSyncComponent::PeerState *CFXSyncComponent::find_peer_(
    const CFXSyncSource &source) {
  if (!source.identity_valid) {
    return nullptr;
  }
  for (auto &peer : this->peers_) {
    if (this->peer_matches_source_(peer, source)) {
      return &peer;
    }
  }
  return nullptr;
}

CFXSyncComponent::PeerState *CFXSyncComponent::find_or_add_peer_(
    const uint8_t *mac, CFXSyncNodeRole role, uint16_t capabilities) {
  if (mac == nullptr || this->is_broadcast_(mac)) {
    return nullptr;
  }

  if (auto *peer = this->find_peer_(mac); peer != nullptr) {
    peer->node_role = role;
    peer->capabilities = capabilities;
    peer->last_seen_ms = millis();
    peer->tx_suspended_until_ms = 0;
    peer->consecutive_send_failures = 0;
    return peer;
  }

  for (auto &peer : this->peers_) {
    if (peer.active) {
      continue;
    }
    peer = PeerState{};
    peer.active = true;
    peer.transport = CFXSyncTransportKind::ESPNOW;
    memcpy(peer.mac.data(), mac, peer.mac.size());
    peer.node_role = role;
    peer.capabilities = capabilities;
    peer.last_seen_ms = millis();
    char peer_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
    format_mac_addr_upper(peer.mac.data(), peer_buf);
    ESP_LOGI(TAG, "Discovered CFX Sync %s peer %s in group %08" PRIX32,
             node_role_name(role), peer_buf, this->group_hash_);
    return &peer;
  }

  char peer_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  format_mac_addr_upper(mac, peer_buf);
  ESP_LOGW(TAG, "CFX Sync peer table full; ignoring %s", peer_buf);
  return nullptr;
}

CFXSyncComponent::PeerState *CFXSyncComponent::find_or_add_peer_(
    const CFXSyncSource &source, CFXSyncNodeRole role,
    uint16_t capabilities) {
  if (!source.identity_valid) {
    return nullptr;
  }
  if (source.transport == CFXSyncTransportKind::ESPNOW) {
    return this->find_or_add_peer_(source.mac.data(), role, capabilities);
  }

  if (auto *peer = this->find_peer_(source); peer != nullptr) {
    peer->node_role = role;
    peer->capabilities = capabilities;
    peer->udp_port = source.port;
    peer->last_seen_ms = millis();
    peer->tx_suspended_until_ms = 0;
    peer->consecutive_send_failures = 0;
    return peer;
  }

  for (auto &peer : this->peers_) {
    if (peer.active) {
      continue;
    }
    peer = PeerState{};
    peer.active = true;
    peer.transport = CFXSyncTransportKind::UDP;
    peer.ipv4 = source.ipv4;
    peer.udp_port = source.port;
    peer.node_role = role;
    peer.capabilities = capabilities;
    peer.registered = true;
    peer.last_seen_ms = millis();
    const uint8_t *addr = reinterpret_cast<const uint8_t *>(&peer.ipv4);
    ESP_LOGI(TAG,
             "Discovered CFX Sync %s peer %u.%u.%u.%u:%u in group %08" PRIX32,
             node_role_name(role), addr[0], addr[1], addr[2], addr[3],
             static_cast<unsigned>(peer.udp_port), this->group_hash_);
    return &peer;
  }

  ESP_LOGW(TAG, "CFX Sync peer table full; ignoring UDP peer");
  return nullptr;
}

bool CFXSyncComponent::register_peer_(PeerState &peer) {
  if (peer.transport == CFXSyncTransportKind::UDP) {
    peer.registered = true;
    return peer.active;
  }
#ifdef USE_ESPNOW
  if (!peer.active || !this->bus_->has_espnow() ||
      this->is_broadcast_(peer.mac.data())) {
    return false;
  }
  if (peer.registered) {
    return true;
  }
  if (!this->bus_->add_espnow_peer(peer.mac.data())) {
    char peer_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
    format_mac_addr_upper(peer.mac.data(), peer_buf);
    ESP_LOGW(TAG, "CFX Sync failed to register peer %s", peer_buf);
    return false;
  }
  peer.registered = true;
  return true;
#else
  return false;
#endif
}

bool CFXSyncComponent::accept_sequence_(PeerState &peer, uint32_t boot_id,
                                        uint32_t sequence) {
  if (!peer.has_rx_sequence || boot_id != peer.rx_boot_id) {
    peer.has_rx_sequence = true;
    peer.rx_boot_id = boot_id;
    peer.rx_sequence = sequence;
    return true;
  }
  if (sequence <= peer.rx_sequence) {
    return false;
  }
  peer.rx_sequence = sequence;
  return true;
}

bool CFXSyncComponent::has_peer_send_warning_() const {
  for (const auto &peer : this->peers_) {
    if (peer.active && peer.consecutive_send_failures >=
                           MAX_CONSECUTIVE_SEND_FAILURES) {
      return true;
    }
  }
  return false;
}

bool CFXSyncComponent::has_pending_ack_(const PeerState &peer) const {
  return peer.last_state_sent_sequence != 0 &&
         (peer.last_ack_boot_id != peer.last_state_sent_boot_id ||
          peer.last_ack_sequence != peer.last_state_sent_sequence);
}

#if defined(USE_ESP32)
bool CFXSyncComponent::is_current_broadcast_ack_(
    const CFXSyncPacket &packet) const {
  return this->last_broadcast_state_boot_id_ != 0 &&
         packet.acked_boot_id == this->last_broadcast_state_boot_id_ &&
         packet.acked_sequence == this->last_broadcast_state_sequence_;
}

void CFXSyncComponent::seed_peer_sent_state_from_ack_(
    PeerState &peer, const CFXSyncPacket &packet) {
  peer.last_state_sent_boot_id = packet.acked_boot_id;
  peer.last_state_sent_sequence = packet.acked_sequence;
  peer.last_state_sent_ms =
      this->last_broadcast_state_ms_ != 0 ? this->last_broadcast_state_ms_
                                          : millis();
}
#endif

bool CFXSyncComponent::is_peer_send_suspended_(
    const PeerState &peer) const {
  return peer.tx_suspended_until_ms != 0 &&
         static_cast<int32_t>(peer.tx_suspended_until_ms - millis()) > 0;
}

uint8_t CFXSyncComponent::active_peer_count_() const {
  uint8_t count = 0;
  for (const auto &peer : this->peers_) {
    if (peer.active) {
      count++;
    }
  }
  return count;
}

uint8_t CFXSyncComponent::follower_peer_count_() const {
  uint8_t count = 0;
  for (const auto &peer : this->peers_) {
    if (peer.active && peer.node_role == CFXSyncNodeRole::FOLLOWER) {
      count++;
    }
  }
  return count;
}

uint8_t CFXSyncComponent::remote_peer_count_() const {
  uint8_t count = 0;
  for (const auto &peer : this->peers_) {
    if (peer.active && peer.node_role == CFXSyncNodeRole::REMOTE) {
      count++;
    }
  }
  return count;
}

uint8_t CFXSyncComponent::pending_ack_count_() const {
#if defined(USE_ESP32)
  uint8_t count = 0;
  for (const auto &peer : this->peers_) {
    if (this->peer_accepts_leader_state_(peer) &&
        this->has_pending_ack_(peer)) {
      count++;
    }
  }
  return count;
#else
  return 0;
#endif
}

uint32_t CFXSyncComponent::missed_ack_count_() const {
  uint32_t count = 0;
  for (const auto &peer : this->peers_) {
    count += peer.missed_acks;
  }
  return count;
}

#if defined(USE_ESP32)
void CFXSyncComponent::handle_state_ack_(PeerState &peer,
                                         const CFXSyncPacket &packet) {
  if (packet.acked_boot_id != peer.last_state_sent_boot_id ||
      packet.acked_sequence != peer.last_state_sent_sequence) {
    this->log_rejection_("Ignoring stale or mismatched STATE_ACK");
    return;
  }

  peer.last_ack_boot_id = packet.acked_boot_id;
  peer.last_ack_sequence = packet.acked_sequence;
  peer.last_ack_ms = millis();
  peer.missed_acks = 0;

  bool has_pending = false;
  for (const auto &candidate : this->peers_) {
    if (this->peer_accepts_leader_state_(candidate) &&
        this->has_pending_ack_(candidate)) {
      has_pending = true;
      break;
    }
  }
  if (!this->has_peer_send_warning_() && !has_pending &&
      this->consecutive_send_failures_ < MAX_CONSECUTIVE_SEND_FAILURES) {
    this->state_retry_attempts_ = 0;
    this->last_state_retry_packet_valid_ = false;
    this->clear_warning_if_set_();
  } else {
    this->check_ack_health_();
  }
}

void CFXSyncComponent::check_ack_health_() {
  if (this->role_ != CFXSyncRole::LEADER) {
    return;
  }

  const uint32_t now = millis();
  uint8_t missing = 0;
  for (auto &peer : this->peers_) {
    if (!this->peer_accepts_leader_state_(peer) ||
        !this->has_pending_ack_(peer)) {
      continue;
    }
    if (now - peer.last_state_sent_ms >= ACK_WARNING_MS) {
      missing++;
      if (peer.missed_acks < UINT32_MAX) {
        peer.missed_acks++;
      }
    }
  }

  if (missing == 0) {
    return;
  }
  if (this->last_ack_warning_log_ms_ == 0 ||
      now - this->last_ack_warning_log_ms_ >= 5000) {
    ESP_LOGW(TAG, "CFX Sync follower ACK missing from %u peer(s)",
             static_cast<unsigned>(missing));
    this->last_ack_warning_log_ms_ = now;
  }
  this->status_set_warning();
}

void CFXSyncComponent::handle_send_result_(esp_err_t result) {
  if (result == ESP_OK) {
    this->consecutive_send_failures_ = 0;
    bool has_pending = false;
    for (const auto &peer : this->peers_) {
      if (this->peer_accepts_leader_state_(peer) &&
          this->has_pending_ack_(peer)) {
        has_pending = true;
        break;
      }
    }
    if (this->role_ == CFXSyncRole::LEADER &&
        !this->has_peer_send_warning_() && !has_pending) {
      this->clear_warning_if_set_();
    }
    return;
  }

  this->send_failures_++;
  if (this->consecutive_send_failures_ < UINT8_MAX) {
    this->consecutive_send_failures_++;
  }
  const uint32_t now = millis();
  if (this->last_send_failure_log_ms_ == 0 ||
      now - this->last_send_failure_log_ms_ >= 5000) {
    ESP_LOGW(TAG, "ESP-NOW send failed: %s", esp_err_to_name(result));
    this->last_send_failure_log_ms_ = now;
  }
  if (this->role_ == CFXSyncRole::LEADER &&
      this->consecutive_send_failures_ >=
          MAX_CONSECUTIVE_SEND_FAILURES) {
    this->status_set_warning();
  }
}

void CFXSyncComponent::handle_peer_send_result_(PeerState &peer,
                                                esp_err_t result) {
  if (result == ESP_OK) {
    peer.consecutive_send_failures = 0;
    bool has_pending = false;
    for (const auto &candidate : this->peers_) {
      if (this->peer_accepts_leader_state_(candidate) &&
          this->has_pending_ack_(candidate)) {
        has_pending = true;
        break;
      }
    }
    if (this->role_ == CFXSyncRole::LEADER &&
        this->consecutive_send_failures_ < MAX_CONSECUTIVE_SEND_FAILURES &&
        !this->has_peer_send_warning_() && !has_pending) {
      this->clear_warning_if_set_();
    }
    return;
  }

  peer.send_failures++;
  this->send_failures_++;
  if (peer.consecutive_send_failures < UINT8_MAX) {
    peer.consecutive_send_failures++;
  }
  const uint32_t now = millis();
  peer.tx_suspended_until_ms = now + PEER_SEND_COOLDOWN_MS;
  if (peer.last_send_failure_log_ms == 0 ||
      now - peer.last_send_failure_log_ms >= 5000) {
    char peer_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
    format_mac_addr_upper(peer.mac.data(), peer_buf);
    ESP_LOGW(TAG, "ESP-NOW send to peer %s failed: %s", peer_buf,
             esp_err_to_name(result));
    peer.last_send_failure_log_ms = now;
  }
  if (this->role_ == CFXSyncRole::LEADER &&
      peer.consecutive_send_failures >=
          MAX_CONSECUTIVE_SEND_FAILURES) {
    this->status_set_warning();
  }
}
#endif

void CFXSyncComponent::clear_warning_if_set_() {
  if (this->status_has_warning()) {
    this->status_clear_warning();
  }
}

#if defined(USE_ESP32)
void CFXSyncComponent::flush_deferred_state_() {
  if (!this->state_send_deferred_ || this->send_pending_ ||
      this->role_ != CFXSyncRole::LEADER) {
    return;
  }
  const bool include_default_transition =
      this->state_send_deferred_with_transition_;
  this->state_send_deferred_ = false;
  this->state_send_deferred_with_transition_ = false;
  this->send_state_(include_default_transition);
}

void CFXSyncComponent::schedule_state_retry_() {
  if (this->role_ != CFXSyncRole::LEADER ||
      this->state_retry_scheduled_ ||
      this->pending_ack_count_() == 0) {
    return;
  }
  this->state_retry_scheduled_ = true;
  this->set_timeout("state-retry", STATE_RETRY_DELAY_MS, [this]() {
    this->state_retry_scheduled_ = false;
    if (this->role_ != CFXSyncRole::LEADER) {
      return;
    }
    if (this->pending_ack_count_() == 0) {
      this->state_retry_attempts_ = 0;
      this->last_state_retry_packet_valid_ = false;
      return;
    }
    if (this->state_retry_attempts_ >= STATE_RETRY_MAX_ATTEMPTS) {
      this->check_ack_health_();
      return;
    }
    if (this->send_pending_) {
      this->schedule_state_retry_();
      return;
    }
    this->state_retry_attempts_++;
    this->state_retry_active_ = true;
    if (this->last_state_retry_packet_valid_) {
      this->send_state_packet_to_followers_(this->last_state_retry_packet_);
    }
    this->state_retry_active_ = false;
    this->schedule_state_retry_();
  });
}
#endif

void CFXSyncComponent::handle_decode_failure_(
    CFXSyncDecodeResult result) {
  switch (result) {
    case CFXSyncDecodeResult::MALFORMED:
      this->malformed_packets_++;
      this->log_rejection_("Ignoring malformed packet");
      break;
    case CFXSyncDecodeResult::UNSUPPORTED_VERSION:
    case CFXSyncDecodeResult::UNSUPPORTED_TYPE:
      this->unsupported_packets_++;
      this->log_rejection_("Ignoring unsupported packet");
      break;
    case CFXSyncDecodeResult::WRONG_GROUP:
      this->wrong_group_packets_++;
      this->log_rejection_("Ignoring packet for another group");
      break;
    case CFXSyncDecodeResult::BAD_AUTH:
      this->authentication_failures_++;
      this->log_rejection_("Ignoring packet with invalid authentication");
      break;
    default:
      break;
  }
}

void CFXSyncComponent::log_rejection_(const char *message) {
  const uint32_t now = millis();
  if (this->last_rejection_log_ms_ == 0 ||
      now - this->last_rejection_log_ms_ >= 5000) {
    ESP_LOGV(TAG, "%s", message);
    this->last_rejection_log_ms_ = now;
  }
}

void CFXSyncComponent::schedule_boot_discovery_() {
  const uint32_t delay_ms =
      BOOT_DISCOVERY_DELAY_MS +
      (esp_random() % (BOOT_DISCOVERY_JITTER_SPREAD_MS + 1));
  this->set_timeout("boot-discovery", delay_ms,
                    [this]() { this->run_boot_discovery_(); });
}

void CFXSyncComponent::run_boot_discovery_() {
  if (!this->boot_radio_ready_() &&
      millis() - this->boot_discovery_started_ms_ <
          BOOT_DISCOVERY_MAX_WAIT_MS) {
    this->set_timeout("boot-discovery-retry", BOOT_DISCOVERY_RETRY_MS,
                      [this]() { this->run_boot_discovery_(); });
    return;
  }
  this->send_hello_();
}

bool CFXSyncComponent::boot_radio_ready_() const {
#if defined(USE_ESP32)
  if (this->offline_fallback_active_) {
    return true;
  }
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr) {
    return wifi::global_wifi_component->is_connected();
  }
#endif
#endif
  return true;
}

#if defined(USE_ESP32)
uint8_t CFXSyncComponent::current_wifi_channel_() const {
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr &&
      wifi::global_wifi_component->is_connected()) {
    const int32_t channel = wifi::global_wifi_component->get_wifi_channel();
    if (channel > 0 && channel <= UINT8_MAX) {
      return static_cast<uint8_t>(channel);
    }
  }
#endif
  return 0;
}

uint8_t CFXSyncComponent::active_sync_channel_() const {
  if (this->offline_fallback_active_) {
    return this->fallback_channel_;
  }
  return this->current_wifi_channel_();
}

const char *CFXSyncComponent::sync_mode_name_() const {
  return this->offline_fallback_active_ ? "offline fallback" : "infrastructure";
}

void CFXSyncComponent::enter_offline_fallback_() {
  if (this->offline_fallback_active_) {
    return;
  }
  this->offline_fallback_active_ = true;
  ESP_LOGW(TAG,
           "CFX Sync entering offline fallback on channel %u; ESPHome Wi-Fi reboot policy still applies",
           static_cast<unsigned>(this->fallback_channel_));
  this->apply_fallback_channel_();
  this->schedule_espnow_rearm_("offline-fallback");
  this->send_hello_();
  if (this->role_ == CFXSyncRole::LEADER) {
    this->send_state_();
  } else if (this->is_state_receiver_role_() &&
             !this->has_valid_state_) {
    this->schedule_follower_recovery_loop_();
  }
}

void CFXSyncComponent::exit_offline_fallback_(uint8_t channel) {
  if (!this->offline_fallback_active_) {
    return;
  }
  this->offline_fallback_active_ = false;
  ESP_LOGV(TAG,
           "CFX Sync exiting offline fallback; infrastructure channel %u restored",
           static_cast<unsigned>(channel));
  this->schedule_espnow_rearm_("wifi-restored");
  this->send_hello_();
  if (this->role_ == CFXSyncRole::LEADER) {
    this->send_state_();
  }
}

bool CFXSyncComponent::apply_fallback_channel_() {
  if (!this->offline_fallback_active_ ||
      this->fallback_channel_ == 0) {
    return false;
  }
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr &&
      wifi::global_wifi_component->is_connected()) {
    return false;
  }
#endif
  esp_err_t err = esp_wifi_set_promiscuous(true);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to prepare ESP-NOW fallback channel %u: %s",
             static_cast<unsigned>(this->fallback_channel_),
             esp_err_to_name(err));
    return false;
  }
  err = esp_wifi_set_channel(this->fallback_channel_, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set ESP-NOW fallback channel %u: %s",
             static_cast<unsigned>(this->fallback_channel_),
             esp_err_to_name(err));
    return false;
  }
  return true;
}

void CFXSyncComponent::format_current_wifi_bssid_(char *buffer,
                                                  size_t size) const {
  if (buffer == nullptr || size == 0) {
    return;
  }
  strncpy(buffer, "unknown", size);
  buffer[size - 1] = '\0';
#ifdef USE_WIFI
  if (wifi::global_wifi_component == nullptr ||
      !wifi::global_wifi_component->is_connected() ||
      size < MAC_ADDRESS_PRETTY_BUFFER_SIZE) {
    return;
  }
  const auto bssid = wifi::global_wifi_component->wifi_bssid();
  bool has_bssid = false;
  for (auto byte : bssid) {
    if (byte != 0) {
      has_bssid = true;
      break;
    }
  }
  if (has_bssid) {
    format_mac_addr_upper(bssid.data(), buffer);
  }
#endif
}

void CFXSyncComponent::schedule_espnow_rearm_(const char *reason) {
#ifdef USE_ESPNOW
  if (!this->bus_->has_espnow() || this->espnow_rearm_scheduled_) {
    return;
  }
  uint32_t delay_ms = ESPNOW_REARM_DELAY_MS;
  const uint32_t now = millis();
  if (this->last_espnow_rearm_ms_ != 0 &&
      now - this->last_espnow_rearm_ms_ < ESPNOW_REARM_MIN_INTERVAL_MS) {
    delay_ms += ESPNOW_REARM_MIN_INTERVAL_MS -
                (now - this->last_espnow_rearm_ms_);
  }
  this->espnow_rearm_scheduled_ = true;
  this->set_timeout("espnow-rearm", delay_ms, [this, reason]() {
    this->espnow_rearm_scheduled_ = false;
    if (!this->bus_->has_espnow()) {
      return;
    }
    if (this->send_pending_ || !this->boot_radio_ready_()) {
      this->schedule_espnow_rearm_(reason);
      return;
    }
    const uint8_t channel = this->active_sync_channel_();
    ESP_LOGV(TAG, "Re-arming ESP-NOW after %s on %s channel %u", reason,
             this->sync_mode_name_(), static_cast<unsigned>(channel));
    this->bus_->disable_espnow();
    this->apply_fallback_channel_();
    this->bus_->enable_espnow();
    this->last_espnow_rearm_ms_ = millis();
    this->boot_discovery_started_ms_ = millis();
    this->schedule_boot_discovery_();
    if (this->is_state_receiver_role_() && this->sync_enabled_ &&
        !this->has_valid_state_) {
      this->schedule_follower_recovery_loop_();
    }
  });
#else
  (void) reason;
#endif
}

#endif  // defined(USE_ESP32)

void CFXSyncComponent::schedule_follower_hello_() {
  if (this->role_ == CFXSyncRole::LEADER) {
    return;
  }
  const uint32_t delay_ms =
      HELLO_INTERVAL_MS + (esp_random() % (HELLO_JITTER_SPREAD_MS + 1));
  this->set_timeout("hello", delay_ms, [this]() {
    if (this->role_ == CFXSyncRole::LEADER) {
      return;
    }
    this->send_hello_();
    this->schedule_follower_hello_();
  });
}

#if defined(USE_ESP32)
void CFXSyncComponent::schedule_follower_recovery_() {
  if (!this->is_state_receiver_role_()) {
    return;
  }
  this->schedule_follower_recovery_attempt_("sync-request-1",
                                            FOLLOWER_RECOVERY_FIRST_MS);
  this->schedule_follower_recovery_attempt_("sync-request-2",
                                            FOLLOWER_RECOVERY_SECOND_MS);
  this->schedule_follower_recovery_attempt_("sync-request-3",
                                            FOLLOWER_RECOVERY_THIRD_MS);
  this->set_timeout("sync-recovery-expired", FOLLOWER_RECOVERY_EXPIRE_MS,
                    [this]() {
                      if (this->sync_enabled_ && !this->has_valid_state_) {
                        char bssid_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
                        this->format_current_wifi_bssid_(
                            bssid_buf, sizeof(bssid_buf));
                        ESP_LOGW(
                            TAG,
                            "Discovered CFX Sync leader failed on BSSID %s "
                            "channel %u",
                            bssid_buf,
                            static_cast<unsigned>(
                                this->current_wifi_channel_()));
                        this->status_set_warning();
                        this->schedule_follower_recovery_loop_();
                      }
                    });
}

void CFXSyncComponent::schedule_follower_recovery_loop_() {
  if (!this->is_state_receiver_role_() || !this->sync_enabled_ ||
      this->has_valid_state_) {
    return;
  }
  const uint32_t delay_ms =
      FOLLOWER_RECOVERY_REPEAT_MS +
      (esp_random() % (FOLLOWER_RECOVERY_REPEAT_JITTER_SPREAD_MS + 1));
  this->set_timeout("sync-recovery-loop", delay_ms, [this]() {
    if (!this->is_state_receiver_role_() || !this->sync_enabled_ ||
        this->has_valid_state_) {
      return;
    }
    const uint32_t now = millis();
    if (this->last_espnow_rearm_ms_ == 0 ||
        now - this->last_espnow_rearm_ms_ >= FOLLOWER_RECOVERY_REARM_MS) {
      this->schedule_espnow_rearm_("follower-recovery");
    }
    if (this->boot_radio_ready_()) {
      this->send_sync_request_to_(BROADCAST_MAC);
    }
    this->schedule_follower_recovery_loop_();
  });
}

void CFXSyncComponent::schedule_follower_recovery_attempt_(
    const char *name, uint32_t base_delay_ms) {
  const uint32_t delay_ms =
      base_delay_ms + (esp_random() % (RECOVERY_JITTER_SPREAD_MS + 1));
  this->set_timeout(name, delay_ms, [this]() {
    if (this->sync_enabled_ && !this->has_valid_state_ &&
        this->boot_radio_ready_()) {
      this->send_sync_request_to_(BROADCAST_MAC);
    }
  });
}

void CFXSyncComponent::schedule_enable_resync_() {
  if (!this->is_state_receiver_role_() || !this->sync_enabled_) {
    return;
  }
  if (this->boot_radio_ready_()) {
    this->send_sync_request_to_(BROADCAST_MAC);
  }
  this->schedule_enable_resync_attempt_("enable-sync-1", 1000);
  this->schedule_enable_resync_attempt_("enable-sync-2", 2000);
  this->schedule_enable_resync_attempt_("enable-sync-4", 4000);
}

void CFXSyncComponent::schedule_enable_resync_attempt_(const char *name,
                                                       uint32_t delay_ms) {
  this->set_timeout(name, delay_ms, [this]() {
    if (this->is_state_receiver_role_() && this->sync_enabled_ &&
        !this->has_valid_state_ && this->boot_radio_ready_()) {
      this->send_sync_request_to_(BROADCAST_MAC);
    }
  });
}

#endif  // defined(USE_ESP32)

bool CFXSyncComponent::apply_remote_state_(const CFXSyncPacket &packet) {
  RemoteApplyGuard guard(this->applying_remote_state_);
  bool applied = false;
#if defined(USE_ESP8266)
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *light = this->lights_[i];
    if (light == nullptr) {
      continue;
    }
    applied |= this->apply_remote_state_to_light_(packet, i);
  }
#else
  const size_t aligned_light_count =
      std::min(this->lights_.size(),
               std::min(this->effect_catalogs_.size(),
                        std::min(this->effect_log_states_.size(),
                                 this->control_bindings_.size())));
  for (size_t i = 0; i < aligned_light_count; i++) {
    auto *light = this->lights_[i];
    if (light == nullptr) {
      continue;
    }
    applied |= this->apply_remote_state_to_light_(packet, i);
  }
#endif
  return applied;
}

bool CFXSyncComponent::apply_remote_state_to_light_(
    const CFXSyncPacket &packet, size_t light_index) {
#if defined(USE_ESP8266)
  if (light_index >= this->lights_.size()) {
    return false;
  }
  auto *light = this->lights_[light_index];
  if (light == nullptr) {
    return false;
  }

  auto call = light->make_call();
  bool has_action = false;
  const bool apply_visual_state = !(packet.has_power && !packet.power);
  if (packet.has_power) {
    if (light->remote_values.is_on() != packet.power) {
      call.set_state(packet.power);
      has_action = true;
    }
  }
  const auto traits = light->get_traits();
  const bool supports_brightness =
      traits.supports_color_mode(light::ColorMode::BRIGHTNESS) ||
      traits.supports_color_mode(light::ColorMode::WHITE) ||
      traits.supports_color_mode(light::ColorMode::RGB) ||
      traits.supports_color_mode(light::ColorMode::RGB_WHITE) ||
      traits.supports_color_mode(light::ColorMode::COLOR_TEMPERATURE) ||
      traits.supports_color_mode(light::ColorMode::COLD_WARM_WHITE) ||
      traits.supports_color_mode(light::ColorMode::RGB_COLOR_TEMPERATURE) ||
      traits.supports_color_mode(light::ColorMode::RGB_COLD_WARM_WHITE);
  if (packet.has_brightness && supports_brightness && apply_visual_state) {
    const float desired_brightness = packet.brightness / 255.0f;
    if (std::fabs(light->remote_values.get_brightness() -
                  desired_brightness) > (0.5f / 255.0f)) {
      call.set_brightness(desired_brightness);
      has_action = true;
    }
  }
  if (packet.has_color && apply_visual_state) {
    CFXSyncLightSnapshot snapshot;
    snapshot.red = packet.red;
    snapshot.green = packet.green;
    snapshot.blue = packet.blue;
    snapshot.white = packet.source_has_white ? packet.white : 0;
    snapshot.color_brightness =
        packet.has_color_brightness ? packet.color_brightness : 255;
    snapshot.has_white = packet.source_has_white;

    if (light_supports_rgb_white(*light)) {
      const auto converted = convert_color_for_follower(snapshot, true);
      call.set_color_mode(light::ColorMode::RGB_WHITE);
      call.set_color_brightness(converted.color_brightness / 255.0f);
      call.set_rgb(converted.red / 255.0f, converted.green / 255.0f,
                   converted.blue / 255.0f);
      call.set_white(converted.white / 255.0f);
      has_action = true;
    } else if (light_supports_rgb(*light)) {
      const auto converted = convert_color_for_follower(snapshot, false);
      call.set_color_mode(light::ColorMode::RGB);
      call.set_color_brightness(converted.color_brightness / 255.0f);
      call.set_rgb(converted.red / 255.0f, converted.green / 255.0f,
                   converted.blue / 255.0f);
      has_action = true;
    }
  } else if (packet.has_color_brightness && apply_visual_state) {
    call.set_color_brightness(packet.color_brightness / 255.0f);
    has_action = true;
  }
  if (packet.has_cold_warm_white && apply_visual_state &&
      light_supports_cold_warm_white(*light)) {
    call.set_color_mode(light::ColorMode::COLD_WARM_WHITE);
    if (packet.has_color_temperature &&
        light_supports_color_temperature(*light)) {
      call.set_color_temperature(packet.color_temperature_mireds);
    }
    call.set_cold_white(packet.cold_white / 255.0f);
    call.set_warm_white(packet.warm_white / 255.0f);
    has_action = true;
  } else if (packet.has_color_temperature && apply_visual_state &&
             light_supports_color_temperature(*light)) {
    call.set_color_mode(light::ColorMode::COLOR_TEMPERATURE);
    call.set_color_temperature(packet.color_temperature_mireds);
    has_action = true;
  }
  if (packet.has_effect || packet.has_controls || packet.has_transition ||
      packet.has_ramp) {
    const uint32_t now = millis();
    if (this->last_unsupported_visual_log_ms_ == 0 ||
        now - this->last_unsupported_visual_log_ms_ >=
            CONTROL_SKIP_LOG_INTERVAL_MS) {
      ESP_LOGV(TAG, "ESP8266 light follower ignoring ChimeraFX-only fields");
      this->last_unsupported_visual_log_ms_ = now;
    }
  }
  if (has_action) {
    call.perform();
  }
  return has_action;
#else
  if (light_index >= this->lights_.size() ||
      light_index >= this->effect_catalogs_.size() ||
      light_index >= this->effect_log_states_.size()) {
    return false;
  }
  auto *light = this->lights_[light_index];
  if (light == nullptr) {
    return false;
  }
  this->apply_remote_controls_to_light_(packet, light_index);

  const auto &catalog = this->effect_catalogs_[light_index];
  auto &log_state = this->effect_log_states_[light_index];
  const bool turning_off =
      packet.has_power && !packet.power && light->remote_values.is_on();
  const bool turning_off_effect =
      turning_off && light->get_effect_name() != "None";
  const bool apply_visual_state = !turning_off;
  const bool use_remote_effect_off_transition =
      turning_off_effect && packet.has_transition &&
      light->get_default_transition_length() == 0;
  const uint32_t saved_default_transition =
      use_remote_effect_off_transition
          ? light->get_default_transition_length()
          : 0;
  auto call = light->make_call();

  if (packet.has_power) {
    call.set_state(packet.power);
  }
  if (packet.has_brightness && apply_visual_state) {
    call.set_brightness(packet.brightness / 255.0f);
  }
  if (packet.has_color && apply_visual_state) {
    CFXSyncLightSnapshot snapshot;
    snapshot.red = packet.red;
    snapshot.green = packet.green;
    snapshot.blue = packet.blue;
    snapshot.white = packet.source_has_white ? packet.white : 0;
    snapshot.color_brightness =
        packet.has_color_brightness ? packet.color_brightness : 255;
    snapshot.has_white = packet.source_has_white;

    if (light_supports_rgb_white(*light)) {
      const auto converted = convert_color_for_follower(snapshot, true);
      call.set_color_mode(light::ColorMode::RGB_WHITE);
      call.set_color_brightness(converted.color_brightness / 255.0f);
      call.set_rgb(converted.red / 255.0f, converted.green / 255.0f,
                   converted.blue / 255.0f);
      call.set_white(converted.white / 255.0f);
    } else if (light_supports_rgb(*light)) {
      const auto converted = convert_color_for_follower(snapshot, false);
      call.set_color_mode(light::ColorMode::RGB);
      call.set_color_brightness(converted.color_brightness / 255.0f);
      call.set_rgb(converted.red / 255.0f, converted.green / 255.0f,
                   converted.blue / 255.0f);
    }
  } else if (packet.has_color_brightness && apply_visual_state) {
    call.set_color_brightness(packet.color_brightness / 255.0f);
  }
  if (packet.has_cold_warm_white && apply_visual_state &&
      light_supports_cold_warm_white(*light)) {
    call.set_color_mode(light::ColorMode::COLD_WARM_WHITE);
    if (packet.has_color_temperature &&
        light_supports_color_temperature(*light)) {
      call.set_color_temperature(packet.color_temperature_mireds);
    }
    call.set_cold_white(packet.cold_white / 255.0f);
    call.set_warm_white(packet.warm_white / 255.0f);
  } else if (packet.has_color_temperature && apply_visual_state &&
             light_supports_color_temperature(*light)) {
    call.set_color_mode(light::ColorMode::COLOR_TEMPERATURE);
    call.set_color_temperature(packet.color_temperature_mireds);
  }

  const bool may_select_effect =
      apply_visual_state && (!packet.has_power || packet.power);
  if (packet.has_effect && may_select_effect) {
    std::string desired_effect{"None"};
    bool fallback = false;
    const bool identity_changed =
        !log_state.valid ||
        log_state.kind != packet.effect.kind ||
        log_state.effect_id != packet.effect.effect_id ||
        log_state.name != packet.effect.name;

    if (packet.effect.kind == CFXSyncEffectKind::CHIMERAFX) {
      const auto *entry =
          find_effect_entry(catalog, packet.effect.effect_id,
                            packet.effect.name);
      if (entry != nullptr) {
        desired_effect = entry->name;
      } else {
        fallback = true;
      }
    } else if (packet.effect.kind ==
               CFXSyncEffectKind::UNSUPPORTED) {
      fallback = true;
    }

    if (fallback) {
      const uint32_t now = millis();
      const bool should_log =
          identity_changed ||
          now - log_state.last_log_ms >=
              EFFECT_FALLBACK_LOG_INTERVAL_MS;

      if (should_log) {
        if (packet.effect.kind == CFXSyncEffectKind::CHIMERAFX) {
          ESP_LOGI(
              TAG,
              "Leader effect id=%u name='%s' is missing for follower "
              "light '%s' [%u]; using None",
              packet.effect.effect_id, packet.effect.name.c_str(),
              light->get_name().c_str(),
              static_cast<unsigned>(light_index));
        } else {
          ESP_LOGI(
              TAG,
              "Leader effect '%s' is unsupported for follower light "
              "'%s' [%u]; using None",
              packet.effect.name.c_str(), light->get_name().c_str(),
              static_cast<unsigned>(light_index));
        }
        log_state.last_log_ms = now;
      }
    }

    log_state.valid = true;
    log_state.kind = packet.effect.kind;
    log_state.effect_id = packet.effect.effect_id;
    log_state.name = packet.effect.name;

    if (light->get_effect_name() != desired_effect) {
      call.set_effect(desired_effect);
    }
  }
  if (turning_off_effect) {
    if (use_remote_effect_off_transition) {
      light->set_default_transition_length(packet.transition_ms);
    }
    call.set_transition_length(0);
  } else if (packet.has_ramp && packet.has_brightness) {
    call.set_transition_length(packet.ramp_ms);
  } else if (packet.has_transition &&
             light->get_default_transition_length() == 0) {
    call.set_transition_length(packet.transition_ms);
  }

  call.perform();
  if (use_remote_effect_off_transition) {
    light->set_default_transition_length(saved_default_transition);
  }
  return true;
#endif
}

#if defined(USE_ESP32)
void CFXSyncComponent::apply_remote_controls_to_light_(
    const CFXSyncPacket &packet, size_t light_index) {
  if (!packet.has_controls || light_index >= this->lights_.size() ||
      light_index >= this->control_bindings_.size()) {
    return;
  }

  auto *light = this->lights_[light_index];
  auto &binding = this->control_bindings_[light_index];
  const auto &controls = packet.controls;

  if (controls.has_force_white) {
    if (binding.force_white == nullptr) {
      this->log_control_skip_(binding, light, light_index, "Force White",
                              "missing control");
    } else if (binding.force_white->state != controls.force_white) {
      if (controls.force_white) {
        binding.force_white->turn_on();
      } else {
        binding.force_white->turn_off();
      }
    }
  }

  if (controls.has_intro) {
    if (binding.intro == nullptr) {
      this->log_control_skip_(binding, light, light_index, "Intro",
                              "missing control");
    } else if (!binding.intro->has_index(controls.intro)) {
      this->log_control_skip_(binding, light, light_index, "Intro",
                              "unsupported option index");
    } else {
      const auto active = binding.intro->active_index();
      if (!active.has_value() || active.value() != controls.intro) {
        auto call = binding.intro->make_call();
        call.set_index(controls.intro);
        call.perform();
      }
    }
  }

  if (controls.has_outro) {
    if (binding.outro == nullptr) {
      this->log_control_skip_(binding, light, light_index, "Outro",
                              "missing control");
    } else if (!binding.outro->has_index(controls.outro)) {
      this->log_control_skip_(binding, light, light_index, "Outro",
                              "unsupported option index");
    } else {
      const auto active = binding.outro->active_index();
      if (!active.has_value() || active.value() != controls.outro) {
        auto call = binding.outro->make_call();
        call.set_index(controls.outro);
        call.perform();
      }
    }
  }

  if (controls.has_inout_duration) {
    if (binding.inout_duration == nullptr) {
      this->log_control_skip_(binding, light, light_index, "In/Out Duration",
                              "missing control");
    } else {
      const float target =
          controls.inout_duration_deciseconds / 10.0f;
      if (!binding.inout_duration->has_state() ||
          std::fabs(binding.inout_duration->state - target) > 0.01f) {
        auto call = binding.inout_duration->make_call();
        call.set_value(target);
        call.perform();
      }
    }
  }

  if (controls.has_speed) {
    if (binding.speed == nullptr) {
      this->log_control_skip_(binding, light, light_index, "Speed",
                              "missing control");
    } else {
      const float target = controls.speed;
      if (!binding.speed->has_state() ||
          std::fabs(binding.speed->state - target) > 0.01f) {
        auto call = binding.speed->make_call();
        call.set_value(target);
        call.perform();
      }
    }
  }

  if (controls.has_intensity) {
    if (binding.intensity == nullptr) {
      this->log_control_skip_(binding, light, light_index, "Intensity",
                              "missing control");
    } else {
      const float target = controls.intensity;
      if (!binding.intensity->has_state() ||
          std::fabs(binding.intensity->state - target) > 0.01f) {
        auto call = binding.intensity->make_call();
        call.set_value(target);
        call.perform();
      }
    }
  }

  if (controls.has_mirror) {
    if (binding.mirror == nullptr) {
      this->log_control_skip_(binding, light, light_index, "Mirror",
                              "missing control");
    } else if (binding.mirror->state != controls.mirror) {
      if (controls.mirror) {
        binding.mirror->turn_on();
      } else {
        binding.mirror->turn_off();
      }
    }
  }

  if (controls.has_palette) {
    if (binding.palette == nullptr) {
      this->log_control_skip_(binding, light, light_index, "Palette",
                              "missing control");
    } else if (!binding.palette->has_index(controls.palette)) {
      this->log_control_skip_(binding, light, light_index, "Palette",
                              "unsupported option index");
    } else {
      const auto active = binding.palette->active_index();
      if (!active.has_value() || active.value() != controls.palette) {
        auto call = binding.palette->make_call();
        call.set_index(controls.palette);
        call.perform();
      }
    }
  }
}

void CFXSyncComponent::register_control_callbacks_(size_t light_index) {
  if (light_index >= this->control_bindings_.size()) {
    return;
  }
  auto &binding = this->control_bindings_[light_index];
  if (binding.callbacks_registered) {
    return;
  }
  binding.callbacks_registered = true;
  if (binding.force_white != nullptr) {
    binding.force_white->add_on_state_callback(
        [this](bool) { this->on_local_control_update(); });
  }
  if (binding.intro != nullptr) {
    binding.intro->add_on_state_callback(
        [this](size_t) { this->on_local_control_update(); });
  }
  if (binding.outro != nullptr) {
    binding.outro->add_on_state_callback(
        [this](size_t) { this->on_local_control_update(); });
  }
  if (binding.inout_duration != nullptr) {
    binding.inout_duration->add_on_state_callback(
        [this](float) { this->on_local_control_update(); });
  }
  if (binding.speed != nullptr) {
    binding.speed->add_on_state_callback(
        [this](float) { this->on_local_control_update(); });
  }
  if (binding.intensity != nullptr) {
    binding.intensity->add_on_state_callback(
        [this](float) { this->on_local_control_update(); });
  }
  if (binding.mirror != nullptr) {
    binding.mirror->add_on_state_callback(
        [this](bool) { this->on_local_control_update(); });
  }
  if (binding.palette != nullptr) {
    binding.palette->add_on_state_callback(
        [this](size_t) { this->on_local_control_update(); });
  }
}

CFXSyncControlState CFXSyncComponent::capture_control_state_(
    size_t light_index) const {
  CFXSyncControlState result;
  if (light_index >= this->control_bindings_.size()) {
    return result;
  }

  const auto &binding = this->control_bindings_[light_index];
  if (binding.force_white != nullptr) {
    result.has_force_white = true;
    result.force_white = binding.force_white->state;
  }
  if (binding.intro != nullptr) {
    const auto active = binding.intro->active_index();
    if (active.has_value() &&
        active.value() <= std::numeric_limits<uint8_t>::max()) {
      result.has_intro = true;
      result.intro = static_cast<uint8_t>(active.value());
    }
  }
  if (binding.outro != nullptr) {
    const auto active = binding.outro->active_index();
    if (active.has_value() &&
        active.value() <= std::numeric_limits<uint8_t>::max()) {
      result.has_outro = true;
      result.outro = static_cast<uint8_t>(active.value());
    }
  }
  if (binding.inout_duration != nullptr &&
      binding.inout_duration->has_state()) {
    const float deciseconds = binding.inout_duration->state * 10.0f;
    const long rounded = std::lround(deciseconds);
    result.has_inout_duration = true;
    result.inout_duration_deciseconds = static_cast<uint16_t>(
        std::max<long>(0, std::min<long>(
                              rounded,
                              std::numeric_limits<uint16_t>::max())));
  }
  if (binding.speed != nullptr && binding.speed->has_state()) {
    const long rounded = std::lround(binding.speed->state);
    result.has_speed = true;
    result.speed = static_cast<uint8_t>(
        std::max<long>(0, std::min<long>(
                              rounded,
                              std::numeric_limits<uint8_t>::max())));
  }
  if (binding.intensity != nullptr && binding.intensity->has_state()) {
    const long rounded = std::lround(binding.intensity->state);
    result.has_intensity = true;
    result.intensity = static_cast<uint8_t>(
        std::max<long>(0, std::min<long>(
                              rounded,
                              std::numeric_limits<uint8_t>::max())));
  }
  if (binding.mirror != nullptr) {
    result.has_mirror = true;
    result.mirror = binding.mirror->state;
  }
  if (binding.palette != nullptr) {
    const auto active = binding.palette->active_index();
    if (active.has_value() &&
        active.value() <= std::numeric_limits<uint8_t>::max()) {
      result.has_palette = true;
      result.palette = static_cast<uint8_t>(active.value());
    }
  }
  return result;
}

void CFXSyncComponent::log_control_skip_(
    ControlBinding &binding, light::LightState *light, size_t light_index,
    const char *control_name, const char *reason) {
  const uint32_t now = millis();
  if (binding.last_skip_log_ms != 0 &&
      now - binding.last_skip_log_ms < CONTROL_SKIP_LOG_INTERVAL_MS) {
    return;
  }
  binding.last_skip_log_ms = now;
  ESP_LOGI(TAG,
           "Leader control %s is unavailable for follower light '%s' [%u]: %s",
           control_name, light != nullptr ? light->get_name().c_str() : "<null>",
           static_cast<unsigned>(light_index), reason);
}

#endif  // defined(USE_ESP32)

bool CFXSyncComponent::is_broadcast_(const uint8_t *address) const {
  if (address == nullptr) {
    return false;
  }
  for (size_t i = 0; i < 6; i++) {
    if (address[i] != 0xFF) {
      return false;
    }
  }
  return true;
}

const char *CFXSyncComponent::role_name_() const {
  if (this->role_ == CFXSyncRole::LEADER) {
    return "leader";
  }
  if (this->role_ == CFXSyncRole::CFX_SYNC_ROLE_CONTROLLER) {
    return "controller";
  }
  if (this->role_ == CFXSyncRole::SATELLITE) {
    return "satellite";
  }
  return "follower";
}

const char *CFXSyncComponent::transport_name_() const {
  const bool udp = this->use_udp_transport_();
  const bool espnow = this->use_espnow_transport_();
  if (udp && espnow) {
    return "ESP-NOW + UDP bridge";
  }
  if (espnow) {
    return "ESP-NOW";
  }
  if (udp) {
    return "UDP";
  }
  return "none";
}

}  // namespace cfx_sync
}  // namespace esphome

#endif  // defined(USE_ESP32) || defined(USE_ESP8266)
