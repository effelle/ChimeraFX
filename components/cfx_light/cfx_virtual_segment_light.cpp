/*
 * Copyright (c) 2026 Federico Leoni (effelle)
 *
 * CFXVirtualSegmentLight implementation
 */

#ifdef USE_ESP32

#include "cfx_virtual_segment_light.h"
#include "esphome/core/log.h"

namespace esphome {
namespace cfx_light {

static const char *const TAG = "cfx_vseg";

void CFXVirtualSegmentLight::dump_config() {
  ESP_LOGCONFIG(TAG, "  Virtual Segment '%s': LEDs %u-%u (%d pixels)",
                seg_id_.c_str(), start_, stop_, this->size());
}

} // namespace cfx_light
} // namespace esphome

#endif // USE_ESP32
