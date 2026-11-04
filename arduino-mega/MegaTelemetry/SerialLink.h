#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

// One-way SoftwareSerial at 19200 baud, 8N1, wired like the Uno/Nano sets:
// Mega D6 TX -> ESP32 GPIO17 RX, Mega D5 <- ESP32 GPIO16 (unused; the ESP32
// never transmits, and D5 has no pin-change interrupt on the Mega anyway).
// Transmission blocks interrupts briefly; the sketch pauses the D7/D8 window
// for that time so the frequencies stay accurate.
constexpr bool ESP_LINK_ENABLED = true;
SoftwareSerial arduino(5, 6); // RX, TX. Mega D6 -> ESP32 GPIO17.
static char linkBuffer[96];

inline void beginEspLink() {
  if (!ESP_LINK_ENABLED) return;
  arduino.begin(19200);
  arduino.stopListening(); // One-way telemetry; no incoming commands.
}

// 0-30 V input in mV; 65535 = no input attached (NAN), shown as ERR.
inline unsigned toMillivolts(float v) { return isnan(v) ? 65535u : unsigned(v * 1000 + 0.5f); }

inline void sendEspSnapshot(const float* raw, float rpmHz, float speedHz, bool keyOn, bool absOn, float declogHz,
                            const float* referenceV, float batteryV) {
  if (!ESP_LINK_ENABLED) return;
  // U6 = Mega 2560 Pro: key, ABS, 5 x ADC in 1/16 counts, D7 RPM Hz and D8
  // speed Hz in 1/100 Hz, Reference 1/2 and the battery in mV (0-30 V
  // inputs), Declog on/off (above 0 Hz) and the Declog Hz x100 (the A9 pot's
  // 0-100 Hz on D2). Integers avoid AVR printf float support. XOR
  // checksum covers the payload before '*'.
  const int count = snprintf(linkBuffer, sizeof(linkBuffer),
    "U6,%u,%u,%u,%u,%u,%u,%u,%lu,%lu,%u,%u,%u,%u,%u", unsigned(keyOn), unsigned(absOn), unsigned(raw[0] * 16 + 0.5f),
    unsigned(raw[1] * 16 + 0.5f), unsigned(raw[2] * 16 + 0.5f),
    unsigned(raw[3] * 16 + 0.5f), unsigned(raw[4] * 16 + 0.5f),
    (unsigned long)(rpmHz * 100 + 0.5f), (unsigned long)(speedHz * 100 + 0.5f),
    toMillivolts(referenceV[0]), toMillivolts(referenceV[1]), toMillivolts(batteryV), unsigned(declogHz > 0), unsigned(declogHz * 100 + 0.5f));
  if (count < 0 || count > int(sizeof(linkBuffer) - 5)) return;
  uint8_t checksum = 0;
  for (int i = 0; i < count; i++) checksum ^= uint8_t(linkBuffer[i]);
  snprintf(linkBuffer + count, sizeof(linkBuffer) - count, "*%02X\n", checksum);
  arduino.write(reinterpret_cast<const uint8_t*>(linkBuffer), count + 4);
}
