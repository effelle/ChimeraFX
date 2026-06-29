#include "cfx_sync_udp.h"
#include "cfx_sync.h"

#include "esphome/core/log.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <unistd.h>

namespace esphome {
namespace cfx_sync {

static const char *const TAG = "cfx_sync.udp";
static constexpr size_t UDP_RX_BUFFER_SIZE = 250;

CFXSyncUDPTransport::~CFXSyncUDPTransport() { this->close_(); }

void CFXSyncUDPTransport::close_() {
  if (this->socket_fd_ >= 0) {
    ::close(this->socket_fd_);
    this->socket_fd_ = -1;
  }
  this->ready_ = false;
}

bool CFXSyncUDPTransport::begin(uint16_t port) {
  this->close_();
  this->port_ = port;
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

  this->ready_ = true;
  ESP_LOGI(TAG, "UDP transport listening on port %u",
           static_cast<unsigned>(port));
  return true;
}

void CFXSyncUDPTransport::poll(CFXSyncComponent *parent) {
  if (!this->ready_ || parent == nullptr) {
    return;
  }

  uint8_t buffer[UDP_RX_BUFFER_SIZE];
  while (true) {
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);
    const ssize_t received =
        ::recvfrom(this->socket_fd_, buffer, sizeof(buffer), 0,
                   reinterpret_cast<sockaddr *>(&addr), &addr_len);
    if (received < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGD(TAG, "UDP receive failed: errno=%d", errno);
      }
      return;
    }
    if (received == 0) {
      return;
    }

    CFXSyncSource source =
        CFXSyncSource::from_udp(addr.sin_addr.s_addr, ntohs(addr.sin_port));
    parent->handle_packet_(source, buffer, static_cast<size_t>(received));
  }
}

bool CFXSyncUDPTransport::send_broadcast(const std::vector<uint8_t> &packet) {
  if (!this->ready_ || this->socket_fd_ < 0 || packet.empty()) {
    return false;
  }

  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(this->port_);
  destination.sin_addr.s_addr = INADDR_BROADCAST;

  const ssize_t sent =
      ::sendto(this->socket_fd_, packet.data(), packet.size(), 0,
               reinterpret_cast<const sockaddr *>(&destination),
               sizeof(destination));
  return sent == static_cast<ssize_t>(packet.size());
}

}  // namespace cfx_sync
}  // namespace esphome
