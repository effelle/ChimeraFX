#pragma once

#include <cstdint>

namespace esphome {
namespace cfx_cct_sweeper {

enum class CCTEndpoint : uint8_t {
  NON_CCT,
  NATIVE,
  PREFERRED,
};

enum class CCTShortPressAction : uint8_t {
  RESTORE_RETAINED,
  APPLY_NATIVE,
  APPLY_PREFERRED,
};

constexpr CCTShortPressAction select_cct_short_press_action(
    bool any_on, bool has_retained_state, CCTEndpoint current_endpoint,
    CCTEndpoint last_endpoint) {
  if (!any_on) {
    return has_retained_state ? CCTShortPressAction::RESTORE_RETAINED
                              : CCTShortPressAction::APPLY_PREFERRED;
  }
  if (current_endpoint == CCTEndpoint::NATIVE) {
    return CCTShortPressAction::APPLY_PREFERRED;
  }
  if (current_endpoint == CCTEndpoint::PREFERRED) {
    return CCTShortPressAction::APPLY_NATIVE;
  }
  return last_endpoint == CCTEndpoint::NATIVE
             ? CCTShortPressAction::APPLY_NATIVE
             : CCTShortPressAction::APPLY_PREFERRED;
}

constexpr bool can_start_cct_sweep(bool began_on) { return began_on; }

constexpr bool select_sweep_direction_warmer(
    bool alternating_direction_warmer, bool first_sweep_after_boot) {
  return first_sweep_after_boot || alternating_direction_warmer;
}

}  // namespace cfx_cct_sweeper
}  // namespace esphome
