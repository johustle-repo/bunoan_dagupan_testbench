#pragma once
#include <stdint.h>

// Demonstration limits relative to measured simulator endpoints, not ECU DTCs.
struct SimulatedFault {
  uint8_t active = 0, pending = 0;
  uint32_t since = 0;
  void reset() { active = pending = 0; since = 0; }
  void update(uint8_t candidate, uint32_t now) {
    if (candidate != pending) { pending = candidate; since = now; }
    const uint32_t hold = candidate ? 2000 : 3000;
    if (uint32_t(now - since) >= hold) active = candidate;
  }
};

inline uint8_t simulatorFaultMask(const float* raw, uint8_t active) {
  const float zero[] = {708.3f,747.2f,0,78.5f,76};
  const float maximum[] = {343.4f,61,257.8f,889,893};
  uint8_t mask = 0;
  for (uint8_t i=0;i<5;++i) {
    const float p = (raw[i]-zero[i])*100/(maximum[i]-zero[i]);
    const float tolerance = active & (1<<i) ? 3.0f : 5.0f;
    if (p < -tolerance || p > 100+tolerance) mask |= 1<<i;
  }
  return mask;
}
