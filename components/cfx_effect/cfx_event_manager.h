#pragma once

#include "esphome/components/event/event.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <map>
#include <string>
#include <vector>
#include <algorithm>

namespace esphome {
namespace chimera_fx {

class CFXEventManager {
public:
  static CFXEventManager &get() {
    static CFXEventManager instance;
    return instance;
  }

  // CFX-028: register a dedicated HA event entity for one strip tag.
  void register_strip_entity(const std::string &tag, esphome::event::Event *e) {
    this->strip_entities_[tag] = e;
    if (this->event_entity_ == nullptr)
      this->event_entity_ = e;
  }

  void set_event_entity(esphome::event::Event *e) { this->event_entity_ = e; }
  void set_ha_events_enabled(bool enabled) { this->ha_events_enabled_ = enabled; }

  // Per-tag event delivery opt-out (CFX-040)
  void set_ha_events_disabled_for_tag(const std::string &tag) {
    if (std::find(this->disabled_tags_.begin(), this->disabled_tags_.end(), tag) == this->disabled_tags_.end()) {
      this->disabled_tags_.push_back(tag);
    }
  }

  void add_known_tag(const std::string &tag) {
    for (const auto &t : this->known_tags_)
      if (t == tag) return;
    this->known_tags_.push_back(tag);
  }

  void fire_event(const char *type) {
    if (!this->ha_events_enabled_) return;
    this->push_deferred(std::string(type));
  }

  // Push one event string onto the deferred ring-buffer queue.
  // Full-string coalescing: exact duplicates are dropped. (CFX-034)
  void push_deferred(const std::string &evt) {
    for (uint8_t i = this->deferred_read_; i != this->deferred_write_;
         i = (i + 1) % DEFERRED_QUEUE_SIZE) {
      if (this->deferred_queue_[i] == evt) {
        return;  // exact duplicate already queued — skip
      }
    }
    uint8_t next = (this->deferred_write_ + 1) % DEFERRED_QUEUE_SIZE;
    if (next == this->deferred_read_) {
      ESP_LOGW("cfx_seq", "deferred queue full, dropping '%s'", evt.c_str());
      return;
    }
    this->deferred_queue_[this->deferred_write_] = evt;
    this->deferred_write_ = next;
  }

  // Drain exactly one queued event per ESPHome loop() tick. (CFX-025)
  void flush_pending() {
    static uint32_t last_flush_ms = 0;
    uint32_t now = esphome::millis();
    if (now == last_flush_ms)
      return;

    if (this->deferred_read_ == this->deferred_write_)
      return;

    last_flush_ms = now;

    const std::string evt = this->deferred_queue_[this->deferred_read_];
    this->deferred_read_ = (this->deferred_read_ + 1) % DEFERRED_QUEUE_SIZE;

    // CFX-028: route to the per-strip entity whose tag appears in the string.
    esphome::event::Event *target_entity = this->event_entity_;
    size_t first_colon = evt.find(':');
    if (first_colon != std::string::npos) {
      size_t second_colon = evt.find(':', first_colon + 1);
      std::string tag = (second_colon != std::string::npos)
                            ? evt.substr(first_colon + 1, second_colon - first_colon - 1)
                            : evt.substr(first_colon + 1);

      // CFX-040: per-tag opt-out
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

protected:
  CFXEventManager() = default;
  esphome::event::Event *event_entity_{nullptr};
  std::map<std::string, esphome::event::Event *> strip_entities_;
  bool ha_events_enabled_{true};
  bool discovery_done_{false};
  std::vector<std::string> known_tags_;
  std::vector<std::string> disabled_tags_;

  static constexpr uint8_t DEFERRED_QUEUE_SIZE = 32;
  std::string deferred_queue_[DEFERRED_QUEUE_SIZE];
  uint8_t deferred_write_{0};
  uint8_t deferred_read_{0};
};

}  // namespace chimera_fx
}  // namespace esphome
