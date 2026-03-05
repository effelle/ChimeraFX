#pragma once
#include "esphome/core/automation.h"

namespace esphome {
namespace chimera_fx {

class CfxOnStartTrigger : public Trigger<> {};

class CfxOnCompleteTrigger : public Trigger<> {};

class CfxOnReachTrigger : public Trigger<float> {
 public:
  explicit CfxOnReachTrigger(float position) : target_position_(position) {}

  float get_target_position() const { return target_position_; }

 protected:
  float target_position_;
};

class CfxOnPixelNumTrigger : public Trigger<int32_t> {
 public:
  explicit CfxOnPixelNumTrigger(int32_t pixel) : target_pixel_(pixel) {}

  int32_t get_target_pixel() const { return target_pixel_; }

 protected:
  int32_t target_pixel_;
};

}  // namespace chimera_fx
}  // namespace esphome
