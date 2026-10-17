#pragma once
#include <math.h>

// 5-sensor diagnostic formulas, the same as arduino-mega/MegaTelemetry's
// SensorConversion.h. The board sends raw ADC counts; every engineering value
// is derived here. A signal outside a sensor's valid range is a fault (the
// Mega prints "ERR"): the reading is unavailable and, after 2 s, sets a DTC.

constexpr float SENSOR_SUPPLY_V = 5.0f;

struct SensorReading {
  bool available;
  float value;
  bool inSpec;  // Inside the formula's calibrated span.
};

// Fault limits per channel (IAT, ECT, O2, MAP, TPS): below low or above high.
// A low limit of 0 means the sensor has no low fault.
struct FaultLimits {
  float low, high;
};
constexpr FaultLimits FAULT_LIMITS[5] = {{0.15f, 4.85f}, {0.10f, 4.85f}, {0.02f, 1.20f}, {0.0f, 4.60f}, {0.0f, 4.75f}};

inline bool sensorFault(uint8_t channel, float voltage) {
  return voltage < FAULT_LIMITS[channel].low || voltage > FAULT_LIMITS[channel].high;
}
inline bool sensorFaultHigh(uint8_t channel, float voltage) { return voltage > FAULT_LIMITS[channel].high; }

// IAT and ECT: non-linear lookup tables, volts falling as temperature rises.
struct TempTable {
  uint8_t channel;
  const float* volts;
  const float* temps;
  uint8_t size;
};
const float IAT_VOLTS[5] = {2.806f, 2.300f, 1.850f, 1.550f, 1.300f};
const float IAT_TEMPS[5] = {15.0f, 25.0f, 35.0f, 45.0f, 55.0f};
const float ECT_VOLTS[6] = {3.564f, 2.500f, 1.500f, 0.800f, 0.300f, 0.100f};
const float ECT_TEMPS[6] = {20.0f, 40.0f, 65.0f, 90.0f, 110.0f, 120.0f};
const TempTable IAT_MODEL{0, IAT_VOLTS, IAT_TEMPS, 5};
const TempTable ECT_MODEL{1, ECT_VOLTS, ECT_TEMPS, 6};

// Linear interpolation between table points, clamped to the table ends.
inline float lookupTemperature(const TempTable& t, float voltage) {
  if (voltage >= t.volts[0]) return t.temps[0];
  if (voltage <= t.volts[t.size - 1]) return t.temps[t.size - 1];
  for (uint8_t i = 0; i + 1 < t.size; ++i)
    if (voltage <= t.volts[i] && voltage >= t.volts[i + 1])
      return t.temps[i] + (voltage - t.volts[i]) / (t.volts[i + 1] - t.volts[i]) * (t.temps[i + 1] - t.temps[i]);
  return t.temps[0];
}

inline SensorReading convertTemperature(const TempTable& t, float voltage) {
  if (sensorFault(t.channel, voltage)) return {false, 0, false};
  return {true, lookupTemperature(t, voltage), voltage <= t.volts[0] && voltage >= t.volts[t.size - 1]};
}

// MAP: 0 V = 10 kPa, 4.21 V = 100 kPa.
inline SensorReading convertMap(float voltage) {
  if (sensorFault(3, voltage)) return {false, 0, false};
  return {true, 10.0f + voltage * (100.0f - 10.0f) / 4.21f, voltage <= 4.21f};
}

// TPS: 0 V = 0 %, 4.25 V = 100 %.
inline SensorReading convertTps(float voltage) {
  if (sensorFault(4, voltage)) return {false, 0, false};
  const float pct = voltage * 100.0f / 4.25f;
  return {true, pct < 0 ? 0 : pct > 100 ? 100 : pct, voltage <= 4.25f};
}

// O2 volts to AFR: 0.1 V = 15.2:1 (lean), 0.9 V = 14.0:1 (rich). Lambda is
// AFR / 14.7, for the dashboard's lambda scale and AFR gauge.
constexpr float STOICH_AFR = 14.7f;
struct OxygenReading {
  float volts;
  float lambda;
  bool afrAvailable;
  float afr;
  bool inSpec;
  const char* mixture;
};

inline OxygenReading convertOxygen(float voltage) {
  if (sensorFault(2, voltage)) return {voltage, 0, false, 0, false, "fault"};
  const float afr = 15.2f + (voltage - 0.1f) * (14.0f - 15.2f) / (0.9f - 0.1f);
  const char* mixture = afr < 14.4f ? "rich" : afr <= 15.0f ? "stoich" : "lean";
  return {voltage, afr / STOICH_AFR, true, afr, voltage >= 0.1f && voltage <= 0.9f, mixture};
}

// Diagnostic trouble codes. Index matches the Nano channel order
// IAT, ECT, O2, MAP, TPS. Blink codes follow the common Honda PGM-FI
// MIL pattern (MAP 1, ECT 7, TPS 8, IAT 9, O2 21).
struct SensorDtc {
  const char* general;   // Injected / generic circuit malfunction.
  const char* low;       // Circuit low input.
  const char* high;      // Circuit high input.
  uint8_t blink;
};
constexpr SensorDtc SENSOR_DTCS[5] = {
  {"P0110", "P0112", "P0113", 9},   // IAT
  {"P0115", "P0117", "P0118", 7},   // ECT
  {"P0130", "P0131", "P0132", 21},  // O2
  {"P0105", "P0107", "P0108", 1},   // MAP
  {"P0120", "P0122", "P0123", 8},   // TPS
};

constexpr float COOLANT_WARNING_ON_C = 105.0f;
constexpr float COOLANT_WARNING_OFF_C = 100.0f;
