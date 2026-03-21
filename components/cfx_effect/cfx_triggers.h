#pragma once
#include "esphome/core/automation.h"

namespace esphome {
namespace chimera_fx {

class CfxOnStartTrigger : public Trigger<> {};

class CfxOnStopTrigger    : public Trigger<> {};
class CfxOnCompleteTrigger : public Trigger<> {};

class CfxOnReachTrigger : public Trigger<float> {
 public:
  explicit CfxOnReachTrigger(float position) : target_position_(position) {}

  float get_target_position() const { return target_position_; }

 protected:
  float target_position_;
};


}  // namespace chimera_fx
}  // namespace esphome
