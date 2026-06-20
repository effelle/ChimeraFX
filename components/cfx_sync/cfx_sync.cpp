#include "cfx_sync.h"

#ifdef USE_ESP32

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

#include <esp_random.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <inttypes.h>
#include <limits>

namespace esphome {
namespace cfx_sync {

static const char *const TAG = "cfx_sync";

const char *node_role_name(CFXSyncNodeRole role) {
  switch (role) {
    case CFXSyncNodeRole::LEADER:
      return "leader";
    case CFXSyncNodeRole::FOLLOWER:
      return "follower";
    case CFXSyncNodeRole::REMOTE:
      return "remote";
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

light::LightState *CFXSyncComponent::leader_light_() const {
  if (this->role_ != CFXSyncRole::LEADER || this->lights_.size() != 1) {
    return nullptr;
  }
  return this->lights_[0];
}

CFXSyncNodeRole CFXSyncComponent::local_node_role_() const {
  return this->role_ == CFXSyncRole::LEADER ? CFXSyncNodeRole::LEADER
                                            : CFXSyncNodeRole::FOLLOWER;
}

uint16_t CFXSyncComponent::local_capabilities_() const {
  return this->role_ == CFXSyncRole::LEADER
             ? CFXSyncPacketCodec::CAP_LIGHT_LEADER
             : CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER;
}

bool CFXSyncComponent::accepts_peer_role_(CFXSyncNodeRole role) const {
  if (this->role_ == CFXSyncRole::LEADER) {
    return role == CFXSyncNodeRole::FOLLOWER;
  }
  return role == CFXSyncNodeRole::LEADER;
}

bool CFXSyncComponent::should_send_state_for_hello_(
    const PeerState &peer, bool new_peer, bool peer_rebooted) const {
  if (this->role_ != CFXSyncRole::LEADER ||
      !this->peer_accepts_leader_state_(peer)) {
    return false;
  }
  return new_peer || peer_rebooted ||
         peer.last_state_sent_sequence == 0 ||
         this->has_pending_ack_(peer);
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
  if (this->espnow_ == nullptr || this->lights_.empty()) {
    ESP_LOGE(TAG, "ESP-NOW and at least one light reference are required");
    this->mark_failed();
    return;
  }
  if (this->role_ == CFXSyncRole::LEADER && this->lights_.size() != 1) {
    ESP_LOGE(TAG, "Leader requires exactly one light reference");
    this->mark_failed();
    return;
  }

  this->boot_id_ = esp_random();
  if (this->boot_id_ == 0) {
    this->boot_id_ = 1;
  }
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)
  this->espnow_->register_received_handler(this);
  this->espnow_->register_unknown_peer_handler(this);
  this->espnow_->register_broadcasted_handler(this);
#else
  this->espnow_->register_receive_handler(this);
  this->espnow_->register_unknown_peer_handler(this);
  this->espnow_->register_broadcast_handler(this);
#endif
  const esp_err_t broadcast_result =
      this->espnow_->add_peer(BROADCAST_MAC.data());
  if (broadcast_result != ESP_OK) {
    ESP_LOGD(TAG, "ESP-NOW broadcast peer add skipped: %s",
             esp_err_to_name(broadcast_result));
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
  } else {
    this->schedule_follower_recovery_();
  }
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
                "  Last valid packet age: %" PRIu32 " ms\n"
                "  Peers: active=%u followers=%u remotes=%u\n"
                "  ACK: pending=%u missed=%" PRIu32,
                this->role_name_(), this->group_hash_,
                this->heartbeat_ms_, packet_age,
                static_cast<unsigned>(this->active_peer_count_()),
                static_cast<unsigned>(this->follower_peer_count_()),
                static_cast<unsigned>(this->remote_peer_count_()),
                static_cast<unsigned>(this->pending_ack_count_()),
                this->missed_ack_count_());
  ESP_LOGCONFIG(TAG, "  Lights: %u",
                static_cast<unsigned>(this->lights_.size()));
  for (size_t i = 0; i < this->lights_.size(); i++) {
    auto *light = this->lights_[i];
    ESP_LOGCONFIG(TAG, "    [%u] %s", static_cast<unsigned>(i),
                  light != nullptr ? light->get_name().c_str() : "<null>");
  }
  ESP_LOGCONFIG(TAG,
                "  Packets: sent=%" PRIu32 " received=%" PRIu32
                " malformed=%" PRIu32 " auth_failed=%" PRIu32
                " wrong_group=%" PRIu32 " stale=%" PRIu32
                " unsupported=%" PRIu32 " send_failed=%" PRIu32,
                this->sent_packets_, this->received_packets_,
                this->malformed_packets_, this->authentication_failures_,
                this->wrong_group_packets_, this->stale_packets_,
                this->unsupported_packets_, this->send_failures_);
}

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)
bool CFXSyncComponent::on_received(const espnow::ESPNowRecvInfo &info,
                                   const uint8_t *data, uint8_t size) {
  return this->handle_packet_(info, data, size);
}

bool CFXSyncComponent::on_unknown_peer(const espnow::ESPNowRecvInfo &info,
                                       const uint8_t *data, uint8_t size) {
  return this->admit_unknown_peer_(info, data, size);
}

bool CFXSyncComponent::on_broadcasted(const espnow::ESPNowRecvInfo &info,
                                      const uint8_t *data, uint8_t size) {
  return this->handle_packet_(info, data, size);
}
#else
bool CFXSyncComponent::on_receive(const espnow::ESPNowRecvInfo &info,
                                  const uint8_t *data, uint8_t size) {
  return this->handle_packet_(info, data, size);
}

bool CFXSyncComponent::on_unknown_peer(const espnow::ESPNowRecvInfo &info,
                                       const uint8_t *data, uint8_t size) {
  return this->admit_unknown_peer_(info, data, size);
}

bool CFXSyncComponent::on_broadcast(const espnow::ESPNowRecvInfo &info,
                                    const uint8_t *data, uint8_t size) {
  return this->handle_packet_(info, data, size);
}
#endif

bool CFXSyncComponent::admit_unknown_peer_(
    const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) {
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
      packet.type != CFXSyncPacketType::STATE_ACK) {
    this->log_rejection_(
        "Ignoring authenticated non-discovery packet from unknown peer");
    return true;
  }

