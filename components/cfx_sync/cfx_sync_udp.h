/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * UDP transport shell for CFX Sync packets.
 */

#pragma once

#include "cfx_sync_transport.h"

#if defined(USE_ESP8266)
#include <WiFiUdp.h>
#endif

#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome {
namespace cfx_sync {

class CFXSyncBus;

class CFXSyncUDPTransport {
 public:
  ~CFXSyncUDPTransport();

  bool begin(uint16_t port);
  void poll(CFXSyncBus *bus);
  bool send_broadcast(const uint8_t *data, size_t size);
  bool send_broadcast(const std::vector<uint8_t> &packet);
  bool send_unicast(uint32_t address, uint16_t port, const uint8_t *data,
                    size_t size);
  bool send_unicast(uint32_t address, uint16_t port,
                    const std::vector<uint8_t> &packet);
  bool is_ready() const { return this->ready_; }

 protected:
  void close_();
  bool send_to_(uint32_t address, uint16_t port, const uint8_t *data,
                size_t size);

#if defined(USE_ESP8266)
  WiFiUDP udp_;
#else
  int socket_fd_{-1};
#endif
  bool ready_{false};
  uint16_t port_{0};
};

}  // namespace cfx_sync
}  // namespace esphome
