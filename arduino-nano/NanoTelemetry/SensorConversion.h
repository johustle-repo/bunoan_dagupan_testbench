#pragma once

// Provisional linear estimates from ECU TESTBENCH DISPLAY.docx.
// These are not verified curves for the connected simulator or ECU.
struct SensorConversion {
  bool available;
  float value;
};

inline SensorConversion interpolateSensor(float voltage, float v0, float y0,
                                          float v1, float y1) {
  float low = v0 < v1 ? v0 : v1;
  float high = v0 > v1 ? v0 : v1;
  if (!(voltage >= low && voltage <= high)) return {false, 0};
  return {true, y0 + (voltage - v0) * (y1 - y0) / (v1 - v0)};
}

inline SensorConversion convertSensor(uint8_t channel, float voltage) {
  switch (channel) {
    case 0: return interpolateSensor(voltage, 3.0f, 30.0f, 1.72f, 50.0f); // IAT
    case 1: return interpolateSensor(voltage, 3.0f, 20.0f, 0.43f, 100.0f); // ECT
    case 3: return interpolateSensor(voltage, 0.78f, 20.0f, 4.0f, 101.3f); // MAP
    case 4: return interpolateSensor(voltage, 0.6f, 0.0f, 3.5f, 100.0f); // TPS
    default: return {false, 0}; // O2 is voltage, not AFR.
  }
}
