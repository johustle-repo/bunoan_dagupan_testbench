#pragma once

// Units are the user's mapped RPM display scale, not physical engine RPM.
inline float stepMappedRpm(float current, float target, float elapsedSeconds) {
  const float step = 100.0f * elapsedSeconds; // 300 -> 0 takes 3 seconds.
  if (target > current) return target - current <= step ? target : current + step;
  return current - target <= step ? target : current - step;
}
