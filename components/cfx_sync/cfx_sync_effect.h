/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * Portable effect identity for CFX sync.
 */

#pragma once

#ifdef USE_ESP32

#include "esphome/components/light/light_state.h"

#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace cfx_sync {

enum class CFXSyncEffectKind : uint8_t {
  NONE = 0,
  CHIMERAFX = 1,
  UNSUPPORTED = 2,
};

struct CFXSyncEffectState {
  CFXSyncEffectKind kind{CFXSyncEffectKind::NONE};
  uint8_t effect_id{0};
  std::string name;

  bool operator==(const CFXSyncEffectState &other) const {
    return this->kind == other.kind &&
           this->effect_id == other.effect_id &&
           this->name == other.name;
  }

  bool operator!=(const CFXSyncEffectState &other) const {
    return !(*this == other);
  }
};

struct CFXSyncEffectEntry {
  uint8_t effect_id;
  std::string name;
};

inline const CFXSyncEffectEntry *find_effect_entry(
    const std::vector<CFXSyncEffectEntry> &catalog, uint8_t effect_id,
    const std::string &name) {
  for (const auto &entry : catalog) {
    if (entry.effect_id == effect_id && entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

inline CFXSyncEffectState capture_effect_state(
    light::LightState *state,
    const std::vector<CFXSyncEffectEntry> &catalog) {
  if (state == nullptr) {
    return {};
  }

  const std::string effect_name = state->get_effect_name();
  if (effect_name == "None") {
    return {};
  }

  for (const auto &entry : catalog) {
    if (entry.name == effect_name) {
      return {
          CFXSyncEffectKind::CHIMERAFX,
          entry.effect_id,
          entry.name,
      };
    }
  }

  return {
      CFXSyncEffectKind::UNSUPPORTED,
      0,
      effect_name,
  };
}

}  // namespace cfx_sync
}  // namespace esphome

#endif  // USE_ESP32
