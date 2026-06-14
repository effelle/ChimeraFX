#include "cfx_sync.h"

#ifdef USE_ESP32

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <esp_random.h>
#include <cstring>
#include <inttypes.h>

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
  this->espnow_->register_receive_handler(this);

  if (this->role_ == CFXSyncRole::LEADER) {
    auto *leader = this->leader_light_();
    this->observed_state_ = capture_light_snapshot(*leader);
    this->has_observed_state_ = true;
    leader->add_remote_values_listener(&this->light_listener_);
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

bool CFXSyncComponent::on_receive(const espnow::ESPNowRecvInfo &info,
                                  const uint8_t *data, uint8_t size) {
  if (!this->is_peer_(info.src_addr)) {
    return false;
  }

  CFXSyncPacket packet;
  const CFXSyncDecodeResult result = CFXSyncPacketCodec::decode(
      data, size, this->group_hash_, this->key_, packet);
  if (result == CFXSyncDecodeResult::NOT_CFX) {
    return false;
  }
  if (result != CFXSyncDecodeResult::OK) {
    this->handle_decode_failure_(result);
    return true;
  }
  if (!this->accept_sequence_(packet.boot_id, packet.sequence)) {
    this->stale_packets_++;
    this->log_rejection_("Ignoring duplicate or stale packet");
    return true;
  }

  this->received_packets_++;
  this->last_valid_packet_ms_ = millis();

  if (packet.type == CFXSyncPacketType::SYNC_REQUEST) {
    if (this->role_ == CFXSyncRole::LEADER) {
      this->send_state_();
    }
    return true;
  }

  if (this->role_ == CFXSyncRole::FOLLOWER &&
      (packet.has_power || packet.has_brightness || packet.has_color ||
       packet.has_color_brightness)) {
    this->has_valid_state_ = true;
    this->status_clear_warning();
    this->apply_remote_state_(packet);
  }
  return true;
}

void CFXSyncComponent::on_local_light_update() {
  auto *leader = this->leader_light_();
  if (this->applying_remote_state_ || leader == nullptr) {
    return;
  }

  const auto snapshot = capture_light_snapshot(*leader);
  if (this->has_observed_state_ && snapshot == this->observed_state_) {
    return;
  }
  this->has_observed_state_ = true;
  this->observed_state_ = snapshot;
  this->send_state_(snapshot);
}

bool CFXSyncComponent::send_state_() {
  auto *leader = this->leader_light_();
  if (leader == nullptr) {
    return false;
  }
  return this->send_state_(capture_light_snapshot(*leader));
}

bool CFXSyncComponent::send_state_(
    const CFXSyncLightSnapshot &snapshot) {
  std::vector<uint8_t> packet;
  if (!CFXSyncPacketCodec::encode_state(
          this->group_hash_, this->boot_id_, this->next_sequence_(),
          snapshot.power, snapshot.brightness, snapshot.color_brightness,
          snapshot.red, snapshot.green, snapshot.blue, snapshot.white,
          snapshot.has_white, this->key_, packet)) {
    return false;
  }
  return this->send_packet_(packet);
}

bool CFXSyncComponent::send_sync_request_() {
  if (this->role_ != CFXSyncRole::FOLLOWER) {
    return false;
  }

  std::vector<uint8_t> packet;
  if (!CFXSyncPacketCodec::encode_sync_request(
          this->group_hash_, this->boot_id_, this->next_sequence_(),
          this->key_, packet)) {
    return false;
  }
  return this->send_packet_(packet);
}

bool CFXSyncComponent::send_packet_(std::vector<uint8_t> &packet) {
  if (this->espnow_ == nullptr) {
    return false;
  }

  const esp_err_t result = this->espnow_->send(
      this->peer_.data(), packet,
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

bool CFXSyncComponent::accept_sequence_(uint32_t boot_id,
                                        uint32_t sequence) {
  if (!this->has_rx_sequence_ || boot_id != this->rx_boot_id_) {
    this->has_rx_sequence_ = true;
    this->rx_boot_id_ = boot_id;
    this->rx_sequence_ = sequence;
    return true;
  }
  if (sequence <= this->rx_sequence_) {
    return false;
  }
  this->rx_sequence_ = sequence;
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
      this->send_sync_request_();
    }
  });
  this->set_timeout("sync-request-2", 2000, [this]() {
    if (!this->has_valid_state_) {
      this->send_sync_request_();
    }
  });
  this->set_timeout("sync-request-3", 4000, [this]() {
    if (!this->has_valid_state_) {
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
  for (auto *light : this->lights_) {
    if (light == nullptr) {
      continue;
    }
    this->apply_remote_state_to_light_(packet, light);
  }
}

void CFXSyncComponent::apply_remote_state_to_light_(
    const CFXSyncPacket &packet, light::LightState *light) {
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

  call.perform();
}

bool CFXSyncComponent::is_peer_(const uint8_t *address) const {
  return address != nullptr &&
         memcmp(address, this->peer_.data(), this->peer_.size()) == 0;
}

const char *CFXSyncComponent::role_name_() const {
  return this->role_ == CFXSyncRole::LEADER ? "leader" : "follower";
}

}  // namespace cfx_sync
}  // namespace esphome

#endif  // USE_ESP32
