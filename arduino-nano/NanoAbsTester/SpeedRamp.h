#pragma once

// Eases a wheel speed toward the target at 100 km/h per second, so braking
// from 250 km/h to 0 takes 2.5 seconds and releasing the brake climbs back.
inline float stepSpeed(float current, float target, float elapsedSeconds) {
  const float step = 100.0f * elapsedSeconds;
  if (target > current) return target - current <= step ? target : current + step;
  return current - target <= step ? target : current - step;
}
