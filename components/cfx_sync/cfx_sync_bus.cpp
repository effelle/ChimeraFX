#include "cfx_sync_bus.h"
#include "cfx_sync.h"

#if defined(USE_ESP32) || defined(USE_ESP8266)

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESPNOW
#include <esp_err.h>
#endif
#include <cstring>

namespace esphome {
namespace cfx_sync {

static const char *const TAG = "cfx_sync.bus";

CFXSyncBus &global_cfx_sync_bus() {
  static CFXSyncBus bus;
  return bus;
}

void CFXSyncBus::register_group(CFXSyncComponent *group) {
  if (group == nullptr) {
    return;
  }
  for (size_t i = 0; i < this->group_count_; i++) {
    if (this->groups_[i] == group) {
      return;
    }
  }
  if (this->group_count_ >= MAX_GROUPS) {
    ESP_LOGW(TAG, "Maximum CFX Sync group count reached");
    return;
  }
  this->groups_[this->group_count_++] = group;
}

bool CFXSyncBus::register_shared_transport_consumer(
    CFXSyncSharedTransportConsumer *consumer) {
  if (consumer == nullptr) {
    return false;
  }
  for (size_t i = 0; i < this->shared_transport_consumer_count_; i++) {
    if (this->shared_transport_consumers_[i] == consumer) {
      return true;
    }
  }
  if (this->shared_transport_consumer_count_ >=
      MAX_SHARED_TRANSPORT_CONSUMERS) {
    ESP_LOGE(TAG, "Maximum shared transport consumer count reached");
    return false;
  }
  this->shared_transport_consumers_[this->shared_transport_consumer_count_++] =
      consumer;
  ESP_LOGV(TAG, "Registered shared transport consumer (%u/%u)",
           static_cast<unsigned>(this->shared_transport_consumer_count_),
           static_cast<unsigned>(MAX_SHARED_TRANSPORT_CONSUMERS));
  return true;
}

bool CFXSyncBus::unregister_shared_transport_consumer(
    CFXSyncSharedTransportConsumer *consumer) {
  if (consumer == nullptr) {
    return false;
  }
  for (size_t i = 0; i < this->shared_transport_consumer_count_; i++) {
    if (this->shared_transport_consumers_[i] != consumer) {
      continue;
    }
    for (size_t j = i + 1; j < this->shared_transport_consumer_count_; j++) {
      this->shared_transport_consumers_[j - 1] =
          this->shared_transport_consumers_[j];
    }
    this->shared_transport_consumer_count_--;
    this->shared_transport_consumers_[this->shared_transport_consumer_count_] =
        nullptr;
    ESP_LOGV(TAG, "Unregistered shared transport consumer (%u/%u)",
             static_cast<unsigned>(this->shared_transport_consumer_count_),
             static_cast<unsigned>(MAX_SHARED_TRANSPORT_CONSUMERS));
    return true;
  }
  return false;
}

bool CFXSyncBus::begin_udp(uint16_t port) {
  if (this->udp_.is_ready()) {
    if (this->udp_port_ == port) {
      return true;
    }
    ESP_LOGE(TAG,
             "Shared UDP transport already owns port %u; refusing port %u",
             static_cast<unsigned>(this->udp_port_),
             static_cast<unsigned>(port));
    return false;
  }
  if (!this->udp_.begin(port)) {
    return false;
  }
  this->udp_port_ = port;
  return true;
}

void CFXSyncBus::poll() { this->udp_.poll(this); }

bool CFXSyncBus::send_udp(const uint8_t *data, size_t size) {
  if (data == nullptr || size == 0 ||
      size > CFX_SYNC_SHARED_TRANSPORT_MTU) {
    return false;
  }
  return this->udp_.send_broadcast(data, size);
}

bool CFXSyncBus::send_udp(const std::vector<uint8_t> &packet) {
  return this->send_udp(packet.data(), packet.size());
}

bool CFXSyncBus::send_udp_to(uint32_t address, uint16_t port,
                             const uint8_t *data, size_t size) {
  if (data == nullptr || size == 0 ||
      size > CFX_SYNC_SHARED_TRANSPORT_MTU) {
    return false;
  }
  return this->udp_.send_unicast(address, port, data, size);
}

bool CFXSyncBus::send_udp_to(uint32_t address, uint16_t port,
                             const std::vector<uint8_t> &packet) {
  return this->send_udp_to(address, port, packet.data(), packet.size());
}

bool CFXSyncBus::dispatch_shared_transport_packet_(
    CFXSyncReceivePath path, const CFXSyncSource &source, const uint8_t *data,
    size_t size) {
  if (data == nullptr || size == 0 ||
      size > CFX_SYNC_SHARED_TRANSPORT_MTU) {
    ESP_LOGV(TAG, "Dropped invalid shared transport packet (%u bytes)",
             static_cast<unsigned>(size));
    return false;
  }
  for (size_t i = 0; i < this->shared_transport_consumer_count_; i++) {
    auto *consumer = this->shared_transport_consumers_[i];
    if (consumer != nullptr &&
        consumer->on_shared_transport_packet(path, source, data, size)) {
      return true;
    }
  }
  return false;
}

bool CFXSyncBus::dispatch_packet(const CFXSyncSource &source,
                                 const uint8_t *data, size_t size) {
  uint32_t packet_group_hash = 0;
  const auto result =
      CFXSyncPacketCodec::peek_group_hash(data, size, packet_group_hash);
  if (result == CFXSyncDecodeResult::NOT_CFX) {
    return this->dispatch_shared_transport_packet_(
        CFXSyncReceivePath::NORMAL, source, data, size);
  }

  bool handled = false;
  for (auto *group : this->groups_) {
    if (group == nullptr) {
      continue;
    }
    if (result == CFXSyncDecodeResult::OK &&
        group->group_hash() != packet_group_hash) {
      continue;
    }
    handled = group->handle_packet_(source, data, size) || handled;
  }
  return handled;
}

bool CFXSyncBus::dispatch_unknown_packet(const CFXSyncSource &source,
                                         const uint8_t *data, size_t size) {
  uint32_t packet_group_hash = 0;
  const auto result =
      CFXSyncPacketCodec::peek_group_hash(data, size, packet_group_hash);
  if (result == CFXSyncDecodeResult::NOT_CFX) {
    return this->dispatch_shared_transport_packet_(
        CFXSyncReceivePath::UNKNOWN_PEER, source, data, size);
  }

  bool handled = false;
  for (auto *group : this->groups_) {
    if (group == nullptr) {
      continue;
    }
    if (result == CFXSyncDecodeResult::OK &&
        group->group_hash() != packet_group_hash) {
      continue;
    }
    handled = group->handle_unknown_packet_(source, data, size) || handled;
  }
  return handled;
}

#ifdef USE_ESPNOW
bool CFXSyncBus::begin_espnow() {
  if (this->espnow_ == nullptr) {
    return false;
  }
  if (!this->espnow_registered_) {
    this->espnow_->register_receive_handler(this);
    this->espnow_->register_unknown_peer_handler(this);
    this->espnow_->register_broadcast_handler(this);
    this->espnow_registered_ = true;
  }
  const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF,
                                0xFF, 0xFF, 0xFF};
  const esp_err_t result = this->espnow_->add_peer(broadcast);
  if (result != ESP_OK) {
    ESP_LOGV(TAG, "ESP-NOW broadcast peer add skipped: %s",
             esp_err_to_name(result));
  }
  this->espnow_enabled_ = true;
  return true;
}

bool CFXSyncBus::add_espnow_peer(const uint8_t *mac) {
  if (this->espnow_ == nullptr || mac == nullptr) {
    return false;
  }
  return this->espnow_->add_peer(mac) == ESP_OK;
}

void CFXSyncBus::disable_espnow() {
  if (this->espnow_ == nullptr) {
    return;
  }
  this->espnow_enabled_ = false;
  this->espnow_->disable();
}

void CFXSyncBus::enable_espnow() {
  if (this->espnow_ == nullptr) {
    return;
  }
  this->espnow_->enable();
  this->espnow_enabled_ = true;
}

bool CFXSyncBus::on_receive(const espnow::ESPNowRecvInfo &info,
                            const uint8_t *data,
                            CFXSyncESPNowPacketSize size) {
  CFXSyncSource source = CFXSyncSource::from_espnow(info.src_addr);
  return this->dispatch_packet(source, data, size);
}

bool CFXSyncBus::on_unknown_peer(const espnow::ESPNowRecvInfo &info,
                                 const uint8_t *data,
                                 CFXSyncESPNowPacketSize size) {
  CFXSyncSource source = CFXSyncSource::from_espnow(info.src_addr);
  return this->dispatch_unknown_packet(source, data, size);
}

bool CFXSyncBus::on_broadcast(const espnow::ESPNowRecvInfo &info,
                              const uint8_t *data,
                              CFXSyncESPNowPacketSize size) {
  CFXSyncSource source = CFXSyncSource::from_espnow(info.src_addr);
  return this->dispatch_packet(source, data, size);
}
#endif

}  // namespace cfx_sync
}  // namespace esphome

#endif  // defined(USE_ESP32) || defined(USE_ESP8266)
