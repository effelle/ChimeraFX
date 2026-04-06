#pragma once

#include "esphome/components/event/event.h"
#include "esphome/core/log.h"
#include <map>
#include <string>
#include <vector>
#include <algorithm>

namespace esphome {
namespace chimera_fx {

class CFXEventManager {
public:
  static CFXEventManager &get();
  // CFX-028: register a dedicated HA event entity for one strip tag.
  // Called from generated code once per strip at setup time.
  void register_strip_entity(const std::string &tag, esphome::event::Event *e) {
    this->strip_entities_[tag] = e;
    // Keep event_entity_ pointing at the first registered strip so any
    // legacy call sites that still use it get a valid (non-null) pointer.
    if (this->event_entity_ == nullptr)
      this->event_entity_ = e;
  }
  // Legacy single-entity setter — kept for backward compat; maps to the
  // "no tag" fallback slot used when a strip tag cannot be resolved.
  void set_event_entity(esphome::event::Event *e) { this->event_entity_ = e; }

  // HA event delivery opt-in. When false, fire_event() is a no-op for all
  // HA-facing paths. Internal on_cfx_reach triggers are unaffected. (CFX-026)
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
  void fire_event(const char *type);
  void flush_pending();

  // Push one event string onto the deferred queue (called from render loop).
  // Zero blocking — just a ring buffer write. (CFX-025)
  // Coalescing: identical events (full string match) replace any pending entry
  // instead of appending, preventing API buffer overflow with 4+ strips.
  // CFX-034: Changed from prefix match to full-string match so that different
  // milestones (e.g. 95% and 100%) firing in the same check_milestones_()
  // while-loop are never coalesced — only truly duplicate events are merged.
  void push_deferred(const std::string &evt) {
    // Full-string coalescing: scan pending entries for an exact duplicate.
    // If found, no need to push again (idempotent). This protects against
    // burst spam (same milestone re-firing across rapid frames) while never
    // losing distinct milestone events (95% vs 100%). (CFX-034)
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

protected:
  CFXEventManager() = default;
  esphome::event::Event *event_entity_{nullptr};  // fallback / first-strip alias
  std::map<std::string, esphome::event::Event *> strip_entities_;  // CFX-028
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
