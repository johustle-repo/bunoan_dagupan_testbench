#pragma once

// Wheel speed in km/h ramps at 100 km/h per second, so 250 -> 0 takes 2.5 seconds.
inline float stepSpeed(float current, float target, float elapsedSeconds) {
  const float step = 100.0f * elapsedSeconds;
  if (target > current) return target - current <= step ? target : current + step;
  return current - target <= step ? target : current - step;
}
