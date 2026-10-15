#pragma once
#include <Arduino.h>
#include <Wire.h>

// INA219 battery monitor on the ESP32 I2C bus. The Nano's I2C pins (A4/A5)
// carry TPS and Reference 1, so the sensor lives here instead.
// Battery + -> VIN+, VIN- -> load +, battery - -> GND shared with the ESP32.
namespace battery {

constexpr uint8_t SDA_PIN = 21, SCL_PIN = 22;
constexpr uint8_t ADDRESS = 0x40;          // A0/A1 open on the common module.
constexpr float SHUNT_OHMS = 0.1f;         // "R100" shunt on the common module.
constexpr uint32_t READ_INTERVAL_MS = 250, RETRY_INTERVAL_MS = 2000;
// 32 V bus range, ±320 mV shunt range (±3.2 A), 128-sample averaging, continuous.
constexpr uint16_t CONFIG = 0x3FFF;
constexpr uint8_t REG_CONFIG = 0x00, REG_SHUNT = 0x01, REG_BUS = 0x02;

struct Reading {
  bool available;  // Sensor answered and the bus reading did not overflow.
  float voltage;   // At VIN+, i.e. the battery terminal.
  float currentA;  // Positive while the battery supplies the load.
};

Reading latest{};
bool found = false;
uint32_t lastReadMs = 0, lastProbeMs = 0;

bool writeRegister(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(ADDRESS);
  Wire.write(reg);
  Wire.write(uint8_t(value >> 8));
  Wire.write(uint8_t(value));
  return Wire.endTransmission() == 0;
}

bool readRegister(uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(ADDRESS, uint8_t(2)) != 2) return false;
  value = uint16_t(Wire.read()) << 8;
  value |= Wire.read();
  return true;
}

void begin() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(20);
  found = writeRegister(REG_CONFIG, CONFIG);
  lastProbeMs = millis();
}

// Call every loop; reads at most every READ_INTERVAL_MS and re-probes an
// unplugged sensor every RETRY_INTERVAL_MS so it can be connected live.
void update() {
  const uint32_t now = millis();
  if (!found) {
    if (uint32_t(now - lastProbeMs) < RETRY_INTERVAL_MS) return;
    lastProbeMs = now;
    found = writeRegister(REG_CONFIG, CONFIG);
    if (!found) { latest.available = false; return; }
  }
  if (uint32_t(now - lastReadMs) < READ_INTERVAL_MS) return;
  lastReadMs = now;
  uint16_t bus, shunt;
  if (!readRegister(REG_BUS, bus) || !readRegister(REG_SHUNT, shunt)) {
    found = false;
    latest.available = false;
    return;
  }
  const float shuntV = int16_t(shunt) * 10e-6f;  // 10 µV per bit, signed.
  latest.available = !(bus & 0x01);                // OVF: math overflow.
  latest.voltage = (bus >> 3) * 0.004f + shuntV;   // 4 mV per bit; add shunt drop for VIN+.
  latest.currentA = shuntV / SHUNT_OHMS;
}

}  // namespace battery
