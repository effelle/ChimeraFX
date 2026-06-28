/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Transport-neutral source identity for CFX Sync packets.
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace esphome {
namespace cfx_sync {

enum class CFXSyncTransportKind : uint8_t { ESPNOW = 0, UDP = 1 };

struct CFXSyncSource {
  CFXSyncTransportKind transport{CFXSyncTransportKind::ESPNOW};
  std::array<uint8_t, 6> mac{{0, 0, 0, 0, 0, 0}};
  uint32_t ipv4{0};
  uint16_t port{0};
  bool identity_valid{false};

  static CFXSyncSource from_espnow(const uint8_t *mac_addr) {
    CFXSyncSource source;
    source.transport = CFXSyncTransportKind::ESPNOW;
    if (mac_addr != nullptr) {
      for (std::size_t i = 0; i < source.mac.size(); i++) {
        source.mac[i] = mac_addr[i];
      }
      source.identity_valid = true;
    }
    return source;
  }

  static CFXSyncSource from_udp(uint32_t ipv4_addr, uint16_t udp_port) {
    CFXSyncSource source;
    source.transport = CFXSyncTransportKind::UDP;
    source.ipv4 = ipv4_addr;
    source.port = udp_port;
    source.identity_valid = true;
    return source;
  }

  const uint8_t *espnow_mac_or_null() const {
    if (this->transport != CFXSyncTransportKind::ESPNOW ||
        !this->identity_valid) {
      return nullptr;
    }
    return this->mac.data();
  }
};

}  // namespace cfx_sync
}  // namespace esphome
