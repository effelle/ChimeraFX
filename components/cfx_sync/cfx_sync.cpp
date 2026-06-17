#include "cfx_sync.h"

#ifdef USE_ESP32

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <esp_random.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <inttypes.h>
#include <limits>

namespace esphome {
namespace cfx_sync {

static const char *const TAG = "cfx_sync";

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

void CFXSyncComponent::set_peer(const std::array<uint8_t, 6> &peer) {
  this->peer_ = peer;
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
  this->espnow_->register_broadcasted_handler(this);
#else
  this->espnow_->register_receive_handler(this);
  this->espnow_->register_broadcast_handler(this);
#endif
  const esp_err_t broadcast_result =
      this->espnow_->add_peer(BROADCAST_MAC.data());
  if (broadcast_result != ESP_OK) {
    ESP_LOGD(TAG, "ESP-NOW broadcast peer add skipped: %s",
             esp_err_to_name(broadcast_result));
  }
  this->send_hello_();
  this->set_interval("hello", HELLO_INTERVAL_MS,
                     [this]() { this->send_hello_(); });
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
                       [this]() { this->send_state_(); });
  } else {
    this->schedule_follower_recovery_();
  }
}

void CFXSyncComponent::dump_config() {
  char peer_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  format_mac_addr_upper(this->peer_.data(), peer_buf);
  const uint32_t packet_age =
      this->last_valid_packet_ms_ == 0
          ? 0
          : millis() - this->last_valid_packet_ms_;

  ESP_LOGCONFIG(TAG,
                "CFX Sync:\n"
                "  Role: %s\n"
                "  Peer: %s\n"
                "  Group hash: %08" PRIX32 "\n"
                "  Heartbeat: %" PRIu32 " ms\n"
                "  Last valid packet age: %" PRIu32 " ms",
                this->role_name_(), peer_buf, this->group_hash_,
                this->heartbeat_ms_, packet_age);
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

bool CFXSyncComponent::on_broadcasted(const espnow::ESPNowRecvInfo &info,
                                      const uint8_t *data, uint8_t size) {
  return this->handle_packet_(info, data, size);
}
#else
bool CFXSyncComponent::on_receive(const espnow::ESPNowRecvInfo &info,
                                  const uint8_t *data, uint8_t size) {
  return this->handle_packet_(info, data, size);
}

bool CFXSyncComponent::on_broadcast(const espnow::ESPNowRecvInfo &info,
                                    const uint8_t *data, uint8_t size) {
  return this->handle_packet_(info, data, size);
}
#endif

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

  auto *peer = this->find_peer_(info.src_addr);
  if (packet.type == CFXSyncPacketType::HELLO) {
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
        this->peer_accepts_leader_state_(*peer)) {
      this->send_state_to_peer_(*peer);
    }
    return true;
  }

  if (packet.type == CFXSyncPacketType::SYNC_REQUEST) {
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
      this->send_state_to_peer_(*peer);
    }
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
    return true;
  }

  if (packet.type == CFXSyncPacketType::STATE &&
      this->role_ == CFXSyncRole::FOLLOWER &&
      (packet.has_power || packet.has_brightness || packet.has_color ||
       packet.has_color_brightness || packet.has_effect ||
       packet.has_controls)) {
    this->has_valid_state_ = true;
    this->status_clear_warning();
    this->apply_remote_state_(packet);
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
  bool attempted = false;
  for (auto &peer : this->peers_) {
    if (!this->peer_accepts_leader_state_(peer)) {
      continue;
    }
    attempted = true;
    this->send_state_to_peer_(peer, snapshot, effect, controls);
  }
  return attempted;
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
  std::vector<uint8_t> packet;
  const uint32_t sequence = this->next_sequence_();
  if (!CFXSyncPacketCodec::encode_state(
          this->group_hash_, this->boot_id_, sequence, snapshot.power,
          snapshot.brightness, snapshot.color_brightness, snapshot.red,
          snapshot.green, snapshot.blue, snapshot.white, snapshot.has_white,
          true, effect, controls.has_any(), controls, this->key_, packet)) {
    return false;
  }
  if (!this->send_packet_to_(peer.mac, packet)) {
    return false;
  }
  peer.last_state_sent_boot_id = this->boot_id_;
  peer.last_state_sent_sequence = sequence;
  return true;
}

bool CFXSyncComponent::send_sync_request_() {
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
  return this->send_packet_to_(this->peer_, packet);
}

bool CFXSyncComponent::send_packet_to_(const std::array<uint8_t, 6> &mac,
                                       std::vector<uint8_t> &packet) {
  if (this->espnow_ == nullptr) {
    return false;
  }

  const esp_err_t result = this->espnow_->send(
      mac.data(), packet,
      [this](esp_err_t send_result) {
        this->handle_send_result_(send_result);
      });
  if (result != ESP_OK) {
    this->handle_send_result_(result);
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
    return &peer;
  }

  this->log_rejection_("Ignoring peer because runtime peer table is full");
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

void CFXSyncComponent::handle_send_result_(esp_err_t result) {
  if (result == ESP_OK) {
    this->consecutive_send_failures_ = 0;
    if (this->role_ == CFXSyncRole::LEADER) {
      this->status_clear_warning();
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

void CFXSyncComponent::schedule_follower_recovery_() {
  this->set_timeout("sync-request-1", 1000, [this]() {
    if (!this->has_valid_state_) {
      this->send_hello_();
      this->send_sync_request_to_(BROADCAST_MAC);
      this->send_sync_request_();
    }
  });
  this->set_timeout("sync-request-2", 2000, [this]() {
    if (!this->has_valid_state_) {
      this->send_hello_();
      this->send_sync_request_to_(BROADCAST_MAC);
      this->send_sync_request_();
    }
  });
  this->set_timeout("sync-request-3", 4000, [this]() {
    if (!this->has_valid_state_) {
      this->send_hello_();
      this->send_sync_request_to_(BROADCAST_MAC);
      this->send_sync_request_();
    }
  });
  this->set_timeout("sync-recovery-expired", 6000, [this]() {
    if (!this->has_valid_state_) {
      ESP_LOGW(TAG, "No valid leader state received during startup recovery");
      this->status_set_warning();
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
