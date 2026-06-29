#include "cfx_sync_udp.h"
#include "cfx_sync.h"

#include "esphome/core/log.h"

namespace esphome {
namespace cfx_sync {

static const char *const TAG = "cfx_sync.udp";

bool CFXSyncUDPTransport::begin(uint16_t port) {
  this->port_ = port;
  this->ready_ = false;
  ESP_LOGD(TAG, "UDP transport shell initialized on port %u",
           static_cast<unsigned>(port));
  return false;
}

void CFXSyncUDPTransport::poll(CFXSyncComponent *parent) {
  (void) parent;
}

bool CFXSyncUDPTransport::send_broadcast(const std::vector<uint8_t> &packet) {
  (void) packet;
  return false;
}

}  // namespace cfx_sync
}  // namespace esphome
