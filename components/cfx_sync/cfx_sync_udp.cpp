#include "cfx_sync_udp.h"
#include "cfx_sync_bus.h"

#include "esphome/core/log.h"

#if defined(USE_ESP8266)
#include <ESP8266WiFi.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <lwip/inet.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>
#include <unistd.h>
#endif

namespace esphome {
namespace cfx_sync {

static const char *const TAG = "cfx_sync.udp";
static constexpr size_t UDP_RX_BUFFER_SIZE =
    CFX_SYNC_SHARED_TRANSPORT_MTU + 1;

CFXSyncUDPTransport::~CFXSyncUDPTransport() { this->close_(); }

void CFXSyncUDPTransport::close_() {
#if defined(USE_ESP8266)
  this->udp_.stop();
#else
  if (this->socket_fd_ >= 0) {
    ::close(this->socket_fd_);
    this->socket_fd_ = -1;
  }
#endif
  this->ready_ = false;
}

bool CFXSyncUDPTransport::begin(uint16_t port) {
  this->close_();
  this->port_ = port;
#if defined(USE_ESP8266)
  if (!this->udp_.begin(port)) {
    ESP_LOGW(TAG, "Failed to bind UDP sync socket on port %u",
             static_cast<unsigned>(port));
    return false;
  }
#else
  this->socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (this->socket_fd_ < 0) {
    ESP_LOGW(TAG, "Failed to create UDP socket: errno=%d", errno);
    return false;
  }

  int enabled = 1;
  if (::setsockopt(this->socket_fd_, SOL_SOCKET, SO_REUSEADDR, &enabled,
                   sizeof(enabled)) < 0) {
    ESP_LOGD(TAG, "UDP SO_REUSEADDR setup failed: errno=%d", errno);
  }
  if (::setsockopt(this->socket_fd_, SOL_SOCKET, SO_BROADCAST, &enabled,
                   sizeof(enabled)) < 0) {
    ESP_LOGW(TAG, "UDP SO_BROADCAST setup failed: errno=%d", errno);
    this->close_();
    return false;
  }

  const int flags = ::fcntl(this->socket_fd_, F_GETFL, 0);
  if (flags >= 0) {
    ::fcntl(this->socket_fd_, F_SETFL, flags | O_NONBLOCK);
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = INADDR_ANY;
  if (::bind(this->socket_fd_, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0) {
    ESP_LOGW(TAG, "Failed to bind UDP sync socket on port %u: errno=%d",
             static_cast<unsigned>(port), errno);
    this->close_();
    return false;
  }
#endif

  this->ready_ = true;
  ESP_LOGI(TAG, "UDP transport listening on port %u",
           static_cast<unsigned>(port));
  return true;
}

void CFXSyncUDPTransport::poll(CFXSyncBus *bus) {
  if (!this->ready_ || bus == nullptr) {
    return;
  }

  uint8_t buffer[UDP_RX_BUFFER_SIZE];
#if defined(USE_ESP8266)
  while (true) {
    const int packet_size = this->udp_.parsePacket();
    if (packet_size <= 0) {
      return;
    }
    if (packet_size > static_cast<int>(CFX_SYNC_SHARED_TRANSPORT_MTU)) {
      ESP_LOGV(TAG, "Dropped oversized UDP datagram: bytes=%d maximum=%u",
               packet_size,
               static_cast<unsigned>(CFX_SYNC_SHARED_TRANSPORT_MTU));
      this->udp_.flush();
      continue;
    }
    const int received = this->udp_.read(buffer, sizeof(buffer));
    if (received <= 0) {
      continue;
    }
    const IPAddress remote_ip = this->udp_.remoteIP();
    CFXSyncSource source =
        CFXSyncSource::from_udp(static_cast<uint32_t>(remote_ip),
                                this->udp_.remotePort());
    bus->dispatch_packet(source, buffer, static_cast<size_t>(received));
  }
#else
  while (true) {
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);
    const ssize_t received =
        ::recvfrom(this->socket_fd_, buffer, sizeof(buffer), 0,
                   reinterpret_cast<sockaddr *>(&addr), &addr_len);
    if (received < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGV(TAG, "UDP receive failed: errno=%d", errno);
      }
      return;
    }
    if (received == 0) {
      return;
    }
    if (received >
        static_cast<ssize_t>(CFX_SYNC_SHARED_TRANSPORT_MTU)) {
      ESP_LOGV(TAG, "Dropped oversized UDP datagram: bytes>=%u maximum=%u",
               static_cast<unsigned>(received),
               static_cast<unsigned>(CFX_SYNC_SHARED_TRANSPORT_MTU));
      continue;
    }

    CFXSyncSource source =
        CFXSyncSource::from_udp(addr.sin_addr.s_addr, ntohs(addr.sin_port));
    bus->dispatch_packet(source, buffer, static_cast<size_t>(received));
  }
#endif
}

bool CFXSyncUDPTransport::send_to_(uint32_t address, uint16_t port,
                                   const uint8_t *data, size_t size) {
#if defined(USE_ESP8266)
  if (!this->ready_ || data == nullptr || size == 0) {
    return false;
  }

  IPAddress destination(address);
  if (!this->udp_.beginPacket(destination, port)) {
    return false;
  }
  const size_t written = this->udp_.write(data, size);
  return written == size && this->udp_.endPacket() == 1;
#else
  if (!this->ready_ || this->socket_fd_ < 0 || data == nullptr || size == 0) {
    return false;
  }

  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(port);
  destination.sin_addr.s_addr = address;

  const ssize_t sent =
      ::sendto(this->socket_fd_, data, size, 0,
               reinterpret_cast<const sockaddr *>(&destination),
               sizeof(destination));
  return sent == static_cast<ssize_t>(size);
#endif
}

bool CFXSyncUDPTransport::send_unicast(uint32_t address, uint16_t port,
                                       const uint8_t *data, size_t size) {
  if (address == 0 || port == 0) {
    return false;
  }
  return this->send_to_(address, port, data, size);
}

bool CFXSyncUDPTransport::send_unicast(
    uint32_t address, uint16_t port, const std::vector<uint8_t> &packet) {
  return this->send_unicast(address, port, packet.data(), packet.size());
}

bool CFXSyncUDPTransport::send_broadcast(const uint8_t *data, size_t size) {
#if defined(USE_ESP8266)
  bool sent = false;
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress local_ip = WiFi.localIP();
    const IPAddress subnet = WiFi.subnetMask();
    const uint32_t broadcast =
        static_cast<uint32_t>(local_ip) | ~static_cast<uint32_t>(subnet);
    if (broadcast != 0 && broadcast != 0xFFFFFFFFUL) {
      sent = this->send_to_(broadcast, this->port_, data, size);
    }
  }
  const IPAddress broadcast(255, 255, 255, 255);
  if (!this->ready_ || data == nullptr || size == 0) {
    return sent;
  }
  if (!this->udp_.beginPacket(broadcast, this->port_)) {
    return sent;
  }
  const size_t written = this->udp_.write(data, size);
  return (written == size && this->udp_.endPacket() == 1) || sent;
#else
  bool sent = false;

  for (netif *interface = netif_list; interface != nullptr;
       interface = interface->next) {
    if (!netif_is_up(interface)) {
      continue;
    }
    const ip4_addr_t *ip = netif_ip4_addr(interface);
    const ip4_addr_t *netmask = netif_ip4_netmask(interface);
    if (ip == nullptr || netmask == nullptr || ip4_addr_isany_val(*ip)) {
      continue;
    }
    const uint32_t subnet_broadcast = ip->addr | ~netmask->addr;
    if (subnet_broadcast == 0 || subnet_broadcast == INADDR_BROADCAST) {
      continue;
    }
    sent = this->send_to_(subnet_broadcast, this->port_, data, size) || sent;
  }

  return this->send_to_(INADDR_BROADCAST, this->port_, data, size) || sent;
#endif
}

bool CFXSyncUDPTransport::send_broadcast(
    const std::vector<uint8_t> &packet) {
  return this->send_broadcast(packet.data(), packet.size());
}

}  // namespace cfx_sync
}  // namespace esphome