  return this->handle_decoded_packet_(info, packet);
}

bool CFXSyncComponent::handle_packet_(const espnow::ESPNowRecvInfo &info,
                                      const uint8_t *data, uint8_t size) {
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

  return this->handle_decoded_packet_(info, packet);
}

bool CFXSyncComponent::handle_decoded_packet_(
    const espnow::ESPNowRecvInfo &info, const CFXSyncPacket &packet) {
  auto *peer = this->find_peer_(info.src_addr);
  if (packet.type == CFXSyncPacketType::HELLO) {
    if (this->role_ == CFXSyncRole::FOLLOWER &&
        packet.node_role == CFXSyncNodeRole::FOLLOWER) {
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
    peer = this->find_or_add_peer_(info.src_addr, packet.node_role,
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
    if (this->role_ == CFXSyncRole::LEADER &&
        this->should_send_state_for_hello_(*peer, new_peer,
                                           peer_rebooted)) {
      this->send_state_();
    }
    return true;
  }

  if (packet.type == CFXSyncPacketType::SYNC_REQUEST) {
    if (this->role_ != CFXSyncRole::LEADER) {
      this->log_rejection_("Ignoring SYNC_REQUEST for incompatible role");
      return true;
    }
    if (peer == nullptr) {
      peer = this->find_or_add_peer_(info.src_addr,
                                     CFXSyncNodeRole::FOLLOWER,
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
    return true;
  }

  if (peer == nullptr && packet.type == CFXSyncPacketType::STATE &&
      this->role_ == CFXSyncRole::FOLLOWER) {
    peer = this->find_or_add_peer_(info.src_addr, CFXSyncNodeRole::LEADER,
                                   CFXSyncPacketCodec::CAP_LIGHT_LEADER);
    if (peer != nullptr) {
      this->register_peer_(*peer);
    }
  }
  if (peer == nullptr && packet.type == CFXSyncPacketType::STATE_ACK &&
      this->role_ == CFXSyncRole::LEADER &&
      this->is_current_broadcast_ack_(packet)) {
    peer = this->find_or_add_peer_(info.src_addr, CFXSyncNodeRole::FOLLOWER,
                                   CFXSyncPacketCodec::CAP_LIGHT_FOLLOWER);
    if (peer != nullptr) {
      this->register_peer_(*peer);
      this->seed_peer_sent_state_from_ack_(*peer, packet);
    }
  }
  if (packet.type == CFXSyncPacketType::STATE_ACK &&
      this->role_ != CFXSyncRole::LEADER) {
    return true;
  }
  if (peer == nullptr) {
    this->log_rejection_("Ignoring authenticated packet from unknown peer");
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

  if (packet.type == CFXSyncPacketType::STATE_ACK) {
    if (this->role_ == CFXSyncRole::LEADER) {
      this->handle_state_ack_(*peer, packet);
    }
    return true;
  }

  if (packet.type == CFXSyncPacketType::STATE &&
      this->role_ == CFXSyncRole::FOLLOWER &&
      (packet.has_power || packet.has_brightness || packet.has_color ||
       packet.has_color_brightness || packet.has_effect ||
       packet.has_controls)) {
    this->has_valid_state_ = true;
    this->clear_warning_if_set_();
    this->apply_remote_state_(packet);
    this->schedule_state_ack_(info.src_addr, packet,
                              CFXSyncAckResult::APPLIED);
  }
  return true;
}

void CFXSyncComponent::on_local_light_update() {
  auto *leader = this->leader_light_();
  if (this->applying_remote_state_ || leader == nullptr ||
      this->effect_catalogs_.empty()) {
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
  this->send_state_(snapshot, effect, controls);
}

void CFXSyncComponent::on_local_control_update() {
  this->on_local_light_update();
}

bool CFXSyncComponent::send_state_() {
  auto *leader = this->leader_light_();
  if (leader == nullptr || this->effect_catalogs_.empty()) {
    return false;
  }
  const auto snapshot = capture_light_snapshot(*leader);
  const auto effect = capture_effect_state(leader, this->effect_catalogs_[0]);
  const auto controls = this->capture_control_state_(0);
  return this->send_state_to_followers_(snapshot, effect, controls);
}

bool CFXSyncComponent::send_state_(
    const CFXSyncLightSnapshot &snapshot,
    const CFXSyncEffectState &effect,
    const CFXSyncControlState &controls) {
  return this->send_state_to_followers_(snapshot, effect, controls);
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
  return this->send_state_to_followers_(snapshot, effect, controls);
}

bool CFXSyncComponent::send_state_to_followers_(
    const CFXSyncLightSnapshot &snapshot,
    const CFXSyncEffectState &effect,
    const CFXSyncControlState &controls) {
  if (this->send_pending_) {
    this->state_send_deferred_ = true;
    return false;
  }

  std::vector<uint8_t> packet;
  const uint32_t sequence = this->next_sequence_();
  if (!CFXSyncPacketCodec::encode_state(
          this->group_hash_, this->boot_id_, sequence, snapshot.power,
          snapshot.brightness, snapshot.color_brightness, snapshot.red,
          snapshot.green, snapshot.blue, snapshot.white, snapshot.has_white,
          true, effect, controls.has_any(), controls, this->key_, packet)) {
    return false;
  }
  if (!this->send_packet_to_(BROADCAST_MAC, packet)) {
    return false;
  }
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
         peer.node_role == CFXSyncNodeRole::FOLLOWER &&
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
  const uint32_t sequence = this->next_sequence_();
  if (!CFXSyncPacketCodec::encode_state(
          this->group_hash_, this->boot_id_, sequence, snapshot.power,
          snapshot.brightness, snapshot.color_brightness, snapshot.red,
          snapshot.green, snapshot.blue, snapshot.white, snapshot.has_white,
          true, effect, controls.has_any(), controls, this->key_, packet)) {
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
  if (this->role_ != CFXSyncRole::FOLLOWER) {
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

bool CFXSyncComponent::send_packet_(std::vector<uint8_t> &packet) {
  if (!this->has_static_peer_) {
    return false;
  }
  return this->send_packet_to_(this->peer_, packet);
}

bool CFXSyncComponent::send_packet_to_(const std::array<uint8_t, 6> &mac,
                                       std::vector<uint8_t> &packet) {
  if (this->espnow_ == nullptr) {
    return false;
  }
  if (this->send_pending_) {
    return false;
  }

  this->send_pending_ = true;
  const esp_err_t result = this->espnow_->send(
      mac.data(), packet,
      [this](esp_err_t send_result) {
        this->send_pending_ = false;
        this->handle_send_result_(send_result);
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
}

bool CFXSyncComponent::send_packet_to_peer_(PeerState &peer,
                                            std::vector<uint8_t> &packet) {
  if (this->espnow_ == nullptr) {
    return false;
  }
  if (this->send_pending_) {
    return false;
  }

  this->send_pending_ = true;
  const esp_err_t result = this->espnow_->send(
      peer.mac.data(), packet,
      [this, &peer](esp_err_t send_result) {
        this->send_pending_ = false;
        this->handle_peer_send_result_(peer, send_result);
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

bool CFXSyncComponent::register_peer_(PeerState &peer) {
  if (!peer.active || this->espnow_ == nullptr ||
      this->is_broadcast_(peer.mac.data())) {
    return false;
  }
  if (peer.registered) {
    return true;
  }
  const esp_err_t result = this->espnow_->add_peer(peer.mac.data());
  if (result != ESP_OK) {
    char peer_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
    format_mac_addr_upper(peer.mac.data(), peer_buf);
    ESP_LOGW(TAG, "CFX Sync failed to register peer %s: %s", peer_buf,
             esp_err_to_name(result));
    return false;
  }
  peer.registered = true;
  return true;
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
  uint8_t count = 0;
  for (const auto &peer : this->peers_) {
    if (this->peer_accepts_leader_state_(peer) &&
        this->has_pending_ack_(peer)) {
      count++;
    }
  }
  return count;
}

uint32_t CFXSyncComponent::missed_ack_count_() const {
  uint32_t count = 0;
  for (const auto &peer : this->peers_) {
    count += peer.missed_acks;
  }
  return count;
}

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

void CFXSyncComponent::clear_warning_if_set_() {
  if (this->status_has_warning()) {
    this->status_clear_warning();
  }
}

void CFXSyncComponent::flush_deferred_state_() {
  if (!this->state_send_deferred_ || this->send_pending_ ||
      this->role_ != CFXSyncRole::LEADER) {
    return;
  }
  this->state_send_deferred_ = false;
  this->send_state_();
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
    this->send_state_();
    this->state_retry_active_ = false;
  });
}

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
    ESP_LOGD(TAG, "%s", message);
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
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr) {
    return wifi::global_wifi_component->is_connected();
  }
#endif
  return true;
}

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

void CFXSyncComponent::schedule_follower_recovery_() {
  this->schedule_follower_recovery_attempt_("sync-request-1",
                                            FOLLOWER_RECOVERY_FIRST_MS);
  this->schedule_follower_recovery_attempt_("sync-request-2",
                                            FOLLOWER_RECOVERY_SECOND_MS);
  this->schedule_follower_recovery_attempt_("sync-request-3",
                                            FOLLOWER_RECOVERY_THIRD_MS);
  this->set_timeout("sync-recovery-expired", FOLLOWER_RECOVERY_EXPIRE_MS,
                    [this]() {
                      if (!this->has_valid_state_) {
                        ESP_LOGW(TAG,
                                 "No valid leader state received during "
                                 "startup recovery");
                        this->status_set_warning();
                        this->schedule_follower_recovery_loop_();
                      }
                    });
}

void CFXSyncComponent::schedule_follower_recovery_loop_() {
  if (this->role_ == CFXSyncRole::LEADER || this->has_valid_state_) {
    return;
  }
  const uint32_t delay_ms =
      FOLLOWER_RECOVERY_REPEAT_MS +
      (esp_random() % (FOLLOWER_RECOVERY_REPEAT_JITTER_SPREAD_MS + 1));
  this->set_timeout("sync-recovery-loop", delay_ms, [this]() {
    if (this->role_ == CFXSyncRole::LEADER || this->has_valid_state_) {
      return;
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
    if (!this->has_valid_state_ && this->boot_radio_ready_()) {
      this->send_sync_request_to_(BROADCAST_MAC);
    }
  });
}

void CFXSyncComponent::apply_remote_state_(const CFXSyncPacket &packet) {
  RemoteApplyGuard guard(this->applying_remote_state_);
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
    this->apply_remote_state_to_light_(packet, i);
  }
}

void CFXSyncComponent::apply_remote_state_to_light_(
    const CFXSyncPacket &packet, size_t light_index) {
  if (light_index >= this->lights_.size() ||
      light_index >= this->effect_catalogs_.size() ||
      light_index >= this->effect_log_states_.size()) {
    return;
  }
  auto *light = this->lights_[light_index];
  if (light == nullptr) {
    return;
  }
  this->apply_remote_controls_to_light_(packet, light_index);

  const auto &catalog = this->effect_catalogs_[light_index];
  auto &log_state = this->effect_log_states_[light_index];
  auto call = light->make_call();

  if (packet.has_power) {
    call.set_state(packet.power);
  }
  if (packet.has_brightness) {
    call.set_brightness(packet.brightness / 255.0f);
  }
  if (packet.has_color) {
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
  } else if (packet.has_color_brightness) {
    call.set_color_brightness(packet.color_brightness / 255.0f);
  }

  const bool may_select_effect = !packet.has_power || packet.power;
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

  call.perform();
}

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
  return this->role_ == CFXSyncRole::LEADER ? "leader" : "follower";
}

}  // namespace cfx_sync
}  // namespace esphome

#endif  // USE_ESP32
