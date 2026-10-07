#pragma once
#include <stdint.h>

// Positive reduction slows the left wheel; negative slows the right wheel.
// Counts are unsigned increments from this motion's baseline (including wrap).
struct StraightBalance {
  uint32_t at = 0;
  int8_t reduction = 0;
  bool enabled = false;
  void reset(uint32_t now) { at = now; reduction = 0; enabled = false; }
  bool update(uint32_t now, uint32_t left, uint32_t right, uint8_t pwm) {
    if (uint32_t(now - at) < 200) return false;
    at = now;
    int32_t error = int32_t(left - right);
    if (error > 9) error = 9;
    if (error < -9) error = -9;
    int8_t limit = pwm / 5;
    if (limit > 32) limit = 32;
    int8_t target = error;
    if (target > 0) --target;
    if (target < 0) ++target;
    target *= 4;
    if (target > limit) target = limit;
    if (target < -limit) target = -limit;
    // Cross zero before correcting the other wheel.
    if ((reduction > 0 && target < 0) || (reduction < 0 && target > 0)) target = 0;
    for (uint8_t i = 0; i < 2 && reduction != target; ++i) reduction += reduction < target ? 1 : -1;
    return true;
  }
};
