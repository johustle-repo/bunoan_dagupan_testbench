#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

// One-way SoftwareSerial at 19200 baud, 8N1, wired like the Uno/Nano sets:
// Mega D6 TX -> ESP32 GPIO17 RX, Mega D5 <- ESP32 GPIO16 (unused; the ESP32
// never transmits, and D5 has no pin-change interrupt on the Mega anyway).
// Transmission blocks interrupts briefly; the sketch pauses the D7 window
// for that time so the frequency stays accurate.
constexpr bool ESP_LINK_ENABLED = true;
SoftwareSerial arduino(5, 6); // RX, TX. Mega D6 -> ESP32 GPIO17.
static char linkBuffer[96];

inline void beginEspLink() {
  if (!ESP_LINK_ENABLED) return;
  arduino.begin(19200);
  arduino.stopListening(); // One-way telemetry; no incoming commands.
}

inline void sendEspSnapshot(const float* raw, float hz, bool keyOn, bool brakePressed, float mappedRpm,
                            const float* referenceV, float batteryV, float currentA) {
  if (!ESP_LINK_ENABLED) return;
  // U5 = Mega: the Nano's U4 fields plus battery and current. Integers avoid
  // AVR printf float support. ADC values in 1/16 counts; frequency in 1/100 Hz;
  // references and battery in mV (0-30 V inputs); current in mA (0-10 A). XOR checksum covers the
  // payload before '*'.
  const int count = snprintf(linkBuffer, sizeof(linkBuffer),
    "U5,%u,%u,%u,%u,%u,%u,%u,%lu,%u,%u,%u,%u,%u", unsigned(keyOn), unsigned(brakePressed), unsigned(raw[0] * 16 + 0.5f),
    unsigned(raw[1] * 16 + 0.5f), unsigned(raw[2] * 16 + 0.5f),
    unsigned(raw[3] * 16 + 0.5f), unsigned(raw[4] * 16 + 0.5f),
    (unsigned long)(hz * 100 + 0.5f), unsigned(mappedRpm * 100 + 0.5f),
    unsigned(referenceV[0] * 1000 + 0.5f), unsigned(referenceV[1] * 1000 + 0.5f),
    unsigned(batteryV * 1000 + 0.5f), unsigned(currentA * 1000 + 0.5f));
  if (count < 0 || count > int(sizeof(linkBuffer) - 5)) return;
  uint8_t checksum = 0;
  for (int i = 0; i < count; i++) checksum ^= uint8_t(linkBuffer[i]);
  snprintf(linkBuffer + count, sizeof(linkBuffer) - count, "*%02X\n", checksum);
  arduino.write(reinterpret_cast<const uint8_t*>(linkBuffer), count + 4);
}
