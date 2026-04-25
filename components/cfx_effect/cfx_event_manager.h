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
    // 1. Coalescing: Drop if an identical event is already in the queue.
    for (uint8_t i = this->deferred_read_; i != this->deferred_write_;
         i = (i + 1) % DEFERRED_QUEUE_SIZE) {
      if (this->deferred_queue_[i] == evt) {
        return;
      }
    }

    uint8_t next = (this->deferred_write_ + 1) % DEFERRED_QUEUE_SIZE;
    
    // 2. Priority-aware Dropping (CFX-051)
    // If we're hitting the limit (~80% full), and the new event is a high-priority 
    // lifecycle event, we can safely drop an old non-critical milestone to make room.
    uint8_t count = (this->deferred_write_ + DEFERRED_QUEUE_SIZE - this->deferred_read_) % DEFERRED_QUEUE_SIZE;

    if (next == this->deferred_read_) {
      // Queue is hard-full. Try to reclaim one slot from the oldest milestone.
      bool reclaimed = false;
      const bool is_critical = (evt.find("cfx_reach") == std::string::npos);
      
      if (is_critical) {
        for (uint8_t j = 0; j < DEFERRED_QUEUE_SIZE; j++) {
          uint8_t idx = (this->deferred_read_ + j) % DEFERRED_QUEUE_SIZE;
          if (idx == this->deferred_write_) break;
          
          if (this->deferred_queue_[idx].find("cfx_reach") != std::string::npos) {
            // Drop this milestone to make room
            this->deferred_queue_[idx] = evt;
            this->deferred_write_ = (this->deferred_write_ + 1) % DEFERRED_QUEUE_SIZE;
            this->deferred_read_ = (this->deferred_read_ + 1) % DEFERRED_QUEUE_SIZE;
            reclaimed = true;
            break;
          }
        }
      }
      
      if (!reclaimed) {
        ESP_LOGW("cfx_seq", "deferred queue full, dropping '%s'", evt.c_str());
        return;
      }
      return;
    }
    
    this->deferred_queue_[this->deferred_write_] = evt;
    this->deferred_write_ = next;
  }

  // Drain queued events per ESPHome loop() tick.
  // Adaptive batching: delivers more events per loop when the queue is backed
  // up, reducing the number of loop cycles needed to drain a large burst
  // (e.g. 8 lights × 24 milestones = 192 events from a single effect run).
  //
  // Batch size scales with queue occupancy:
  //   < 25% full  →  1 event  (light load, conservative)
  //   25–50% full →  2 events (moderate backlog)
  //   > 50% full  →  3 events (heavy backlog, drain faster)
  //
  // Three back-to-back trigger() calls are well within the 30ms API heartbeat
  // window — each is a short string delivery costing a few microseconds.
  void flush_pending() {
    if (this->deferred_read_ == this->deferred_write_)
      return;

    // Compute current queue occupancy and pick batch size.
    uint8_t queue_count = (this->deferred_write_ + DEFERRED_QUEUE_SIZE - this->deferred_read_) % DEFERRED_QUEUE_SIZE;
    uint8_t max_this_loop;
    if (queue_count >= DEFERRED_QUEUE_SIZE / 2)
      max_this_loop = 3; // > 50% full — drain aggressively
    else if (queue_count >= DEFERRED_QUEUE_SIZE / 4)
      max_this_loop = 2; // 25–50% full — moderate pace
    else
      max_this_loop = 1; // < 25% full — conservative, API-safe

    uint8_t flushed_this_loop = 0;
    while (this->deferred_read_ != this->deferred_write_ && flushed_this_loop < max_this_loop) {
      const uint8_t idx = this->deferred_read_;
      std::string evt;
      evt.swap(this->deferred_queue_[idx]);
      this->deferred_read_ = (idx + 1) % DEFERRED_QUEUE_SIZE;
      flushed_this_loop++;

      // CFX-028: route to the per-strip entity whose tag appears in the string.
      esphome::event::Event *target_entity = this->event_entity_;
      bool parsed_tag = false;
      bool resolved_tag = false;
      std::string tag;
      size_t first_colon = evt.find(':');
      if (first_colon != std::string::npos) {
        size_t second_colon = evt.find(':', first_colon + 1);
        tag = (second_colon != std::string::npos)
                  ? evt.substr(first_colon + 1, second_colon - first_colon - 1)
                  : evt.substr(first_colon + 1);
        parsed_tag = true;

        // CFX-040: per-tag opt-out
        if (std::find(this->disabled_tags_.begin(), this->disabled_tags_.end(), tag) != this->disabled_tags_.end()) {
          continue;
        }

        auto it = this->strip_entities_.find(tag);
        if (it != this->strip_entities_.end() && it->second != nullptr) {
          target_entity = it->second;
          resolved_tag = true;
        }
      }

      // Unknown tagged events should not fall through to an unrelated default
      // entity; that produces misleading "invalid event type" errors on the
      // wrong strip and hides the real tag mismatch.
      if (parsed_tag && !resolved_tag) {
        ESP_LOGW("cfx_seq", "dropping event with unknown tag '%s': %s",
                 tag.c_str(), evt.c_str());
        continue;
      }

      if (target_entity != nullptr) {
        target_entity->trigger(evt);
      }
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

  static constexpr uint8_t DEFERRED_QUEUE_SIZE = 64;
  std::string deferred_queue_[DEFERRED_QUEUE_SIZE];
  uint8_t deferred_write_{0};
  uint8_t deferred_read_{0};
};

}  // namespace chimera_fx
}  // namespace esphome
