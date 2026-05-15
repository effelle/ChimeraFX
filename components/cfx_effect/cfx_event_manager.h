#pragma once

#include "esphome/components/event/event.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

namespace esphome {
namespace chimera_fx {

class CFXEventManager {
public:
  enum DeferredKind : uint8_t {
    DEFERRED_KIND_TEXT = 0,
    DEFERRED_KIND_REACH = 1,
  };

  static CFXEventManager &get() {
    static CFXEventManager instance;
    return instance;
  }

  // CFX-028: register a dedicated HA event entity for one strip tag.
  void register_strip_entity(const std::string &tag, esphome::event::Event *e) {
    this->strip_entities_[tag] = e;
    auto id_it = this->strip_tag_ids_.find(tag);
    if (id_it == this->strip_tag_ids_.end()) {
      uint16_t tag_id = static_cast<uint16_t>(this->strip_tags_.size());
      this->strip_tag_ids_[tag] = tag_id;
      this->strip_tags_.push_back(tag);
      this->strip_entities_by_id_.push_back(e);
    } else if (id_it->second < this->strip_entities_by_id_.size()) {
      this->strip_entities_by_id_[id_it->second] = e;
    }
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

  void add_known_tag(const std::string &tag) { (void) tag; }

  void fire_event(const char *type) {
    if (!this->ha_events_enabled_ || type == nullptr)
      return;
    std::string evt(type);
    esphome::event::Event *target_entity = this->event_entity_;
    if (!this->resolve_target_entity_(evt, &target_entity))
      return;
    this->push_deferred_text_(evt, target_entity);
  }

  void fire_reach_event(const std::string &tag, uint8_t milestone) {
    if (!this->ha_events_enabled_ || tag.empty())
      return;

    esphome::event::Event *target_entity = this->event_entity_;
    uint16_t tag_id = 0;
    if (!this->resolve_reach_target_(tag, &target_entity, &tag_id))
      return;
    this->push_deferred_reach_(tag_id, milestone, target_entity);
  }

  // Push one event string onto the deferred ring-buffer queue.
  // Full-string coalescing: exact duplicates are dropped. (CFX-034)
  void push_deferred_text_(const std::string &evt,
                           esphome::event::Event *target_entity) {
    // 1. Coalescing: Drop if an identical event is already in the queue.
    for (uint8_t i = this->deferred_read_; i != this->deferred_write_;
         i = (i + 1) % DEFERRED_QUEUE_SIZE) {
      if (this->deferred_kind_[i] == DEFERRED_KIND_TEXT &&
          this->deferred_queue_[i] == evt) {
        return;
      }
    }

    uint8_t next = (this->deferred_write_ + 1) % DEFERRED_QUEUE_SIZE;

    // 2. Priority-aware Dropping (CFX-051)
    // If we're hitting the limit (~80% full), and the new event is a high-priority 
    // lifecycle event, we can safely drop an old non-critical milestone to make room.
    if (next == this->deferred_read_) {
      // Queue is hard-full. Preserve exact cfx_reach semantics by reclaiming
      // only from other deferred text entries.
      bool reclaimed = false;
      for (uint8_t j = 0; j < DEFERRED_QUEUE_SIZE; j++) {
        uint8_t idx = (this->deferred_read_ + j) % DEFERRED_QUEUE_SIZE;
        if (idx == this->deferred_write_)
          break;
        if (this->deferred_kind_[idx] == DEFERRED_KIND_TEXT) {
          this->deferred_queue_[idx] = evt;
          this->deferred_targets_[idx] = target_entity;
          reclaimed = true;
          break;
        }
      }
      if (!reclaimed) {
        ESP_LOGW("cfx_seq", "deferred queue full, dropping '%s'", evt.c_str());
        return;
      }
      return;
    }

    this->deferred_kind_[this->deferred_write_] = DEFERRED_KIND_TEXT;
    this->deferred_queue_[this->deferred_write_] = evt;
    this->deferred_targets_[this->deferred_write_] = target_entity;
    this->deferred_write_ = next;
  }

  void push_deferred_reach_(uint16_t tag_id, uint8_t milestone,
                            esphome::event::Event *target_entity) {
    for (uint8_t i = this->deferred_read_; i != this->deferred_write_;
         i = (i + 1) % DEFERRED_QUEUE_SIZE) {
      if (this->deferred_kind_[i] == DEFERRED_KIND_REACH &&
          this->deferred_reach_tag_ids_[i] == tag_id &&
          this->deferred_reach_milestones_[i] == milestone) {
        return;
      }
    }

    uint8_t next = (this->deferred_write_ + 1) % DEFERRED_QUEUE_SIZE;
    if (next == this->deferred_read_) {
      if (tag_id < this->strip_tags_.size()) {
        ESP_LOGW("cfx_seq", "reach queue full, dropping cfx_reach:%s:%u",
                 this->strip_tags_[tag_id].c_str(), (unsigned) milestone);
      } else {
        ESP_LOGW("cfx_seq", "reach queue full, dropping milestone %u",
                 (unsigned) milestone);
      }
      return;
    }

    this->deferred_kind_[this->deferred_write_] = DEFERRED_KIND_REACH;
    this->deferred_queue_[this->deferred_write_].clear();
    this->deferred_targets_[this->deferred_write_] = target_entity;
    this->deferred_reach_tag_ids_[this->deferred_write_] = tag_id;
    this->deferred_reach_milestones_[this->deferred_write_] = milestone;
    this->deferred_write_ = next;
  }

  // Drain queued events conservatively. This can be called by both sequence and
  // light loops, so the cadence gate prevents fast reach-producing effects from
  // flushing many HA events inside one render window.
  void flush_pending() {
    if (this->deferred_read_ == this->deferred_write_)
      return;

    // Keep event delivery outside the LED frame's hot path.
    const uint32_t now = esphome::millis();
    if (this->last_flush_ms_ != 0 &&
        (now - this->last_flush_ms_) < EVENT_FLUSH_MIN_INTERVAL_MS) {
      return;
    }
    this->last_flush_ms_ = now;
    uint8_t flushed_this_loop = 0;
    while (this->deferred_read_ != this->deferred_write_ &&
           flushed_this_loop < MAX_EVENTS_PER_FLUSH) {
      const uint8_t idx = this->deferred_read_;
      const DeferredKind kind = this->deferred_kind_[idx];
      std::string evt;
      if (kind == DEFERRED_KIND_TEXT) {
        evt.swap(this->deferred_queue_[idx]);
      }
      esphome::event::Event *target_entity = this->deferred_targets_[idx];
      this->deferred_targets_[idx] = nullptr;
      this->deferred_read_ = (idx + 1) % DEFERRED_QUEUE_SIZE;
      flushed_this_loop++;

      if (target_entity != nullptr) {
        if (kind == DEFERRED_KIND_REACH) {
          const uint16_t tag_id = this->deferred_reach_tag_ids_[idx];
          const uint8_t milestone = this->deferred_reach_milestones_[idx];
          if (tag_id < this->strip_tags_.size()) {
            char reach_evt[48];
            std::snprintf(reach_evt, sizeof(reach_evt), "cfx_reach:%s:%u",
                          this->strip_tags_[tag_id].c_str(),
                          (unsigned) milestone);
            target_entity->trigger(reach_evt);
          }
        } else {
          target_entity->trigger(evt);
        }
      }
    }
  }

protected:
  bool resolve_reach_target_(const std::string &tag,
                             esphome::event::Event **target_entity,
                             uint16_t *tag_id) {
    if (!this->resolve_target_for_tag_(tag, target_entity)) {
      return false;
    }

    auto id_it = this->strip_tag_ids_.find(tag);
    if (id_it == this->strip_tag_ids_.end()) {
      ESP_LOGW("cfx_seq", "dropping reach event with unknown tag '%s'",
               tag.c_str());
      return false;
    }

    if (tag_id != nullptr) {
      *tag_id = id_it->second;
    }
    return true;
  }

  bool resolve_target_for_tag_(const std::string &tag,
                               esphome::event::Event **target_entity) {
    if (std::find(this->disabled_tags_.begin(), this->disabled_tags_.end(), tag) !=
        this->disabled_tags_.end()) {
      return false;
    }

    auto it = this->strip_entities_.find(tag);
    if (it != this->strip_entities_.end() && it->second != nullptr) {
      *target_entity = it->second;
      return true;
    }

    ESP_LOGW("cfx_seq", "dropping event with unknown tag '%s'", tag.c_str());
    return false;
  }

  bool resolve_target_entity_(const std::string &evt,
                              esphome::event::Event **target_entity) {
    size_t first_colon = evt.find(':');
    if (first_colon == std::string::npos) {
      return true;
    }

    size_t second_colon = evt.find(':', first_colon + 1);
    std::string tag = (second_colon != std::string::npos)
                          ? evt.substr(first_colon + 1,
                                       second_colon - first_colon - 1)
                          : evt.substr(first_colon + 1);
    return this->resolve_target_for_tag_(tag, target_entity);
  }

  CFXEventManager() = default;
  esphome::event::Event *event_entity_{nullptr};
  std::map<std::string, esphome::event::Event *> strip_entities_;
  std::map<std::string, uint16_t> strip_tag_ids_;
  std::vector<std::string> strip_tags_;
  std::vector<esphome::event::Event *> strip_entities_by_id_;
  bool ha_events_enabled_{true};
  std::vector<std::string> disabled_tags_;

  static constexpr uint8_t DEFERRED_QUEUE_SIZE = 128;
  static constexpr uint8_t MAX_EVENTS_PER_FLUSH = 1;
  static constexpr uint32_t EVENT_FLUSH_MIN_INTERVAL_MS = 12;
  uint32_t last_flush_ms_{0};
  DeferredKind deferred_kind_[DEFERRED_QUEUE_SIZE]{};
  std::string deferred_queue_[DEFERRED_QUEUE_SIZE];
  esphome::event::Event *deferred_targets_[DEFERRED_QUEUE_SIZE]{};
  uint16_t deferred_reach_tag_ids_[DEFERRED_QUEUE_SIZE]{};
  uint8_t deferred_reach_milestones_[DEFERRED_QUEUE_SIZE]{};
  uint8_t deferred_write_{0};
  uint8_t deferred_read_{0};
};

}  // namespace chimera_fx
}  // namespace esphome
