#pragma once

#include "esphome/components/light/light_state.h"

#include <array>
#include <cstdint>

namespace esphome {
namespace cfx_dimmer {

struct CFXDimmerTimingHint {
  bool has_transition{false};
  uint16_t transition_ms{0};
  bool has_ramp{false};
  uint16_t ramp_ms{0};
};

struct CFXDimmerTimingEntry {
  light::LightState *light{nullptr};
  uint32_t ramp_end_ms{0};
  bool has_ramp_duration{false};
  uint16_t ramp_ms{0};
};

inline std::array<CFXDimmerTimingEntry, 8> CFX_DIMMER_TIMING_HINTS{};

inline uint16_t clamp_timing_ms_(uint32_t value) {
  return value > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(value);
}

inline CFXDimmerTimingEntry *find_light_timing_entry_(
    light::LightState *light) {
  if (light == nullptr) {
    return nullptr;
  }
  for (auto &entry : CFX_DIMMER_TIMING_HINTS) {
    if (entry.light == light) {
      return &entry;
    }
  }
  return nullptr;
}

inline CFXDimmerTimingEntry *find_or_create_light_timing_entry_(
    light::LightState *light) {
  if (auto *entry = find_light_timing_entry_(light); entry != nullptr) {
    return entry;
  }
  for (auto &entry : CFX_DIMMER_TIMING_HINTS) {
    if (entry.light == nullptr) {
      entry.light = light;
      return &entry;
    }
  }
  return nullptr;
}

inline void publish_light_ramp_hint(light::LightState *light,
                                    uint32_t ramp_end_ms) {
  if (auto *entry = find_or_create_light_timing_entry_(light);
      entry != nullptr) {
    entry->ramp_end_ms = ramp_end_ms;
    entry->has_ramp_duration = false;
    entry->ramp_ms = 0;
  }
}

inline void publish_light_ramp_duration_hint(light::LightState *light,
                                             uint32_t ramp_ms) {
  if (auto *entry = find_or_create_light_timing_entry_(light);
      entry != nullptr) {
    entry->ramp_end_ms = 0;
    entry->has_ramp_duration = true;
    entry->ramp_ms = clamp_timing_ms_(ramp_ms);
  }
}

inline void clear_light_timing_hint(light::LightState *light) {
  if (auto *entry = find_light_timing_entry_(light); entry != nullptr) {
    entry->ramp_end_ms = 0;
    entry->has_ramp_duration = false;
    entry->ramp_ms = 0;
  }
}

inline CFXDimmerTimingHint capture_light_timing_hint(light::LightState *light,
                                                     uint32_t now) {
  CFXDimmerTimingHint hint;
  auto *entry = find_light_timing_entry_(light);
  if (entry == nullptr) {
    return hint;
  }
  if (entry->has_ramp_duration) {
    hint.has_ramp = true;
    hint.ramp_ms = entry->ramp_ms;
    entry->has_ramp_duration = false;
    entry->ramp_ms = 0;
    return hint;
  }
  if (entry->ramp_end_ms == 0) {
    return hint;
  }
  if (static_cast<int32_t>(entry->ramp_end_ms - now) <= 0) {
    entry->ramp_end_ms = 0;
    return hint;
  }
  hint.has_ramp = true;
  hint.ramp_ms = clamp_timing_ms_(entry->ramp_end_ms - now);
  return hint;
}

}  // namespace cfx_dimmer
}  // namespace esphome
