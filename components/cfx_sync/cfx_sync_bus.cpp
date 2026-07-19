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

bool CFXSyncBus::begin_udp(uint16_t port) {
  if (this->udp_.is_ready() && this->udp_port_ == port) {
    return true;
  }
  if (!this->udp_.begin(port)) {
    return false;
  }
  this->udp_port_ = port;
  return true;
}

void CFXSyncBus::poll() { this->udp_.poll(this); }

bool CFXSyncBus::send_udp(const std::vector<uint8_t> &packet) {
  return this->udp_.send_broadcast(packet);
}

bool CFXSyncBus::send_udp_to(uint32_t address, uint16_t port,
                             const std::vector<uint8_t> &packet) {
  return this->udp_.send_unicast(address, port, packet);
}

bool CFXSyncBus::dispatch_packet(const CFXSyncSource &source,
                                 const uint8_t *data, size_t size) {
  uint32_t packet_group_hash = 0;
  const auto result =
      CFXSyncPacketCodec::peek_group_hash(data, size, packet_group_hash);
  if (result == CFXSyncDecodeResult::NOT_CFX) {
    return false;
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
    return false;
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
  this->espnow_->disable();
}

void CFXSyncBus::enable_espnow() {
  if (this->espnow_ == nullptr) {
    return;
  }
  this->espnow_->enable();
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
