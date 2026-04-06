#include "cfx_event_manager.h"

namespace esphome {
namespace chimera_fx {

CFXEventManager &CFXEventManager::get() {
  static CFXEventManager instance;
  return instance;
}

void CFXEventManager::fire_event(const char *type) {
  // If HA events are disabled (cfx_control ID 6 excluded), push nothing.
  if (!this->ha_events_enabled_) return;

  // Push onto the deferred queue — zero blocking on the render loop hot path.
  // flush_pending() drains one entry per loop() call at ~50Hz. (CFX-025)
  this->push_deferred(std::string(type));
}

void CFXEventManager::flush_pending() {
  // Drain exactly ONE event per loop() call. Combined with cfx_reach
  // coalescing in push_deferred(), the queue holds at most ~6 entries
  // (one per strip) so single-drain keeps pace at 50Hz loop rate while
  // preventing the API TCP send buffer from overflowing. (CFX-027)
  if (this->deferred_read_ == this->deferred_write_)
    return;

  const std::string evt = this->deferred_queue_[this->deferred_read_];
  this->deferred_read_ = (this->deferred_read_ + 1) % DEFERRED_QUEUE_SIZE;

  // CFX-028: route to the per-strip entity whose tag appears in the event
  // string.  All CFX event strings have the form "<verb>:<tag>[:<extra>]",
  // so the tag sits between the first and second colon.
  esphome::event::Event *target_entity = this->event_entity_; // fallback
  size_t first_colon = evt.find(':');
  if (first_colon != std::string::npos) {
    size_t second_colon = evt.find(':', first_colon + 1);
    std::string tag = (second_colon != std::string::npos)
                          ? evt.substr(first_colon + 1, second_colon - first_colon - 1)
                          : evt.substr(first_colon + 1);

    // CFX-040: Per-tag opt-out check
    if (std::find(this->disabled_tags_.begin(), this->disabled_tags_.end(), tag) != this->disabled_tags_.end()) {
      return; 
    }

    auto it = this->strip_entities_.find(tag);
    if (it != this->strip_entities_.end() && it->second != nullptr) {
      target_entity = it->second;
    }
  }

  if (target_entity != nullptr) {
    target_entity->trigger(evt);
  }
}

}  // namespace chimera_fx
}  // namespace esphome
