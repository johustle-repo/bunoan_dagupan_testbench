#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

// Enabled at user's explicit request. Direct 5 V-to-3.3 V wiring risks damage;
// voltage conditioning remains the recommended electrical connection.
constexpr bool ESP_LINK_ENABLED = true;
// One-way SoftwareSerial at 19200 baud, 8N1: Uno D6 TX -> ESP32 GPIO17 RX.
// Transmission blocks interrupts briefly; verify D7 frequency accuracy.
SoftwareSerial arduino(5, 6); // RX, TX. Uno D6 -> ESP32 GPIO17.
static char linkBuffer[96];


inline void beginEspLink() {
  if (!ESP_LINK_ENABLED) return;
  arduino.begin(19200);
  arduino.stopListening(); // One-way telemetry; no incoming commands.
}

inline void sendEspSnapshot(const float* raw, float hz, bool keyOn, bool brakePressed, float mappedRpm) {
  if (!ESP_LINK_ENABLED) return;
  // Integers avoid AVR printf float support. ADC values in 1/16 counts;
  // frequency in 1/100 Hz. XOR checksum covers the payload before '*'.
  const int count = snprintf(linkBuffer, sizeof(linkBuffer),
    "U3,%u,%u,%u,%u,%u,%u,%u,%lu,%u", unsigned(keyOn), unsigned(brakePressed), unsigned(raw[0] * 16 + 0.5f),
    unsigned(raw[1] * 16 + 0.5f), unsigned(raw[2] * 16 + 0.5f),
    unsigned(raw[3] * 16 + 0.5f), unsigned(raw[4] * 16 + 0.5f),
    (unsigned long)(hz * 100 + 0.5f), unsigned(mappedRpm * 100 + 0.5f));
  if (count < 0 || count > int(sizeof(linkBuffer) - 5)) return;
  uint8_t checksum = 0;
  for (int i = 0; i < count; i++) checksum ^= uint8_t(linkBuffer[i]);
  snprintf(linkBuffer + count, sizeof(linkBuffer) - count, "*%02X\n", checksum);
  arduino.write(reinterpret_cast<const uint8_t*>(linkBuffer), count + 4);
}
