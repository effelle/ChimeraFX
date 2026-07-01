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

class CFXSyncBus
#ifdef USE_ESPNOW
    : public espnow::ESPNowReceivedPacketHandler,
      public espnow::ESPNowUnknownPeerHandler,
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 6, 0)
      public espnow::ESPNowBroadcastedHandler
#else
      public espnow::ESPNowBroadcastHandler
#endif
#endif
{
 public:
  void register_group(CFXSyncComponent *group);

#ifdef USE_ESPNOW
  void set_espnow(espnow::ESPNowComponent *espnow) {
    this->espnow_ = espnow;
  }
  bool has_espnow() const { return this->espnow_ != nullptr; }
  bool begin_espnow();
  bool add_espnow_peer(const uint8_t *mac);
  void disable_espnow();
  void enable_espnow();

  template<typename Callback>
  esp_err_t send_espnow(const uint8_t *mac, std::vector<uint8_t> &packet,
                        Callback &&callback) {
    if (this->espnow_ == nullptr) {
      return ESP_FAIL;
    }
    return this->espnow_->send(mac, packet,
                               std::forward<Callback>(callback));
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
#endif

  bool begin_udp(uint16_t port);
  void poll();
  bool send_udp(const std::vector<uint8_t> &packet);

  bool dispatch_packet(const CFXSyncSource &source, const uint8_t *data,
                       size_t size);
  bool dispatch_unknown_packet(const CFXSyncSource &source,
                               const uint8_t *data, size_t size);

 protected:
  static constexpr size_t MAX_GROUPS = 8;

  CFXSyncComponent *groups_[MAX_GROUPS]{};
  size_t group_count_{0};

  CFXSyncUDPTransport udp_;
  uint16_t udp_port_{0};

#ifdef USE_ESPNOW
  espnow::ESPNowComponent *espnow_{nullptr};
  bool espnow_registered_{false};
#endif
};

CFXSyncBus &global_cfx_sync_bus();

}  // namespace cfx_sync
}  // namespace esphome

#endif  // defined(USE_ESP32) || defined(USE_ESP8266)
