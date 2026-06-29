/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * UDP transport shell for CFX Sync packets.
 */

#pragma once

#include "cfx_sync_transport.h"

#include <cstdint>
#include <vector>

namespace esphome {
namespace cfx_sync {

class CFXSyncComponent;

class CFXSyncUDPTransport {
 public:
  bool begin(uint16_t port);
  void poll(CFXSyncComponent *parent);
  bool send_broadcast(const std::vector<uint8_t> &packet);
  bool is_ready() const { return this->ready_; }

 protected:
  bool ready_{false};
  uint16_t port_{0};
};

}  // namespace cfx_sync
}  // namespace esphome
