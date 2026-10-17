#pragma once

// 5-sensor diagnostic formulas. Each reading is either a value or a fault
// (signal outside the sensor's valid range), printed as "ERR".
struct SensorReading {
  bool fault;
  float value;
};

// IAT and ECT: non-linear lookup tables, volts falling as temperature rises.
constexpr int IAT_TABLE_SIZE = 5;
const float iatVoltsTable[IAT_TABLE_SIZE] = {2.806f, 2.300f, 1.850f, 1.550f, 1.300f};
const float iatTempsTable[IAT_TABLE_SIZE] = {15.0f, 25.0f, 35.0f, 45.0f, 55.0f};
constexpr int ECT_TABLE_SIZE = 6;
const float ectVoltsTable[ECT_TABLE_SIZE] = {3.564f, 2.500f, 1.500f, 0.800f, 0.300f, 0.100f};
const float ectTempsTable[ECT_TABLE_SIZE] = {20.0f, 40.0f, 65.0f, 90.0f, 110.0f, 120.0f};

// Linear interpolation between table points, clamped to the table ends.
inline float lookupTemperature(float voltage, const float* volts, const float* temps, int size) {
  if (voltage >= volts[0]) return temps[0];
  if (voltage <= volts[size - 1]) return temps[size - 1];
  for (int i = 0; i < size - 1; i++) {
    if (voltage <= volts[i] && voltage >= volts[i + 1]) {
      const float fraction = (voltage - volts[i]) / (volts[i + 1] - volts[i]);
      return temps[i] + fraction * (temps[i + 1] - temps[i]);
    }
  }
  return temps[0];
}

inline SensorReading convertIat(float v) {
  if (v > 4.85f || v < 0.15f) return {true, 0};
  return {false, lookupTemperature(v, iatVoltsTable, iatTempsTable, IAT_TABLE_SIZE)};
}

inline SensorReading convertEct(float v) {
  if (v > 4.85f || v < 0.10f) return {true, 0};
  return {false, lookupTemperature(v, ectVoltsTable, ectTempsTable, ECT_TABLE_SIZE)};
}

// MAP: 0 V = 10 kPa, 4.21 V = 100 kPa.
inline SensorReading convertMap(float v) {
  if (v > 4.60f) return {true, 0};
  return {false, 10.0f + v * (100.0f - 10.0f) / 4.21f};
}

// O2 to AFR: 0.1 V = 15.2:1 (lean), 0.9 V = 14.0:1 (rich).
inline SensorReading convertAfr(float v) {
  if (v > 1.20f || v < 0.02f) return {true, 0};
  return {false, 15.2f + (v - 0.1f) * (14.0f - 15.2f) / (0.9f - 0.1f)};
}

// TPS: 0 V = 0 %, 4.25 V = 100 %.
inline SensorReading convertTps(float v) {
  if (v > 4.75f) return {true, 0};
  return {false, constrain(v * 100.0f / 4.25f, 0.0f, 100.0f)};
}
