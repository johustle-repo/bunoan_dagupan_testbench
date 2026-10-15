#pragma once
#include <math.h>

// Sensor transfer models built from the client's ECU TESTBENCH DISPLAY.docx
// points. The Nano sends raw ADC counts; every engineering value is derived
// here, so changing a model never requires reflashing the Nano.
//
// Readings outside the client's specified range are still converted (as a
// real sensor would report them) and flagged with inSpec = false.

constexpr float SENSOR_SUPPLY_V = 5.0f;
// Outside this band a 5 V sensor circuit is treated as open or shorted.
constexpr float CIRCUIT_LOW_V = 0.20f;
constexpr float CIRCUIT_HIGH_V = 4.80f;

struct SensorReading {
  bool available;
  float value;
  bool inSpec;
};

// NTC thermistor with a pull-up to the 5 V sensor supply, the usual ECT/IAT
// circuit. With x = Rntc / Rpullup, V = 5x / (1 + x) and the beta model
// x = x1 * exp(B * (1/T - 1/T1)). Two client points fix x1 and B exactly.
struct NtcModel {
  float v1, t1C, v2, t2C;
};
constexpr NtcModel ECT_MODEL{3.0f, 20.0f, 0.43f, 100.0f};  // B ~ 3786 K
constexpr NtcModel IAT_MODEL{3.0f, 30.0f, 1.72f, 50.0f};   // B ~ 5148 K

inline float ntcRatio(float voltage) { return voltage / (SENSOR_SUPPLY_V - voltage); }

inline float ntcTemperatureC(const NtcModel& m, float voltage) {
  const float t1 = m.t1C + 273.15f, t2 = m.t2C + 273.15f;
  const float beta = logf(ntcRatio(m.v1) / ntcRatio(m.v2)) / (1.0f / t1 - 1.0f / t2);
  return 1.0f / (1.0f / t1 + logf(ntcRatio(voltage) / ntcRatio(m.v1)) / beta) - 273.15f;
}

inline bool circuitOk(float voltage) {
  return voltage >= CIRCUIT_LOW_V && voltage <= CIRCUIT_HIGH_V;
}

inline SensorReading convertTemperature(const NtcModel& m, float voltage) {
  if (!circuitOk(voltage)) return {false, 0, false};
  const float c = ntcTemperatureC(m, voltage);
  const float low = m.t1C < m.t2C ? m.t1C : m.t2C, high = m.t1C > m.t2C ? m.t1C : m.t2C;
  return {true, c, c >= low && c <= high};
}

// MAP sensors are linear: 0.78 V -> 20 kPa, 4.00 V -> 101.3 kPa (25.25 kPa/V).
inline SensorReading convertMap(float voltage) {
  if (!circuitOk(voltage)) return {false, 0, false};
  const float kpa = 20.0f + (voltage - 0.78f) * (101.3f - 20.0f) / (4.0f - 0.78f);
  return {true, kpa, kpa >= 20.0f && kpa <= 101.3f};
}

// TPS percent from learned closed / wide-open ADC endpoints, like an ECU's
// TPS reset procedure. Clamped to 0-100 %; inSpec is false beyond 2 %.
inline SensorReading convertTps(float raw, float closedRaw, float wotRaw, float voltage) {
  if (!circuitOk(voltage) || closedRaw == wotRaw) return {false, 0, false};
  const float pct = (raw - closedRaw) * 100.0f / (wotRaw - closedRaw);
  return {true, pct < 0 ? 0 : pct > 100 ? 100 : pct, pct >= -2.0f && pct <= 102.0f};
}

// Client encoding: O2 signal volts read directly as lambda, AFR = 14.7 * lambda
// (0.7 V -> 10.3, 1.0 V -> 14.7, 1.2 V -> 17.6). AFR is reported across the
// client's lambda/AFR table, lambda 0.70-1.60 (AFR 10.3-23.5).
constexpr float STOICH_AFR = 14.7f;
struct OxygenReading {
  float lambda;
  bool afrAvailable;
  float afr;
  bool inSpec;
  const char* mixture;
};

inline OxygenReading convertOxygen(float voltage) {
  const float lambda = voltage < 0 ? 0 : voltage;
  const bool onGauge = lambda >= 0.70f && lambda <= 1.60f;
  const char* mixture = lambda < 0.97f ? "rich" : lambda <= 1.03f ? "stoich" : "lean";
  return {lambda, onGauge, lambda * STOICH_AFR, lambda >= 0.7f && lambda <= 1.2f, mixture};
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
