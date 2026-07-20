/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Shared transport owner for ChimeraFX group sync.
 */

#pragma once

#if defined(USE_ESP32) || defined(USE_ESP8266)

#include "cfx_sync_transport.h"
#include "cfx_sync_udp.h"
#include "esphome/core/defines.h"
#ifdef USE_ESPNOW
#include "esphome/components/espnow/espnow_component.h"
#include <esp_err.h>
#endif
#include "esphome/core/version.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace esphome {
namespace cfx_sync {

class CFXSyncComponent;

#ifdef USE_ESPNOW
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 7, 0)
using CFXSyncESPNowPacketSize = uint16_t;
#else
using CFXSyncESPNowPacketSize = uint8_t;
#endif
#endif

class CFXSyncBus
#ifdef USE_ESPNOW
    : public espnow::ESPNowReceivedPacketHandler,
      public espnow::ESPNowUnknownPeerHandler,
      public espnow::ESPNowBroadcastHandler
#endif
{
 public:
  void register_group(CFXSyncComponent *group);
  bool register_shared_transport_consumer(
      CFXSyncSharedTransportConsumer *consumer);
  bool unregister_shared_transport_consumer(
      CFXSyncSharedTransportConsumer *consumer);
  bool has_active_group() const { return this->group_count_ != 0; }
  bool is_udp_ready() const { return this->udp_.is_ready(); }
  uint16_t udp_port() const { return this->udp_port_; }
  uint32_t recovery_generation() const {
    return this->recovery_generation_;
  }

#ifdef USE_ESPNOW
  void set_espnow(espnow::ESPNowComponent *espnow) {
    this->espnow_ = espnow;
  }
  bool has_espnow() const { return this->espnow_ != nullptr; }
  bool is_espnow_ready() const {
    return this->espnow_registered_ && this->espnow_enabled_;
  }
  bool begin_espnow();
  bool add_espnow_peer(const uint8_t *mac);
  void disable_espnow();
  void enable_espnow();

  template<typename Callback>
  esp_err_t send_espnow(const uint8_t *mac, const uint8_t *data, size_t size,
                        Callback &&callback) {
    if (!this->is_espnow_ready()) {
      return ESP_FAIL;
    }
    if (mac == nullptr || data == nullptr || size == 0 ||
        size > CFX_SYNC_SHARED_TRANSPORT_MTU) {
      return ESP_ERR_INVALID_ARG;
    }
    return this->espnow_->send(mac, data, size,
                               std::forward<Callback>(callback));
  }

  template<typename Callback>
  esp_err_t send_espnow(const uint8_t *mac,
                        const std::vector<uint8_t> &packet,
                        Callback &&callback) {
    return this->send_espnow(mac, packet.data(), packet.size(),
                             std::forward<Callback>(callback));
  }

  bool on_receive(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                  CFXSyncESPNowPacketSize size) override;
  bool on_unknown_peer(const espnow::ESPNowRecvInfo &info,
                       const uint8_t *data,
                       CFXSyncESPNowPacketSize size) override;
  bool on_broadcast(const espnow::ESPNowRecvInfo &info, const uint8_t *data,
                    CFXSyncESPNowPacketSize size) override;
#else
  bool is_espnow_ready() const { return false; }
#endif

  bool begin_udp(uint16_t port);
  void poll();
  bool send_udp(const uint8_t *data, size_t size);
  bool send_udp(const std::vector<uint8_t> &packet);
  bool send_udp_to(uint32_t address, uint16_t port, const uint8_t *data,
                   size_t size);
  bool send_udp_to(uint32_t address, uint16_t port,
                   const std::vector<uint8_t> &packet);

  bool dispatch_packet(const CFXSyncSource &source, const uint8_t *data,
                       size_t size);
  bool dispatch_unknown_packet(const CFXSyncSource &source,
                               const uint8_t *data, size_t size);

 protected:
  static constexpr size_t MAX_GROUPS = 8;
  static constexpr size_t MAX_SHARED_TRANSPORT_CONSUMERS = 8;

  bool dispatch_shared_transport_packet_(CFXSyncReceivePath path,
                                         const CFXSyncSource &source,
                                         const uint8_t *data, size_t size);

  CFXSyncComponent *groups_[MAX_GROUPS]{};
  size_t group_count_{0};
  CFXSyncSharedTransportConsumer
      *shared_transport_consumers_[MAX_SHARED_TRANSPORT_CONSUMERS]{};
  size_t shared_transport_consumer_count_{0};

  CFXSyncUDPTransport udp_;
  uint16_t udp_port_{0};
  uint32_t recovery_generation_{0};

#ifdef USE_ESPNOW
  espnow::ESPNowComponent *espnow_{nullptr};
  bool espnow_registered_{false};
  bool espnow_enabled_{false};
#endif
};

CFXSyncBus &global_cfx_sync_bus();

}  // namespace cfx_sync
}  // namespace esphome

#endif  // defined(USE_ESP32) || defined(USE_ESP8266)
